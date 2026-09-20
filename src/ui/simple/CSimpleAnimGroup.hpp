#ifndef UI_SIMPLE_C_SIMPLE_ANIM_GROUP_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_GROUP_HPP

#include "ui/CScriptObject.hpp"
#include <storm/array/TSGrowableArray.hpp>
#include <cstdint>

class CSimpleAnim;
class CScriptRegion;
class CStatus;
class XMLNode;

// Recovered from the reference's tables at 00a440a4 and 00a440bc, both {int, const char*} triples.
enum ANIM_LOOPTYPE {
    ANIM_LOOPTYPE_NONE      = 0,
    ANIM_LOOPTYPE_REPEAT    = 1,
    ANIM_LOOPTYPE_BOUNCE    = 2,
    NUM_ANIM_LOOPTYPES      = 3
};

enum ANIM_LOOPSTATE {
    ANIM_LOOPSTATE_NONE     = 0,
    ANIM_LOOPSTATE_FORWARD  = 1,
    ANIM_LOOPSTATE_REVERSE  = 2,
    NUM_ANIM_LOOPSTATES     = 3
};

const char* AnimLoopTypeName(ANIM_LOOPTYPE loopType);
bool AnimLoopTypeFromName(const char* name, ANIM_LOOPTYPE& loopType);
const char* AnimLoopStateName(ANIM_LOOPSTATE loopState);

class CSimpleAnimGroup : public CScriptObject {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        CScriptRegion* m_region = nullptr;

        // Owned. The group destroys its animations with it, which is why CreateAnimation hands
        // back a pointer the caller never frees.
        TSGrowableArray<CSimpleAnim*> m_animations;

        ANIM_LOOPTYPE m_looping = ANIM_LOOPTYPE_NONE;
        ANIM_LOOPSTATE m_loopState = ANIM_LOOPSTATE_NONE;

        float m_initialOffsetX = 0.0f;
        float m_initialOffsetY = 0.0f;

        bool m_playing = false;
        bool m_paused = false;
        bool m_pendingFinish = false;

        // Driver state. m_currentOrder is -1 whenever the group is idle, and the group runs one
        // order at a time: every animation of that order together, then on to the next.
        float m_elapsed = 0.0f;
        float m_progress = 0.0f;
        float m_orderDuration = 0.0f;
        int32_t m_currentOrder = -1;

        // Set when Stop or the finish path is already unwinding, so a handler that calls back into
        // the group cannot start a second unwind on top of the first.
        bool m_stopping = false;

        // Set when a loop boundary was crossed and the animations of the outgoing order still have
        // a contribution applied that has to come off before the new one goes on.
        bool m_unapplyPending = false;


        // The handler slots, all seven confirmed against the reference's own GetScriptByName
        // (FUN_00497800) rather than the string table alone -- which is how OnLoad turned up after
        // being missed. The four wrapper signatures come from that function verbatim; OnLoad,
        // OnPlay and OnPause carry none there and carry none here.
        //
        // Nothing FIRES these yet -- that is the driver, stage 4. They are here so SetScript,
        // GetScript and HasScript store and report a handler instead of raising "doesn't have a
        // script", which is what FrameXML would hit if the quartet were left unimplemented.
        ScriptIx m_onLoad;
        ScriptIx m_onPlay;
        ScriptIx m_onPause;
        ScriptIx m_onStop;
        ScriptIx m_onFinished;
        ScriptIx m_onUpdate;
        ScriptIx m_onLoop;

        // Virtual member functions
        virtual ~CSimpleAnimGroup();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual CScriptObject* GetScriptObjectParent();
        virtual void LoadXML(const XMLNode* node, CStatus* status);

        // Member functions
        CSimpleAnimGroup(CScriptRegion* region);

        // The reference refuses to play an empty group -- CSimpleAnimGroup::Play (FUN_0049a8f0)
        // returns 0 straight away when the animation list is empty. Animation::Play relies on
        // that, so an animation whose group holds nothing cannot start either.
        bool Play();
        void Pause();
        void Stop();
        void Finish();

        // ref: FUN_0049c350
        // The per-frame tick. Loops rather than running once: an order whose animations finish
        // part-way through the frame hands the leftover time to the next one, so a group of short
        // orders can complete several in a single frame.
        void OnUpdate(float elapsedSec);

        // ref: FUN_0049b470
        // Run when every animation of the current order is finished. Steps to the next order, or
        // loops, or ends the group.
        void AdvanceOrder(float remaining);

        // ref: FUN_0049ab60
        // Run when one animation finishes: ends the group once no other is still going.
        void OnAnimationFinished(CSimpleAnim* anim);

        // The animations of one order, in list order.
        void CollectOrder(int32_t order, TSGrowableArray<CSimpleAnim*>& out) const;

        // The longest startDelay + duration + endDelay among one order's animations.
        float OrderDuration(int32_t order) const;

        // ref: FUN_00497920
        // Terminates the tick's loop. Not optional -- see the definition.
        bool ShouldStopStepping() const;

        bool IsDone() const;

        // The highest Lua-facing (1-based) order among the group's animations, 0 when empty.
        int32_t GetMaxOrder() const;

        float GetDuration() const;

        CSimpleAnim* CreateAnimation(const char* type, const char* name);
        void AddAnimation(CSimpleAnim* anim);
        void RemoveAnimation(CSimpleAnim* anim);
};

#endif
