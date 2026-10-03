#ifndef OBJECT_CLIENT_GAME_OBJECT_TYPES_HPP
#define OBJECT_CLIENT_GAME_OBJECT_TYPES_HPP

#include "object/client/ShipPath.hpp"
#include "object/movement/CPassenger.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGGameObject_C;
class CM2Model;
class CMapBaseObj;

// The behaviour a game object gets from its GAMEOBJECT_TYPE_*: CGGameObject_C's constructor makes
// one (FUN_00714250, by the type byte of GAMEOBJECT_BYTES_1) and keeps it at +0x1a0, and most of
// the object's own slots hand straight on to it. The reference names are lost (no RTTI); the classes
// are named by the types that use them.
//
// Each class's comment names its reference vtable. The root's 44 slots are declared in slot order;
// a class adding slots of its own (0xb0 onward) declares them after.

// GameObjectDataIndex (FUN_00746190): where in a type's 24 data fields (GameObjectStats_C::m_data)
// a meaning lives, or -1 when the type has no such field. The meanings the client asks for:
enum GAMEOBJECT_DATA {
    GO_DATA_LOCK            = 4,
    GO_DATA_CHAIR_SLOTS     = 11,
    GO_DATA_PAGE            = 15,
    GO_DATA_HIGHLIGHT       = 18,
    GO_DATA_TOOLTIP         = 19,
    GO_DATA_QUEST           = 20,
    GO_DATA_CUSTOM_ANIM     = 22,
};
int32_t GameObjectDataIndex(int32_t type, int32_t meaning);

// The root (0x00a336e0), which is also what a binder (type 4) and a camera (type 13) get as it
// stands. +0x08 is the error a refused use reports, +0x0c how near the player must be to use it.
class CGGameObjectType {
    public:
        CGGameObjectType(CGGameObject_C* owner, float useRange)
            : m_owner(owner), m_useRange(useRange) {}

        // ref: FUN_0070d950
        virtual ~CGGameObjectType() {}                                                  // 0x00
        virtual bool UsesModelBounds() { return true; }                                 // 0x04
        // Whether the object fades in when it is shown (CGGameObject_C slot 0xe4).
        virtual bool FadesIn() { return true; }                                         // 0x08
        // ref: FUN_0070bdb0
        virtual bool CanHighlight() { return this->CanUse(); }                          // 0x0c
        virtual int32_t NoHighlight() { return 0; }                                     // 0x10
        virtual int32_t GetCursor();                                                    // 0x14
        virtual bool CanUse();                                                          // 0x18
        virtual bool CanUseNow(int32_t* error, float* range, const char** spellName);   // 0x1c
        virtual bool IsInUseRange();                                                    // 0x20
        virtual int32_t Use();                                                          // 0x24
        // GAMEOBJECT_FLAGS changed; `changed` holds the bits that moved.
        virtual void OnFlagsChanged(uint32_t changed) {}                                // 0x28
        // The animation progress in GAMEOBJECT_DYNAMIC changed.
        virtual void OnAnimProgressChanged() {}                                         // 0x2c
        // GAMEOBJECT_BYTES_1's state went from `from` to `to`.
        virtual void OnStateChanged(int32_t from, int32_t to) {}                        // 0x30
        virtual int32_t PlaceModel(float elapsed) { return 1; }                         // 0x34
        virtual void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {} // 0x38
        virtual float GetFadeInAlpha() { return 1.0f; }                                 // 0x3c
        // An authored event key on the model ('$GO0', '$SND', ...).
        virtual void OnAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position,
                                 uint32_t a6) {}                                        // 0x40
        // The model's sequence was replaced before it ran out.
        virtual int32_t OnSequenceInterrupted() { return 1; }                           // 0x44
        // The model's sequence ran out.
        virtual void OnSequenceDone() {}                                                // 0x48
        virtual void PlayCustomAnim(int32_t state) {}                                   // 0x4c
        // ref: FUN_0070b220
        virtual const char* GetStateName() { return ""; }                               // 0x50
        virtual C3Vector GetPosition();                                                 // 0x54
        virtual C3Vector GetRawPosition();                                              // 0x58
        virtual float GetFacing();                                                      // 0x5c
        virtual float GetRawFacing();                                                   // 0x60
        virtual float GetScaleMultiplier() { return -1.0f; }                            // 0x64
        // A passenger joins (mode 1) or leaves (mode 2) the transport this object is.
        virtual void UpdatePassenger(CPassenger* passenger, int32_t mode) {}            // 0x68
        virtual int32_t Virtual06C(const C3Vector* position) { return 0; }              // 0x6c
        // The map object (WMO) the type draws, for the types that draw one.
        virtual void* GetMapObject() { return nullptr; }                                // 0x70
        virtual void PostInit(int32_t a4) {}                                            // 0x74
        // The object's GameObjectStats_C record has arrived.
        virtual void OnStatsLoaded() {}                                                 // 0x78
        virtual void OnReenable() {}                                                    // 0x7c
        virtual void OnDisable() {}                                                     // 0x80
        virtual void OnPostReenable() {}                                                // 0x84
        virtual void* GetTransportAnimation() { return nullptr; }                       // 0x88
        virtual void UpdateTransport(uint32_t time, int32_t a3) {}                      // 0x8c
        // Every frame, from CGGameObject_C::UpdateForFrame.
        virtual void UpdateFrame(uint32_t time) {}                                      // 0x90
        virtual void OnModelLoaded() {}                                                 // 0x94
        // The object's window (a quest giver's, a text's, a mailbox's) opened and closed.
        virtual void OnWindowOpened() {}                                                // 0x98
        virtual void OnWindowClosed() {}                                                // 0x9c
        virtual void Virtual0A0(int32_t a2) {}                                          // 0xa0
        // ref: FUN_0070bdc0
        virtual uint32_t AdjustTime(uint32_t time) { return time; }                     // 0xa4
        virtual int32_t Virtual0A8() { return 0; }                                      // 0xa8
        // ref: FUN_0070bdd0
        virtual float GetUseRange() { return this->m_useRange; }                        // 0xac

        // The data field holding `meaning` for the owner's type, or 0 (FUN_0070eef0 and the
        // inlined copies of it every slot repeats).
        int32_t GetData(int32_t meaning) const;

        CGGameObject_C* m_owner;            // +0x04
        int32_t m_useError = 0xEA;          // +0x08
        float m_useRange;                   // +0x0c
};

