#ifndef OBJECT_CLIENT_C_EFFECT_HPP
#define OBJECT_CLIENT_C_EFFECT_HPP

#include "util/GUID.hpp"

#include "model/CM2Model.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include "world/Types.hpp"

#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

#include <cstdint>

class CGObject_C;
class CM2Lighting;
class CM2Model;
class CM2Scene;
class LightningObject;
class MountTransitionObject;
class SpellVisualEffectNameRec;
class SpellVisualKitModelAttachRec;
class SpellVisualKitRec;

// The reference's CEffect (Effect_C.cpp, with its helpers in ObjectEffect.cpp): one model, sound or
// special visual played on or at an object -- a spell's hand glow, a level-up burst, a mount poof,
// the line of a fishing rod. 0x110 bytes in the reference; the offsets beside each member are the
// reference's, recovered from the constructor (FUN_006f9d70), the destructor (FUN_006f9ec0) and the
// functions below that write each one.
//
// Lifetime is a reference count (+0xf4). Each list that holds an effect counts once -- the owner's
// effect list, the global list -- and so does its model while it exists. Release drops one and
// destroys the effect at zero.
//
// Every live effect is on one of two global lists: s_effectList (0x00ca0504) while it plays, and
// s_finishedList (0x00ca0508) once Finish has run, where UpdateAll keeps it until its model has
// finished drawing and then lets it go.
class CEffect {
    public:
        // Flags (+0x48). The bits whose meaning is established, named by what reads them.
        enum {
            EFFECT_SOUND_ON_OWNER       = 0x1,       // the sound follows the owner
            EFFECT_POSITIONED           = 0x2,       // placed at m_position rather than attached
            EFFECT_HOLDS_ANIMATION      = 0x4,       // froze its owner's animation (HoldOwnerAnimation)
            EFFECT_LOOT_ART             = 0x8,
            EFFECT_SHAKE_CAMERA         = 0x10,      // the kit's camera shake plays with it
            EFFECT_NO_EXPIRE            = 0x20,
            EFFECT_NO_FADE              = 0x100,
            EFFECT_PLAYED               = 0x800,     // Play has run (or the effect needs none)
            EFFECT_FROM_SPELL_NO_SOUND  = 0x400,
            EFFECT_WITHOUT_OWNER        = 0x2000,
            EFFECT_FOLLOWS_OWNER        = 0x4000,    // re-placed on its owner every frame
            EFFECT_FACING_FROM_OWNER    = 0x8000,    // m_facing was taken from the owner
            EFFECT_ATTACH_OFFSET        = 0x10000,   // m_attachOffset is applied to the attachment
            EFFECT_EMISSION_COUNTDOWN   = 0x40000,   // emission stops after m_emitCountdown frames
            EFFECT_ITEM_VISUAL          = 0x80000,   // replaces an item section on the owner
            EFFECT_SOUND_ONLY           = 0x100000,  // finishes when its sound does
            EFFECT_SOUND_UNTRACKED      = 0x200000,  // the sound does not follow the owner's guid
            EFFECT_EMITTERS_400000      = 0x400000,  // raise 0x400000 on every emitter
            EFFECT_FINISHED             = 0x800000,
            EFFECT_MODEL_READY_ANIM     = 0x1000000,
            EFFECT_RIBBONS_TRAIL        = 0x2000000, // the model's 0x100000 state bit
        };

        // Static variables
        static CEffect* s_effectList;           // 0x00ca0504
        static CEffect* s_finishedList;         // 0x00ca0508
        // The twelve hard-coded SpellVisualEffectName ids ("HARDCODED Footstep Water Run Spray",
        // ..., "HARDCODED Achievement Base"), looked up by name at load (0x00ca050c).
        static int32_t s_hardcodedEffects[12];

        // Static functions

