#ifndef OBJECT_CLIENT_CG_OBJECT_C_HPP
#define OBJECT_CLIENT_CG_OBJECT_C_HPP

#include "object/Types.hpp"
#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject.hpp"
#include "util/GUID.hpp"
#include "world/Types.hpp"
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>

class CEffect;
class CGWorldFrame;
class CM2Model;
class CPassenger;
class SpellRec;
class SpellVisualEffectNameRec;
class SpellVisualKitModelAttachRec;
class SpellVisualKitRec;
class SpellVisualRec;

typedef void (*M2SequenceDoneCallback)(CM2Model* model, uint32_t boneId, uint32_t animId,
                                       int32_t a4, int32_t a5, WOWGUID owner);

// What CGObject_C::PlayKit is handed: one spell visual kit to play on the object (13 dwords in the
// reference; the indices are the reference's).
struct SPELLVISUALKITPARAMS {
    const SpellRec* m_spell = nullptr;              // [0]
    const SpellVisualKitRec* m_kit = nullptr;       // [1]
    // [2] which of the visual's kits this is: 0 cast, 1 impact, 2 state, 3 state done, 4 channel,
    // 5..8 the impact and area kits. It picks the sequence-done handler and flags.
    int32_t m_kitType = 0;
    const C3Vector* m_position = nullptr;           // [3] a point, for kits played at one
    WOWGUID m_transport = 0;                        // [4..5] what the point is relative to
    int32_t m_stateParam = 0;                       // [6]
    int32_t m_param7 = 0;                           // [7]
    int32_t m_param8 = 0;                           // [8]
    int32_t m_param9 = 0;                           // [9]
    WOWGUID m_target = 0;                           // [10..11] who the effects are for
    int32_t m_param = 0;                            // [12]
};

// The client object every world thing derives from (Object_C.cpp). Its vtable is the reference's
// 0x009f3a70, 66 slots, and the virtuals below are DECLARED IN THAT SLOT ORDER: the comment beside
// each is its offset. A subclass overrides by name, so the order here is what makes CGUnit_C's,
// CGPlayer_C's and CGGameObject_C's vtables line up with 0x00a34d90, 0x00a326c8 and 0x00a34640.
//
// Slots whose purpose is not established yet are named for their offset rather than guessed at;
// their bodies are the reference base's own, which for most of them is a constant.
class CGObject_C : public CGObject, public TSHashObject<CGObject_C, CHashKeyGUID> {
    public:
        // Public static variables

        // The object the highlight was last raised on (0x00cd7770).
        static WOWGUID s_highlightGUID;

        // Public member variables
        TSLink<CGObject_C> m_link;
        // +0x40: when the object was last disabled (the world tick, DAT_00cd76ac).
        uint32_t m_disableTimeMs = 0;
        // +0x8c: the quest-giver marker model attached over the object, and the status (+0x90)
        // and override (+0x94) it was picked from.
        CM2Model* m_questMarker = nullptr;
        uint32_t m_questStatus = 0;
        uint32_t m_questMarkerOverride = 0;
        // +0x98: the scale the object draws at, and +0x9c the multiplier GetScale applies to it.
        float m_scale = 1.0f;
        float m_scaleMultiplier = 1.0f;
        // +0xa0: when a scale ease began (0 when none is running), and +0xa4 the scale it began at.
        uint32_t m_scaleEaseStart = 0;
        float m_scaleEaseFrom = 0.0f;
        // Head of the object's effect list (reference +0xa8), linked through CEffect::m_linkNext.
        CEffect* m_effects = nullptr;
        // +0xac: the object's height in model units, which the name and marker sit above.
        float m_height = 0.0f;
        // +0xb0: the object's name plate, owned by PlayerName.cpp.
        void* m_nameDesc = nullptr;
        // +0xb4
        CM2Model* m_model = nullptr;
        // +0xb8
        HWORLDOBJECT m_worldObject = 0;
        // +0xbc, the state word.
        uint32_t m_lockCount        : 16;
        uint32_t m_disabled         : 1;    // 0x10000
        uint32_t m_inReenable       : 1;    // 0x20000
        uint32_t m_postInited       : 1;    // 0x40000
        uint32_t m_flag19           : 1;    // 0x80000
        uint32_t m_disablePending   : 1;    // 0x100000
        uint32_t m_flag21           : 1;    // 0x200000, model not loaded when OnModelLoaded ran
        uint32_t m_flag22           : 1;    // 0x400000
        uint32_t m_unreached        : 1;    // 0x800000, the map walk did not reach it this frame
        uint32_t m_highlight        : 3;    // 0x7000000, one bit per highlight source
        uint32_t m_flag27           : 5;
        // +0xc0: the alpha fade: when it began, how long it runs (0 when none), the alpha now
        // (+0xc8), where it began (+0xc9) and where it is going (+0xca), and +0xcb the ceiling the
        // object's own alpha scales it by.
        uint32_t m_fadeStart = 0;
        uint32_t m_fadeDuration = 0;
        uint8_t m_alpha = 0;
        uint8_t m_alphaFrom = 0;
        uint8_t m_alphaTo = 0;
        uint8_t m_alphaScale = 0xFF;
        // +0xcc: the object's ObjectEffect manager (ObjectEffect.cpp), when it has one.
        void* m_objectEffects = nullptr;