// What a type the client does not know gets (0x00a33618).
class CGGameObjectTypeUnknown : public CGGameObjectType {
    public:
        CGGameObjectTypeUnknown(CGGameObject_C* owner) : CGGameObjectType(owner, 5.0f) {}

        bool CanUse() override { return false; }                                        // 0x18
        // ref: FUN_0070c340
        const char* GetStateName() override { return "Unknown Object Type"; }           // 0x50
        virtual bool Virtual0B0(int32_t a2, int32_t a3) { return false; }               // 0xb0
};

// The animated types (0x00a33c38): the state in GAMEOBJECT_BYTES_1 picks one of the 13 GameObject
// animations (Closed, Opening, Open, ...), the model plays it, and its events play the display's
// sounds. Buttons, chests, traps, goobers and most of the rest use this one as it stands.
class CGGameObjectAnimated : public CGGameObjectType {
    public:
        CGGameObjectAnimated(CGGameObject_C* owner);
        ~CGGameObjectAnimated() override;

        // ref: FUN_00712510
        bool FadesIn() override { return this->m_animState != 0; }                      // 0x08
        void OnFlagsChanged(uint32_t changed) override;                                 // 0x28
        void OnAnimProgressChanged() override;                                          // 0x2c
        void OnStateChanged(int32_t from, int32_t to) override;                         // 0x30
        int32_t PlaceModel(float elapsed) override;                                     // 0x34
        void OnAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position, uint32_t a6) override; // 0x40
        int32_t OnSequenceInterrupted() override;                                       // 0x44
        void OnSequenceDone() override;                                                 // 0x48
        void PlayCustomAnim(int32_t state) override;                                    // 0x4c
        const char* GetStateName() override;                                            // 0x50
        void PostInit(int32_t a4) override;                                             // 0x74
        void OnDisable() override;                                                      // 0x80
        void OnPostReenable() override;                                                 // 0x84
        void UpdateFrame(uint32_t time) override;                                       // 0x90
        void OnModelLoaded() override;                                                  // 0x94

        virtual void SetAnimState(int32_t state);                                       // 0xb0

        // Play the state's animation on `model`, through the GameObject animation fallbacks.
        int32_t PlayAnimState(CM2Model* model);
        // The state the GAMEOBJECT_BYTES_1 state and the animation progress call for.
        void InitAnimState();

        // The animation state (an index into s_animStateSequences) asked for, the one playing,
        // and the world time the playing one ends at (or -1).
        int32_t m_animState = -1;           // +0x10
        int32_t m_playingState = -1;        // +0x14
        int32_t m_animEndTime = -1;         // +0x18
        // The display's sounds (the '$GO0'.. events and '$DSL') play through this one.
        SOUNDKITOBJECT* m_sound = nullptr;  // +0x1c
};

