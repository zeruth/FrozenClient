#ifndef UI_SIMPLE_C_SIMPLE_ANIM_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_HPP

#include "ui/CScriptObject.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include <cstdint>

class CSimpleAnimGroup;
class CScriptRegion;
class CStatus;
class XMLNode;

// The smoothing curve names, recovered from the reference's enum-to-string table at 00a44068:
// five {float, float, const char*} rows, the pair being the ease-in and ease-out weights.
//
// OUT_IN carries the SAME pair as IN_OUT, (1, 1), and the reference's lookup is a linear scan that
// returns the first row within epsilon. So Animation:GetSmoothing() can never answer "OUT_IN" -- an
// animation set to it reports IN_OUT. Reproduced rather than corrected: it is observable from Lua.
enum ANIM_SMOOTHING {
    ANIM_SMOOTHING_NONE     = 0,
    ANIM_SMOOTHING_IN       = 1,
    ANIM_SMOOTHING_OUT      = 2,
    ANIM_SMOOTHING_IN_OUT   = 3,
    ANIM_SMOOTHING_OUT_IN   = 4,
    NUM_ANIM_SMOOTHINGS     = 5
};

const char* AnimSmoothingName(ANIM_SMOOTHING smoothing);

// ref: FUN_00497ba0
// Map a linear 0..1 through the smoothing curve. See the definition for why OUT_IN is IN_OUT.
float AnimSmoothingApply(ANIM_SMOOTHING smoothing, float t);
bool AnimSmoothingFromName(const char* name, ANIM_SMOOTHING& smoothing);

class CSimpleAnim : public CScriptObject {
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
        CSimpleAnimGroup* m_group = nullptr;

        float m_duration = 0.0f;
        float m_startDelay = 0.0f;
        float m_endDelay = 0.0f;

        // Stored ZERO-BASED, as the reference stores it. Animation:SetOrder(n) writes n - 1 and
        // GetOrder reports m_order + 1. The constructor leaves it at -1, the reference's "unset"
        // sentinel, which CreateAnimation tests for before assigning a default.
        int8_t m_order = -1;

        ANIM_SMOOTHING m_smoothing = ANIM_SMOOTHING_NONE;
        float m_maxFramerate = 0.0f;

        // Seconds per frame, which the reference derives and stores beside the rate rather than
        // dividing each frame. Zero when the rate is zero, meaning uncapped.
        float m_maxFramerateInterval = 0.0f;

        // Driver state. m_progress is the fraction THROUGH THE DURATION, with the start delay
        // already subtracted and the result clamped to [0, 1]; m_progressWithDelay is the fraction
        // through startDelay + duration + endDelay, which is what IsDone tests. They differ
        // whenever either delay is non-zero.
        float m_elapsed = 0.0f;
        float m_progress = 0.0f;
        float m_progressWithDelay = 0.0f;

        // What was last handed to OnApply: the progress put through the smoothing curve, and
        // mirrored when the group is running this animation backwards.
        float m_appliedAmount = 0.0f;

        // maxFramerate does NOT slow the animation down. Elapsed advances every frame regardless;
        // this accumulator gates how often m_progress is RE-SAMPLED, so a capped animation moves
        // in visible steps while still finishing on time.
        float m_framerateAccum = 0.0f;

        // NONE unless the group is looping and told this animation which way it is going. A
        // REVERSE animation subtracts the END delay rather than the start one, and applies
        // 1 - progress.
        ANIM_LOOPSTATE m_loopState = ANIM_LOOPSTATE_NONE;

        // The reference does not store this: it derives it from m_progress through the smoothing
        // curve, and SetSmoothProgress drives the animation to the matching point. Frozen keeps it
        // as its own value because there is no curve evaluation without the driver. Stage 4 must
        // make the two consistent -- until then setting one does not move the other.
        float m_smoothProgress = 0.0f;

        bool m_playing = false;
        bool m_paused = false;

        // The handler slots, all six now read out of the reference's own GetScriptByName
        // (FUN_00497500) rather than inferred from the string table. Two things came out of that
        // which guessing had got wrong: there is an OnLoad, and the animation's OnFinished takes
        // NO wrapper where the group's does. An animation is told it finished; only a group is
        // told whether the finish was requested.
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

        // Virtual member functions
        virtual ~CSimpleAnim();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual CScriptObject* GetScriptObjectParent();
        virtual void LoadXML(const XMLNode* node, CStatus* status);

        // Member functions
        CSimpleAnim(CSimpleAnimGroup* group);

        // Playing an animation plays its whole GROUP first and gives up if the group refuses --
        // the reference's Animation::Play (FUN_0049ad80) opens by calling the group's Play and
        // returning 0 on failure. An animation does not run on its own.
        bool Play();
        void Pause();
        void Stop();
        void Finish();

        // ref: FUN_004985f0
        // Advance by step and recompute the progresses. Returns the time actually CONSUMED, which
        // is less than step on the frame the animation ends -- the group carries the remainder
        // into the next order rather than dropping it.
        float Advance(float step);

        // ref: FUN_00498d50
        // One tick. Returns whether this animation is finished, and writes the time it consumed.
        bool OnUpdate(float step, float& used);

        // Hand this frame's contribution to the region. Empty on the base animation, which has
        // nothing to contribute; each subclass overrides it.
        virtual void OnApply(float amount) {}

        // ref: FUN_00497700
        // Take a contribution back off again. For three of the four subclasses this is just
        // OnApply with the amount negated, which is exactly what the reference's shared body does
        // -- CSimpleScaleAnim is the exception and overrides it, because scale composes
        // multiplicatively and cannot be undone by negating.
        virtual void OnUnapply(float amount) { this->OnApply(-amount); }

        bool IsDone() const;
        bool IsDelaying() const;
        bool IsStopped() const;

        CScriptRegion* GetRegionParent();
        void SetParentGroup(CSimpleAnimGroup* group);
};

// ref: FUN_00497c30
// The <Scripts> block on an animation. Deliberately NOT CSimpleFrame::LoadXML_Scripts: the
// reference keeps two of these, one for frames at 0048fef0 whose complaint begins "Frame %s:" and
// this one whose complaint begins with the object's type and name. Sharing them would change the
// message FrameXML authors see.
void AnimLoadXML_Scripts(CScriptObject* object, const XMLNode* root, CStatus* status);

float AnimXmlOffset(const char* attr);

#endif
