#ifndef UI_SIMPLE_C_SIMPLE_ANIM_GROUP_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_GROUP_HPP

#include "ui/CScriptObject.hpp"
#include <storm/array/TSGrowableArray.hpp>
#include <cstdint>

class CSimpleAnim;
class CScriptRegion;

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

        // Driver state; see the note on CSimpleAnim. Stage 4 moves these.
        float m_elapsed = 0.0f;
        float m_progress = 0.0f;


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

        // Member functions
        CSimpleAnimGroup(CScriptRegion* region);

        // The reference refuses to play an empty group -- CSimpleAnimGroup::Play (FUN_0049a8f0)
        // returns 0 straight away when the animation list is empty. Animation::Play relies on
        // that, so an animation whose group holds nothing cannot start either.
        bool Play();
        void Pause();
        void Stop();
        void Finish();

        bool IsDone() const;

        // The highest Lua-facing (1-based) order among the group's animations, 0 when empty.
        int32_t GetMaxOrder() const;

        float GetDuration() const;

        CSimpleAnim* CreateAnimation(const char* type, const char* name);
        void AddAnimation(CSimpleAnim* anim);
        void RemoveAnimation(CSimpleAnim* anim);
};

#endif