// Doors (0x00a33b80): collidable while closed.
class CGGameObjectDoor : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712520
        CGGameObjectDoor(CGGameObject_C* owner) : CGGameObjectAnimated(owner) { this->m_useError = 0xEB; }

        // ref: FUN_00712550
        bool UsesModelBounds() override { return this->m_animState == 1; }             // 0x04
        void SetAnimState(int32_t state) override;                                      // 0xb0
        virtual bool CanUseDoorNow(int32_t* error, float* range);                       // 0xb4
};

// Quest givers (0x00a33cf0).
class CGGameObjectQuestGiver : public CGGameObjectAnimated {
    public:
        // ref: FUN_007125c0
        CGGameObjectQuestGiver(CGGameObject_C* owner) : CGGameObjectAnimated(owner) { this->m_useRange = 5.5555553f; }

        int32_t GetCursor() override;                                                   // 0x14
        void OnWindowOpened() override;                                                 // 0x98
        void OnWindowClosed() override;                                                 // 0x9c
};

// Generic objects (0x00a33da8): never used, highlighted when their template says so.
class CGGameObjectGeneric : public CGGameObjectAnimated {
    public:
        // ref: FUN_007125f0
        CGGameObjectGeneric(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        bool CanHighlight() override;                                                   // 0x0c
        bool CanUse() override { return false; }                                        // 0x18
};

// Texts (0x00a33f18): a page, read open and shut.
class CGGameObjectText : public CGGameObjectAnimated {
    public:
        // ref: FUN_007126d0
        CGGameObjectText(CGGameObject_C* owner) : CGGameObjectAnimated(owner) { this->m_useRange = 5.5555553f; }

        int32_t GetCursor() override;                                                   // 0x14
        int32_t Use() override;                                                         // 0x24
        void OnStatsLoaded() override;                                                  // 0x78
        // ref: FUN_0070b560
        void OnWindowOpened() override { this->SetAnimState(2); }                       // 0x98
        // ref: FUN_0070b570
        void OnWindowClosed() override { this->SetAnimState(4); }                       // 0x9c
};

// Fishing nodes (0x00a33fd0): only the one who cast it may use it, from far off.
class CGGameObjectFishingNode : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712700
        CGGameObjectFishingNode(CGGameObject_C* owner) : CGGameObjectAnimated(owner) { this->m_useRange = 100.0f; }

        bool CanUse() override;                                                         // 0x18
};

// Summoning rituals (0x00a34088).
class CGGameObjectRitual : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712730
        CGGameObjectRitual(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        bool CanUseNow(int32_t* error, float* range, const char** spellName) override;  // 0x1c
};

// Mailboxes (0x00a34140).
class CGGameObjectMailbox : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712750
        CGGameObjectMailbox(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        int32_t GetCursor() override;                                                   // 0x14
        int32_t Use() override;                                                         // 0x24
        void OnWindowOpened() override;                                                 // 0x98
};

// Meeting stones (0x00a341f8).
class CGGameObjectMeetingStone : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712790
        CGGameObjectMeetingStone(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        int32_t Use() override;                                                         // 0x24
        void Virtual0A0(int32_t a2) override;                                           // 0xa0

        bool IsLevelInRange(uint32_t level);
};

// Capture points (0x00a342b0).
class CGGameObjectCapturePoint : public CGGameObjectAnimated {
    public:
        // ref: FUN_007127e0
        CGGameObjectCapturePoint(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        bool CanHighlight() override;                                                   // 0x0c
        bool CanUse() override { return false; }                                        // 0x18
        void OnStatsLoaded() override;                                                  // 0x78
        void OnReenable() override { this->OnStatsLoaded(); }                           // 0x7c
        void OnDisable() override;                                                      // 0x80
};

// Spell focuses (0x00a34368), and duel arbiters, fishing holes and aura generators with them:
// highlighted, never used.
class CGGameObjectSpellFocus : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712800
        CGGameObjectSpellFocus(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        bool CanHighlight() override { return true; }                                   // 0x0c
        bool CanUse() override { return false; }                                        // 0x18
};

// Guild banks (0x00a34588).
class CGGameObjectGuildBank : public CGGameObjectAnimated {
    public:
        // ref: FUN_00712ac0
        CGGameObjectGuildBank(CGGameObject_C* owner) : CGGameObjectAnimated(owner) {}