        // Virtual public member functions, in the reference's slot order.
        virtual ~CGObject_C();                                                  // 0x000
        virtual void Disable();                                                 // 0x004
        virtual void Reenable();                                                // 0x008
        virtual void PostReenable();                                            // 0x00c
        virtual void HandleOutOfRange(OUT_OF_RANGE_TYPE type) {}                // 0x010
        virtual void UpdateWorldObject(int32_t noRelink);                       // 0x014
        virtual int32_t ShouldFadeOut() { return 1; }                           // 0x018
        virtual void UpdateModel(int32_t a2) {}                                 // 0x01c
        virtual C3Vector GetHeadPosition() const;                               // 0x020
        virtual void* Virtual024() { return nullptr; }                          // 0x024
        virtual void* Virtual028() { return nullptr; }                          // 0x028
        virtual C3Vector GetPosition() const;                                   // 0x02c
        virtual C3Vector GetRawPosition() const { return this->GetPosition(); } // 0x030
        virtual float GetFacing() const;                                        // 0x034
        virtual float GetRawFacing() const;                                     // 0x038
        virtual float GetBaseScale() const;                                     // 0x03c
        virtual WOWGUID GetTransportGUID() const;                               // 0x040
        virtual C4Quaternion GetRotation() const;                               // 0x044
        virtual void SetParticleRelative(const C44Matrix* relative);            // 0x048
        virtual int32_t CanHaveQuestStatus() { return 0; }                      // 0x04c
        virtual void OnReenable() {}                                            // 0x050
        virtual void UpdateQuestMarker();                                       // 0x054
        virtual void AttachQuestMarker();                                       // 0x058
        virtual void ScaleQuestMarker();                                        // 0x05c
        virtual int32_t GetModelFileName(const char*& name) const;              // 0x060
        virtual void OnScaleChanged() {}                                        // 0x064
        virtual void OnScaleEaseDone() {}                                       // 0x068
        virtual void Virtual06C() {}                                            // 0x06c
        virtual void Virtual070() {}                                            // 0x070
        virtual void Virtual074() {}                                            // 0x074
        virtual int32_t Virtual078(int32_t* out) { *out = -1; return 1; }      // 0x078
        virtual float GetScale() const;                                         // 0x07c
        virtual void OnModelLoaded(CM2Model* model);                            // 0x080
        virtual void UpdateFade(uint32_t time);                                 // 0x084
        virtual void UpdateForFrame(CGWorldFrame* frame);                       // 0x088
        virtual int32_t PlaceModel(float elapsed);                              // 0x08c
        // 0x090: whether the object hides this frame, for itself or for another reason.
        virtual void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther); // 0x090
        virtual float GetRenderFacing() const { return this->GetFacing(); }     // 0x094
        virtual void Virtual098() {}                                            // 0x098
        virtual int32_t Virtual09C() { return 1; }                              // 0x09c
        virtual int32_t Virtual0A0(int32_t a2) { return 1; }                    // 0x0a0
        virtual int32_t CanHighlight();                                         // 0x0a4
        virtual int32_t CanBeTargetted();                                       // 0x0a8
        virtual int32_t Virtual0AC() { return 0; }                              // 0x0ac
        virtual void Virtual0B0() {}                                            // 0x0b0
        virtual int32_t Virtual0B4() { return 0; }                              // 0x0b4
        virtual void StopEffect(CEffect* effect);                               // 0x0b8
        virtual const SpellVisualRec* GetSpellVisualRec(const SpellRec* spell); // 0x0bc
        virtual void StartEffects(int32_t spellID, int32_t param);              // 0x0c0
        virtual void GetWorldMatrix(C44Matrix& matrix) const;                   // 0x0c4
        virtual void UpdateQuestMarkerSequence();                               // 0x0c8
        virtual int32_t GetNameText(int32_t a2, char* buffer, uint32_t size);   // 0x0cc
        virtual int32_t Virtual0D0(uint32_t flags);                             // 0x0d0
        virtual CM2Model* GetObjectModel();                                     // 0x0d4
        virtual const char* GetName() { return nullptr; }                       // 0x0d8
        virtual int32_t Virtual0DC(int32_t a2) { return 0; }                    // 0x0dc
        virtual void Virtual0E0() {}                                            // 0x0e0
        virtual int32_t Virtual0E4() { return 1; }                              // 0x0e4
        virtual float GetFadeInAlpha() { return 1.0f; }                         // 0x0e8
        virtual bool Virtual0EC() { return false; }                             // 0x0ec
        virtual int32_t Virtual0F0(int32_t a2) { return 0; }                    // 0x0f0
        virtual int32_t Virtual0F4(CPassenger* passenger, int32_t mode) { return 1; } // 0x0f4
        virtual float Virtual0F8();                                             // 0x0f8
        // 0x0fc: after a kit has played, for the object's own steps (the unit's animation and
        // sound). The base does nothing.
        virtual void PostPlayKit(const SpellRec* spell, const SpellVisualKitRec* kit, int32_t kitType,
                                 const C3Vector* position, int32_t stateParam, int32_t param7,
                                 WOWGUID target, int32_t param, const SpellVisualRec* visual,
                                 uint32_t flags, int32_t played) {}             // 0x0fc
        // 0x100: between a kit's body effects and its specials, for the effects only some objects
        // carry (a unit's weapons). The base does nothing.
        virtual void PlayKitExtras(const SpellRec* spell, const SpellVisualKitRec* kit, const C3Vector* offset,
                                   WOWGUID target, int32_t param, uint32_t flags,
                                   M2SequenceDoneCallback sequenceDone, int32_t* played) {} // 0x100
        // 0x104: before a kit plays; nonzero keeps it from playing. The base lets every kit play.
        virtual int32_t PrePlayKit(const SpellRec* spell, const SpellVisualKitRec* kit, int32_t kitType,
                                   const C3Vector* position, WOWGUID transport, int32_t stateParam,
                                   int32_t param7, int32_t param8, int32_t param9, uint32_t flags,
                                   const SpellVisualRec* visual) { return 0; } // 0x104

        // Public member functions
        CGObject_C() : m_lockCount(0), m_disabled(0), m_inReenable(0), m_postInited(0), m_flag19(0),
            m_disablePending(0), m_flag21(0), m_flag22(0), m_unreached(0), m_highlight(0), m_flag27(0) {}
        CGObject_C(uint32_t time, CClientObjCreate& objCreate);
        void AddWorldObject();
        int32_t IsInReenable();
        // Whether the object has a model and the model has loaded.
        int32_t IsModelLoaded();
        int32_t IsObjectLocked();
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        uint32_t GetBlock(uint32_t block) const;
        void SetBlock(uint32_t block, uint32_t value);

        // Which descriptor block a field of this object's data lives in. Lets the mirror name
        // fields by member rather than by a hand-counted dword index that would rot the moment a
        // descriptor struct gained a member.
        uint32_t BlockIndexOf(const void* field) const;
        void SetDisablePending(int32_t pending);
        void SetModel(CM2Model* model);
        void SetModelFinish(CM2Model* model);
        void SetObjectLocked(int32_t locked);
        void SetStorage(uint32_t* storage, uint32_t* saved);
        void SetTypeID(OBJECT_TYPE_ID typeID);

        // ref: FUN_00743680
        // The object's model has changed from `previous`: its effects hang on the new one.
        void ModelChanged(CM2Model* previous);
        // ref: FUN_007431e0
        void ReleaseModel();
        // ref: FUN_00743af0
        void UpdateEffectAttachments();
        // ref: FUN_00743b40
        // Stop every effect playing `spellID`, the persistent ones only when `includePersistent`.
        void StopEffects(int32_t spellID, int32_t includePersistent);
        // ref: FUN_00743bc0 / FUN_00743c70
        // Lower or raise one of the three highlight sources; the model glows while any is up.
        void Unhighlight(int32_t source);
        void Highlight(int32_t source);
        // ref: FUN_00743d50
        // The object's world entry goes; through a fade-out when the object asks for one.
        void RemoveWorldObject();
        // ref: FUN_00744030
        // Start an alpha fade to `alpha` (0..1) over `duration` ms, or set it at once when 0.
        void SetAlpha(float alpha, uint32_t duration);
        // ref: FUN_007441d0
        // Ease to `scale` from now.
        void SetScaleEase(float scale);
        // ref: FUN_00744230
        void UpdateHeight();
        // ref: FUN_007443d0
        void ReleaseQuestMarker();
        // ref: FUN_00744400
        // The quest-giver status the marker is drawn from (SMSG_QUESTGIVER_STATUS).
        void SetQuestStatus(uint32_t status);
        // ref: FUN_00744640
        // The anim kit a quest marker plays for the status, or 0.
        int32_t GetQuestMarkerAnimKit(int32_t completed);
        // ref: FUN_007446c0
        // Where the object's model is drawn this frame, from its animated placement when it has one.
        C3Vector GetModelWorldPosition();
        // ref: FUN_00744720
        void GetModelWorldMatrix(C44Matrix& out);
        // ref: FUN_00744790
        // One kit effect at `attachment`, for `target`, joining the object's effects.
        void AddKitEffect(int32_t attachment, uint32_t endTime, int32_t spellID, const SpellVisualKitRec* kit,
                          const SpellVisualEffectNameRec* effectName, uint32_t* flags,
                          M2SequenceDoneCallback sequenceDone, const C3Vector* offset, WOWGUID target,
                          int32_t param, const SpellVisualKitModelAttachRec* attach);
        // ref: FUN_00744a50
        void InitializeVisible();
        // ref: FUN_00744ac0
        // Stop every effect; when `force`, release them outright, keeping only timed placed ones.
        void StopAllEffects(int32_t force);
        // ref: FUN_00744bd0
        // Finish the effects of one of a spell's kits.
        void StopKitEffects(int32_t spellID, int32_t kitType, int32_t param, int32_t matchSpell, int32_t matchParam);
        // ref: FUN_00745140
        // Show or hide the marker with the object.
        void ShowQuestMarker(int32_t show);
        // ref: FUN_00745230
        // Play a spell visual kit on the object: its head, body, hand, breath, chest, special and
        // world effects, its model attaches, and the object's own pre and post steps.
        void PlayKit(const SPELLVISUALKITPARAMS& params);

        // The handlers a kit gives the effects it plays, by kit type.
        // ref: FUN_00743580
        static void KitEffectFinished(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_007435a0
        static void KitEffectLoop(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_00744870
        static void KitEffectOneShot(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_00744920
        static void KitEffectStateLoop(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_007449c0
        static void KitEffectStateStart(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_00743230
        static void KitEffectDissolveDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner);
        // ref: FUN_00743110
        static void ModelLoadedCallback(CM2Model* model, void* arg);
};

// ref: FUN_007440f0
// Every visible object updates its world entry.
void ObjectsUpdateWorldObjects();

// ref: FUN_00744140
// Every visible object's ObjectEffect manager updates.
void ObjectsUpdateObjectEffects();

// ref: FUN_007450b0
// The object sounds are heard from: the active mover, or the player -- or what the player is
// seeing through when it is far-sighting.
CGObject_C* GetSoundListenerObject();

// ref: FUN_007460c0
// The selection circle texture and the quest marker models, made at client start.
void ObjectsInitialize();

// ref: FUN_007435e0
// Make (create != 0) or drop the quest-giver marker models, one per marker kind.
void QuestMarkersLoad(int32_t create);

// ref: FUN_00744e50
void ObjectsShutdown();

// ref: FUN_00744eb0
// The selection circle's render states and textures.
int32_t SelectionCircleSetStates();

#endif