        // ref: FUN_006f7520
        static int32_t LoadHardcodedEffects();
        // ref: FUN_006f75b0
        static const SpellVisualEffectNameRec* GetHardcodedEffect(int32_t index);
        // ref: FUN_006f7680
        static void ModelLoadedCallback(CM2Model* model, void* arg);
        // ref: FUN_006f7b00
        static void AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId,
                                      uint32_t eventData, const C3Vector* position, uint32_t a6,
                                      WOWGUID owner);
        // ref: FUN_006f7480
        static void MountSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4,
                                      int32_t a5, WOWGUID owner);
        // ref: FUN_006f84f0
        static void ApplyModelAttach(const SpellVisualKitModelAttachRec* attach, C44Matrix& matrix,
                                     float facing);
        // ref: FUN_006f8600
        static void OnItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found);
        // ref: FUN_006f8a60
        static void StopDuplicates(CGObject_C* object, const SpellVisualEffectNameRec* effectName,
                                   int32_t attachment, int32_t spellID, int32_t kitID,
                                   const SpellVisualKitModelAttachRec* attach);
        // ref: FUN_006f8f50
        static void DrawFishingLine(CM2Model* model, CM2Lighting* lighting, void* arg);
        // ref: FUN_006f9610
        static void OnMountCreatureArrived(uint32_t id, const WOWGUID* guid, void* param, bool found);
        // ref: FUN_006fa3c0
        static void ReleaseAll();
        // ref: FUN_006fa450
        static void UpdateAll();

        // Member variables
        CM2Model* m_model = nullptr;                        // +0x00
        // +0x04: the rod a fishing line is drawn from -- a child of the owner's model, held by
        // reference, whose draw callback is DrawFishingLine.
        CM2Model* m_lineModel = nullptr;
        // +0x08: who the effect is for, and +0x10 the object it is attached to and listed on,
        // when that is someone else (a spell's target). Lookups prefer +0x10.
        WOWGUID m_owner = 0;
        WOWGUID m_attachedTo = 0;
        int32_t m_spellID = 0;                              // +0x18
        const SpellVisualKitRec* m_kit = nullptr;           // +0x1c
        const SpellVisualEffectNameRec* m_effectName = nullptr; // +0x20
        int32_t m_attachment = -1;                          // +0x24, -1 is ATTACH_NONE
        C3Vector m_attachOffset = { 0.0f, 0.0f, 0.0f };     // +0x28
        SOUNDKITOBJECT m_sound;                             // +0x34
        uint32_t m_flags = 0;                               // +0x48
        // +0x4c: the chain-lightning objects the effect owns, released with it.
        LightningObject* m_lightning[12] = {};
        uint32_t m_uint80 = 0;                              // +0x80
        uint32_t m_uint84 = 0;                              // +0x84
        float m_facing = 0.0f;                              // +0x88
        int32_t m_int8c = -1;                               // +0x8c
        MountTransitionObject* m_mountTransition = nullptr; // +0x90
        // +0x94: an id waiting on a cache -- the creature entry of a mount, or an item -- and
        // +0x98 what it resolved to: the mount's display, or the item's display.
        uint32_t m_pendingID = 0;
        int32_t m_displayID = 0;
        int32_t m_itemSection = 12;                         // +0x9c, 12 is none
        uint32_t m_param = 0;                               // +0xa0
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };         // +0xa4
        C3Vector m_worldPosition = { 0.0f, 0.0f, 0.0f };    // +0xb0, m_position through a transport
        WOWGUID m_transport = 0;                            // +0xc0
        uint32_t m_startTime = 0;                           // +0xc8
        uint32_t m_endTime = 0;                             // +0xcc
        uint32_t m_scaleResetTime = 0;                      // +0xd0
        uint32_t m_heldBody = 0;                            // +0xd4
        uint32_t m_heldMount = 0;                           // +0xd8
        // +0xe8: a transparency effect's alpha (char proc 14), and +0xec a colour effect's colour
        // (char proc 1), as 0xAARRGGBB.
        float m_alpha = 1.0f;
        uint32_t m_color = 0;
        HWORLDOBJECT m_worldObject = 0;               // +0xf0
        uint32_t m_refCount = 1;                            // +0xf4
        // +0xf8: the uses a lightning object holds; ReleaseUse finishes the effect at zero.
        uint32_t m_uses = 0;
        // +0xfc: frames left before emission stops, while EFFECT_EMISSION_COUNTDOWN is set.
        // Stop writes 0 and Start writes 2.
        uint32_t m_playState = 0;
        const SpellVisualKitModelAttachRec* m_modelAttach = nullptr;  // +0x100
        CEffect** m_linkPrev = nullptr;                     // +0x104
        CEffect* m_linkNext = nullptr;                      // +0x108

        // Member functions

        // An effect off the heap, held once by its creator (the reference's SMemAlloc of 0x110
        // bytes followed by the constructor at every creation site).
        static CEffect* Create();

        // ref: FUN_006f9d70
        CEffect();
        // ref: FUN_006f9ec0
        ~CEffect();

        // ref: FUN_006fa390
        void Release();

        // ref: FUN_006f74b0
        void Unlink();
        // ref: FUN_006f74f0
        void ReleaseLightning();
        // ref: FUN_006f75f0
        void SetEmittersFlag400000(int32_t enable);
        // ref: FUN_006f76c0
        void LinkToHead(CEffect** head);
        // ref: FUN_006f7720
        C3Vector& GetPosition(C3Vector& out);
        // ref: FUN_006f7850
        void DetachModel();
        // ref: FUN_006f7870
        void SetEmission(int32_t enable);
        // ref: FUN_006f78b0
        void Stop();
        // ref: FUN_006f7900
        void Start();
        // ref: FUN_006f7950
        static float GetOwnerScale(CGObject_C* owner);
        // ref: FUN_006f7a00
        void LinkToGlobalList();

        // ref: FUN_006f7c40
        void InitializeAtPoint(const C3Vector& position, uint32_t startTime, int32_t spellID,
                               const SpellVisualKitRec* kit, const SpellVisualEffectNameRec* effectName,
                               const WOWGUID& owner, uint32_t flags, M2SequenceDoneCallback sequenceDone,
                               uint32_t param, const SpellVisualKitModelAttachRec* attach,
                               WOWGUID transport);
        // ref: FUN_006f7d60
        void Initialize(int32_t spellID, const SpellVisualKitRec* kit,
                        const SpellVisualEffectNameRec* effectName, uint32_t flags,
                        M2SequenceDoneCallback sequenceDone, const C3Vector& position, uint32_t param,
                        WOWGUID transport);
        // ref: FUN_006f7e40
        void InitializeSound(int32_t soundKitID, uint32_t endTime, int32_t spellID,
                             const C3Vector* position, CGObject_C* owner, uint32_t param);
        // ref: FUN_006f7fd0
        void AttachToOwner(int32_t spellID, const SpellVisualKitRec* kit, WOWGUID owner,
                           uint32_t flags, uint32_t param);
        // ref: FUN_006f8040
        void Sub6f8040(uint32_t endTime, int32_t spellID, const SpellVisualKitRec* kit, WOWGUID owner,
                       uint32_t flags);
        // ref: FUN_006f80b0
        void HoldOwnerAnimation(uint32_t endTime, float holdSeconds, int32_t spellID,
                                const SpellVisualKitRec* kit, const WOWGUID& owner, uint32_t flags);
        // ref: FUN_006f82d0
        void ApplyItemSection();
        // ref: FUN_006f83d0
        int32_t CreateMountModel();
        // ref: FUN_006f8650
        void InitializeItemVisual(uint32_t endTime, uint32_t itemID, int32_t spellID,
                                  const SpellVisualKitRec* kit, const WOWGUID& owner, uint32_t flags);
        // ref: FUN_006f8700
        void RestoreItemSection();
        // ref: FUN_006f87c0
        void Finish();
        // ref: FUN_006f8970
        void UpdateVisibility(float unused, uint32_t flags);
        // ref: FUN_006f8ae0
        C44Matrix& GetWorldTransform(C44Matrix& out, CGObject_C* owner);
        // ref: FUN_006f8c50
        int32_t UpdateAttachment();
        // ref: FUN_006f8f40
        void ReleaseUse();
        // ref: FUN_006f9260
        void InitializeHardcoded(int32_t index, const WOWGUID& owner, M2SequenceDoneCallback sequenceDone);
        // ref: FUN_006f93a0
        void InitializeHardcodedAt(int32_t index, const C3Vector& position, const WOWGUID& owner,
                                   uint32_t flags, M2SequenceDoneCallback sequenceDone);
        // ref: FUN_006f94c0
        void InitializeAttached(int32_t attachment, int32_t spellID, const SpellVisualKitRec* kit,
                                const SpellVisualEffectNameRec* effectName, const WOWGUID& owner,
                                const WOWGUID& target, uint32_t flags, M2SequenceDoneCallback sequenceDone,
                                const C3Vector* offset, uint32_t param,
                                const SpellVisualKitModelAttachRec* attach);
        // ref: FUN_006f9670
        void InitializeMountTransition(int32_t spellID, const SpellVisualKitRec* kit, const WOWGUID& owner);
        // ref: FUN_006f97d0
        void AttachFishingLine(CGObject_C* owner, const WOWGUID& bobber);
        // ref: FUN_006f9840
        void Play();
        // ref: FUN_006fa050
        void Update();
        // ref: FUN_006fa5b0
        void InitializeLootArt(CGObject_C* object, M2SequenceDoneCallback sequenceDone);
};

// ref: FUN_00782000
// A world object placed by a matrix rather than attached to anything: what an effect played at a
// point lives in. It is linked into the map at once.
HWORLDOBJECT CWorldAddPlacedObject(CM2Model* model, const C44Matrix& matrix, void* handler,
                                   void* handlerParam, uint32_t param32);

#endif