        int32_t GetCursor() override;                                                   // 0x14
        int32_t Use() override;                                                         // 0x24
        void OnWindowOpened() override;                                                 // 0x98
};

// Chairs (0x00a33848, 0x4c bytes): up to five seats, the nearest of which the player sits in.
class CGGameObjectChair : public CGGameObjectType {
    public:
        // ref: FUN_0070c6a0
        CGGameObjectChair(CGGameObject_C* owner) : CGGameObjectType(owner, 3.0f) {}

        bool CanUse() override;                                                         // 0x18
        bool CanUseNow(int32_t* error, float* range, const char** spellName) override;  // 0x1c
        void OnStatsLoaded() override;                                                  // 0x78
        virtual int32_t GetSeatCount();                                                 // 0xb0

        // The seats, in the object's own space (+0x10).
        C3Vector m_seats[5] = {};
};

// Barber chairs (0x00a33900): one seat, and not while shapeshifted.
class CGGameObjectBarberChair : public CGGameObjectChair {
    public:
        // ref: FUN_0070c7f0
        CGGameObjectBarberChair(CGGameObject_C* owner) : CGGameObjectChair(owner) {}

        bool CanUseNow(int32_t* error, float* range, const char** spellName) override;  // 0x1c
        int32_t GetSeatCount() override { return 1; }                                   // 0xb0
};

// A game object that is a map object (type 14, 0x00a33790): it places its display, a building or a
// prop, through the world's dynamic objects instead of drawing a model of its own. The map object
// types below build on it; each adds the creation and destruction slots 0xb0 and 0xb4.
class CGGameObjectMapObject : public CGGameObjectType {
    public:
        CGGameObjectMapObject(CGGameObject_C* owner) : CGGameObjectType(owner, 5.0f) {}
        ~CGGameObjectMapObject() override;

        bool UsesModelBounds() override { return false; }                               // 0x04
        bool CanHighlight() override { return false; }                                  // 0x0c
        bool CanUse() override { return false; }                                        // 0x18
        void* GetMapObject() override { return this->m_mapObject; }                     // 0x70
        // ref: FUN_0070b270
        void OnStatsLoaded() override { this->CreateMapObject(); }                       // 0x78
        // ref: FUN_0070b280
        void OnDisable() override { this->DestroyMapObject(); }                          // 0x80
        void OnPostReenable() override { this->CreateMapObject(); }                      // 0x84
        virtual void CreateMapObject();                                                  // 0xb0
        virtual void DestroyMapObject();                                                 // 0xb4

        // +0x10: the building or prop the world placed for the object.
        CMapBaseObj* m_mapObject = nullptr;
};

// The moving map objects' common base (0x00a33e60): they carry passengers and keep their own place
// (+0x20) apart from the object's resting one.
class CGGameObjectTransportBase : public CGGameObjectMapObject {
    public:
        CGGameObjectTransportBase(CGGameObject_C* owner);
        ~CGGameObjectTransportBase() override;

        C3Vector GetPosition() override { return this->m_position; }                     // 0x54
        C3Vector GetRawPosition() override { return this->m_position; }                  // 0x58
        // ref: FUN_007126c0
        float GetScaleMultiplier() override { return this->m_speed; }                    // 0x64
        void UpdatePassenger(CPassenger* passenger, int32_t mode) override;              // 0x68
        int32_t Virtual06C(const C3Vector* position) override;                           // 0x6c
        void OnStatsLoaded() override;                                                   // 0x78
        // ref: FUN_0070c540
        void OnReenable() override { this->m_animState = -1; this->m_arrived = 0; }      // 0x7c
        void OnDisable() override;                                                       // 0x80
        void OnPostReenable() override;                                                  // 0x84
        // ref: FUN_00959d00
        int32_t Virtual0A8() override { return static_cast<int32_t>(this->m_adjustedTime); } // 0xa8

        float StepTo(uint32_t elapsed, const C3Vector& to, C3Vector* direction = nullptr);
        void MovePassengers(int32_t* cameraRides);

        // +0x14: the passengers riding it, threaded through CPassenger::m_transportLink.
        STORM_EXPLICIT_LIST(CPassenger, m_transportLink) m_passengers;
        C3Vector m_position = {};           // +0x20
        float m_speed = 0.0f;               // +0x2c, yards per second along its path
        uint32_t m_time = 0;                // +0x30, the path time it last stepped to
        int32_t m_arrived = 0;              // +0x34, its map object has loaded
        int32_t m_animState = -1;           // +0x38, the state the path was last set for
        uint32_t m_adjustedTime = 0;        // +0x3c, the path time (AdjustTime) of the last step
};

// An elevator or other keyed transport (type 11, 0x00a34808): TransportAnimation keys move it from
// its place and TransportRotation keys turn it, on a loop or, with a level set, between two stops
// the object's state chooses.
class CGGameObjectTransport : public CGGameObjectTransportBase {
    public:
        CGGameObjectTransport(CGGameObject_C* owner);

        void OnStateChanged(int32_t from, int32_t to) override;                         // 0x30
        void OnStatsLoaded() override;                                                   // 0x78
        void OnReenable() override;                                                      // 0x7c
        void OnPostReenable() override;                                                  // 0x84
        void UpdateTransport(uint32_t time, int32_t elapsed) override;                   // 0x8c
        uint32_t AdjustTime(uint32_t time) override;                                     // 0xa4

        void Initialize();
        void SetPathStart(uint32_t time);
        uint32_t PathDuration(int32_t state);
        uint32_t AdjustTime(int32_t state, uint32_t time);
        float PathProgress(uint32_t time, int32_t state);
        uint32_t ResetPath(uint32_t time);
        void Evaluate(uint32_t time, C3Vector* offset, C4Quaternion* rotation);
        C3Vector EvaluatePosition(uint32_t time);
        C4Quaternion EvaluateRotation(uint32_t time);
        C44Matrix* UpdateWorldMatrix();

        uint32_t m_animFirst = 0;           // +0x40, the first TransportAnimation row
        uint32_t m_animCount = 0;           // +0x44
        uint32_t m_animKey = 0;             // +0x48, the key it is between
        uint32_t m_rotFirst = 0;            // +0x4c, the first TransportRotation row
        uint32_t m_rotCount = 0;            // +0x50
        uint32_t m_rotKey = 0;              // +0x54
        uint32_t m_sequence = 0x1FA;        // +0x58, the sequence its key plays
        uint32_t m_pathStart = 0;           // +0x5c, when the path began for the current state
        uint32_t m_pathOffset = 0;          // +0x60, how far into the path a reversal started
};

// A ship or zeppelin (type 15, 0x00a348c0): it follows its TaxiPath (ShipPath, +0x40) at the
// object's speed and acceleration, rocked by its TransportPhysics row, playing the start, moving and
// stopping sequences as it goes. +0x38 holds the sequence playing (-1 before the first step).
class CGGameObjectMOTransport : public CGGameObjectTransportBase {
    public:
        // ref: FUN_007141d0
        CGGameObjectMOTransport(CGGameObject_C* owner) : CGGameObjectTransportBase(owner) {}

        void OnStateChanged(int32_t from, int32_t to) override;                         // 0x30
        void OnStatsLoaded() override;                                                   // 0x78
        void OnPostReenable() override;                                                  // 0x84
        // ref: FUN_00714230
        void* GetTransportAnimation() override { return &this->m_path; }                // 0x88
        void UpdateTransport(uint32_t time, int32_t elapsed) override;                   // 0x8c
        // ref: FUN_00714240
        uint32_t AdjustTime(uint32_t time) override { return this->m_path.GetPathTime(time); } // 0xa4

        // ref: FUN_007100d0
        // The path brought to the server's word: the progress round it, and whether it is to stop
        // (state 1) or has stopped (dynamic flag 0x10); then a step to now.
        void SyncPath();

        ShipPath m_path;                    // +0x40
        uint32_t m_leg = 0;                 // +0x80, the leg the last step was on
};

// The sound a GameObjectDisplayInfo record carries in slot `index`, played at `position` --
// following the object through `sound` when the kit says it loops (FUN_0070bf70).
void GameObjectPlayDisplaySound(int32_t displayID, int32_t index, const C3Vector* position, SOUNDKITOBJECT* sound);

// What a game object's authored model event does: '$GO0'..'$GO5' and '$GC0'..'$GC3' play the
// display's sounds, '$SHK' shakes the camera, '$DSL' loops a sound kit and '$DSO'/'$SND' play one.
void GameObjectHandleAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position,
                               SOUNDKITOBJECT* sound, int32_t displayID);

#endif
