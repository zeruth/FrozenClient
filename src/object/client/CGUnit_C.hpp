#ifndef OBJECT_CLIENT_CG_UNIT_C_HPP
#define OBJECT_CLIENT_CG_UNIT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject_C.hpp"
#include "net/Types.hpp"
#include "object/client/CGUnit.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/Types.hpp"
#include "util/GUID.hpp"
#include <tempest/Box.hpp>

class CCharacterComponent;
class ChrClassesRec;
class ChrRacesRec;
class CreatureDisplayInfoRec;
class CreatureDisplayInfoExtraRec;
class CreatureModelDataRec;
class CreatureSoundDataRec;
class UnitBloodLevelsRec;

class FactionTemplateRec;
class CVehicle_C;
class CVehiclePassenger_C;
class VehicleRec;
class VehicleSeatRec;
struct M2BoneSequenceState;

class CGUnit_C : public CGObject_C, public CGUnit {
    public:
        // Public static variables
        static WOWGUID s_activeMover;

        // Public static functions
        static const char* GetDisplayClassNameFromRecord(const ChrClassesRec* classRec, UNIT_SEX sex, UNIT_SEX* displaySex);
        static const char* GetDisplayRaceNameFromRecord(const ChrRacesRec* raceRec, UNIT_SEX sex, UNIT_SEX* displaySex);

        // How the faction template on the left regards the one on the right, on the client's
        // internal 1..4 scale: 1 hostile, 3 neutral, 4 friendly. The Lua side reports this plus one.
        static int32_t GetFactionTemplateReaction(const FactionTemplateRec* a, const FactionTemplateRec* b);

        // How this unit regards another.
        int32_t GetReaction(const CGUnit_C* other) const;

        // Virtual public member functions
        virtual ~CGUnit_C();
        // TODO
        virtual C3Vector GetPosition() const;
        // TODO
        virtual float GetFacing() const;
        virtual float GetRawFacing() const;
        // TODO
        virtual WOWGUID GetTransportGUID() const;
        // TODO
        virtual int32_t GetModelFileName(const char*& name) const;
        // TODO
        virtual int32_t CanHighlight();
        virtual int32_t CanBeTargetted();
        // TODO

        // Public member functions
        CGUnit_C(uint32_t time, CClientObjCreate& objCreate);
        int32_t GetDisplayID() const;
        // ref: FUN_004d43c0
        int32_t IsActiveMover() const;
        CreatureModelDataRec* GetModelData() const;
        float GetModelScale() const;
        float GetRawSmoothFacing() const;
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void PostMovementUpdate(const CClientMoveUpdate& move, int32_t activeMover);
        void SetStorage(uint32_t* storage, uint32_t* saved);

        // Build a character component for a humanoid NPC from its CreatureDisplayInfoExtra (race,
        // sex, skin/face/hair and equipment), the same way a player model is dressed. Returns true
        // when the display uses extended (character) data; false for ordinary creature models.
        bool BuildNpcCharacterComponent();

        // Re-evaluate the looping idle animation from the unit's current state (dead / stand state /
        // emote) and apply it to the model only when it changes, so a unit that sits, stands, dies
        // or starts an emote after spawn updates its pose instead of freezing on its spawn-time one.
        void UpdateIdleAnimation();

        // Horizontal half-extent (yards, unscaled) of the unit's CURRENT animation bounds, or 0
        // when the model has no per-sequence bounds. The reference sizes a blob shadow from the
        // animated box rather than the model's global one, so a crouching or rearing creature
        // casts the right footprint.
        float GetAnimFootprint() const;
        CAaBox& GetShadowBox(CAaBox& box) const;

        // Duration (ms) of the model's animation with the given AnimationData id, or 0 if it has none.
        uint32_t GetSequenceDuration(int32_t animID);

        // ref: FUN_0071af70
        // The unit's current shapeshift form, byte 3 of UNIT_FIELD_BYTES_2. Zero is no form. The
        // reference reads this through an accessor rather than inline, from a dozen places, which
        // is why it is one here too rather than a shift at each call site.
        uint8_t GetShapeshiftForm() const;

        // Creature type ("Beast", "Undead", ...) as an index into CreatureType.dbc, or 0.
        int32_t GetCreatureType() const;

        // Hunter pet family as an index into CreatureFamily.dbc, or 0 for anything that is not a
        // creature with one.
        int32_t GetCreatureFamily() const;

        // 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare, 5 trivial.
        int32_t GetClassification() const;

        // The creature's title -- "Innkeeper", "Stable Master" -- or null when it has none.
        const char* GetSubName() const;
        // Single bits of the creature template's type flags (the template the reference keeps at
        // CGUnit_C +0x964), and the skinning profession the flags select: 1 herbalism (0x100),
        // 2 mining (0x200), 3 engineering (0x8000), 0 plain skinning.
        uint32_t GetCreatureTypeFlag11() const;
        uint32_t GetCreatureTypeFlag12() const;
        uint32_t GetCreatureTypeFlag26() const;
        int32_t GetCreatureSkinningType() const;
        void PlayEmote(uint32_t emoteID);

        // ref: FUN_00716710
        bool IsPlayerControlled() const;
        // ref: FUN_00716fa0
        bool IsMovingAtWalkPace() const;
        // ref: FUN_00718a90
        uint32_t GetCreatureTypeFlag10() const;
        // ref: FUN_00718b70
        CGUnit_C* GetControllingPlayer();
        // ref: FUN_0071b770
        int32_t GetBasePower(int32_t powerType) const;
        // ref: FUN_0071b810
        uint32_t CanFly() const;
        // ref: FUN_0071c500
        bool IsOtherPlayerWithFlaggedModel() const;
        // ref: FUN_0071c570
        bool IsSpellClickable() const;
        // ref: FUN_0071c8b0
        bool IsMovingFasterThanWalkPace() const;
        // ref: FUN_00721ca0
        bool IsInNonStanceForm() const;
        // Sets bit 21 of the object's flag word (CGObject_C::m_flag21).
        void SetFlag21();
        // Bit 0 of the vehicle state byte.
        uint8_t HasMoveFlags2Bit0() const;

        // The animation layer: what the unit is playing now, and how an AnimationData id asked for
        // becomes one the model actually carries.

        // ref: FUN_00717260
        // The AnimationData id the model is playing: the upper-body bone's when the unit has one
        // and it is playing something, else the whole model's. -1 when there is no loaded model.
        uint32_t GetCurrentAnimationId() const;

        // ref: FUN_007176f0
        // `animID` turned into an id `model` (or this unit's own model) can play. Tries the id at
        // the unit's animation tier, then walks AnimationData's fallback chain, then drops a tier
        // and starts over; falls back to the tier's default animation. Returns `animID` unchanged
        // while the object is not yet posted or has no loaded model.
        uint32_t ResolveAnimation(uint32_t animID, CM2Model* model);

        // ref: FUN_00717600
        // The first AnimationData row that is a tier's base behaviour (behaviour 0 at that tier)
        // and that this unit's model carries. False when none does.
        bool GetDefaultAnimationForTier(int32_t tier, int32_t* out) const;

        // ref: FUN_007173f0
        // The three bone sequences a unit animates through, in the order the reference reads them:
        // `mount` from the mount model's root bone, `body` and `upper` from this unit's model at
        // the root and at the upper-body bone. A slot with nothing playing comes back as animation
        // -1 at speed 1, unless `keepFinished` -- which the mount slot never gets, so a finished
        // mount sequence is always cleared.
        void GetBoneSequenceStates(M2BoneSequenceState* mount, M2BoneSequenceState* body,
                                   M2BoneSequenceState* upper, int32_t keepFinished) const;

        // The three ways a sequence reaches a model. Each takes the model to change because the
        // caller may be driving this unit's own model, its mount's, or a passenger's.

        // ref: FUN_00735820
        // Start `animID` on `boneId`. A sequence already held at its end is released instead of
        // restarted; bone 0x1a is put back on its idle rather than released. `fromPassenger` marks
        // the recursive call a vehicle makes on its riders.
        void SetBoneSequence(CM2Model* model, uint32_t boneId, uint32_t animID, uint32_t variation,
                             uint32_t time, float speed, int32_t a8, int32_t a9,
                             int32_t fromPassenger);

        // ref: FUN_00735a60
        // Release whatever `boneId` is playing, and say whether there was anything to release.
        bool UnsetBoneSequence(CM2Model* model, uint32_t boneId, int32_t a4, int32_t a5,
                               int32_t fromPassenger);

        // ref: FUN_00735cc0
        // Re-time the sequence on `boneId` without restarting it.
        void SetBoneSequenceSpeed(CM2Model* model, uint32_t boneId, float speed,
                                  int32_t fromPassenger);

        // The vehicle layer, as far as the animation code needs it.

        // ref: FUN_004f6210
        // The Vehicle.dbc row of the vehicle this unit IS, or null when it is not one (or is one
        // whose row never arrived).
        const VehicleRec* GetVehicleRec() const;

        // ref: FUN_004f6250
        // The unit is aboard a vehicle, past entering and not yet leaving.
        bool IsRidingVehicle() const;

        // ref: FUN_005140c0
        // The seat this unit is riding in, or null when it is not riding.
        const VehicleSeatRec* GetVehicleSeatRec() const;

        // ref: FUN_0074bb60
        // Called on the VEHICLE: which of the seat's two "animate the rider while leaving" bits
        // applies, chosen by the vehicle's own move-flags-2 bit 0x40. Zero when neither is set.
        uint32_t SeatAllowsExitAnimation(const VehicleSeatRec* seat) const;

        // ref: FUN_0071af90
        // The unit is swinging at something.
        bool IsAttacking() const;

        // ref: FUN_0071afb0
        // IsAttacking, or the unit's pet is in combat (UNIT_FIELD_FLAGS 0x800).
        bool IsAttackingOrPetInCombat() const;

        // ref: FUN_0071afe0
        // Clears bit 0x4000 on every effect playing on the unit.
        void ClearEffectFlag4000();

        // ref: FUN_0071f560
        // The unit is dead, feigning, in the dead pose, or running an effect whose visual kit plays
        // a death animation. The animation selector refuses anything but a death animation for a
        // unit in this state.
        bool IsDeadOrFeigning() const;

        // ref: FUN_0071e400
        // Lets a spell visual override the sequences the selector just worked out: the first effect
        // still running that asks to drive the animation replaces each of the three with its visual
        // kit's, resolved against the model it plays on. False when no effect wants to.
        bool ApplyEffectAnimation(const M2BoneSequenceState* mount, const M2BoneSequenceState* body,
                                  const M2BoneSequenceState* upper, int32_t hasUpper,
                                  M2BoneSequenceState* mountOut, M2BoneSequenceState* bodyOut,
                                  M2BoneSequenceState* upperOut, int32_t* bodyKeepVariation,
                                  int32_t* upperKeepVariation);

        // ref: FUN_00737ef0
        // Put one of the three bone sequences a unit animates through onto its model: fix up the
        // variation the state carries, note what class of animation is now playing in m_animFlags,
        // and then either re-time the sequence the bone already holds or start the new one.
        // `targetAnimID` is the animation the selector is heading for, which decides whether the
        // variation is reset; `skipInfoCheck` skips the variation-against-sequence check.
        void ApplySequence(M2BoneSequenceState* state, uint32_t currentAnimID, int32_t upperBody,
                           int32_t targetAnimID, int32_t a8, int32_t skipInfoCheck);

        // ref: FUN_0071df30
        // Turns a bone sequence that resolved to a grounded idle -- 0 Stand, 8 StandWound or 25
        // ReadyUnarmed -- into 193 Hover, and says whether it did. The animation selector calls
        // this on each sequence it is about to apply to a unit that is hovering or unsupported, so
        // it does not stand in mid-air.
        bool ReplaceIdleWithHover(M2BoneSequenceState* state) const;

    protected:
        // Protected member functions
        int32_t GetLocalDisplayID() const;
        void RefreshDataPointers();

    private:
        // Private member variables
        // TODO
        CMovement_C m_localMove;
        // TODO
        CreatureDisplayInfoRec* m_displayInfo = nullptr;
        CreatureDisplayInfoExtraRec* m_displayInfoExtra = nullptr;
        CreatureModelDataRec* m_modelData = nullptr;
        CreatureSoundDataRec* m_soundData = nullptr;
        // TODO
        UnitBloodLevelsRec* m_bloodRec = nullptr;
        // TODO
        CCharacterComponent* m_characterComponent = nullptr; // composited body for humanoid NPCs
        int32_t m_animSeq = -1;         // last idle sequence applied by UpdateIdleAnimation (-1 = none yet)
        // A one-shot emote from SMSG_EMOTE. Unlike UNIT_NPC_EMOTESTATE, which is a looping pose the
        // unit holds, this plays once and then the unit returns to whatever pose it was in -- it is
        // how a scripted creature does a gesture, e.g. the Lich King planting his sword.
        int32_t m_emoteSeq = 0;         // AnimationData id currently playing, 0 = none
        uint32_t m_emoteEndMs = 0;      // scene clock time the one-shot finishes
        bool m_wasDead = false;         // dead state last update, to detect the moment of death
        uint32_t m_deathStartTime = 0;  // scene time (ms) the Death fall began
        uint32_t m_deathDuration = 0;   // duration (ms) of the model's Death animation
        int32_t m_localDisplayID = 0;
        // The mount the unit is riding, as its own model with the unit's model attached under it
        // (the reference builds it in FUN_0073c0c0 from UNIT_FIELD_MOUNTDISPLAYID). Its root bone
        // carries the mount's animation, which is why GetBoneSequenceStates reads it first.
        CM2Model* m_mountModel = nullptr;
        // Byte 3 of UNIT_FIELD_BYTES_1: 0 ground, 1 swim, 2 hover, 3 fly. ResolveAnimation asks
        // AnimationData for the tiered variant of an animation before the plain one, so a flying
        // unit gets the flying walk. The reference refreshes it from the descriptor in its
        // SMSG_MOVE_SET_CAN_FLY-family handler (FUN_007167c0), which is not ported yet, so this
        // still reads 0 (ground) for every unit.
        int32_t m_animTier = 0;
        // The bone the unit's upper body animates on, or -1 when its whole model animates as one.
        // Set with the unit's model; -1 is the reference's "no split" sentinel.
        uint32_t m_upperBodyBoneId = 0xFFFFFFFF;
        // What the unit is animating, as the reference's +0xa38 bit set. Written when a sequence is
        // applied (CGUnit_C::ApplySequence) and read to decide what may interrupt it. The bits
        // recovered so far, each named by the animation behaviour that raises it: 0x4 jump (39),
        // 0x8 behaviour 127, 0x40000 lift-off and hover (192, 193), 0x400000 behaviour 201,
        // 0x2000000 behaviours 458 to 460, 0x4000000 an airborne death at the fly tier, 0x80000
        // behaviour 121. 0x8000000 suppresses the blend on every sequence the unit sets.
        uint32_t m_animFlags = 0;
        // Who the unit is swinging at (reference +0xa20), zero when it is not attacking. The
        // reference has a one-line accessor for "is attacking" over this pair of GUIDs alone
        // (FUN_0071af90, 17 call sites).
        WOWGUID m_attackTarget = 0;
        // The unit's second state word (reference +0xa30). Only one of its bits is read by the code
        // ported so far: 0x80000, which has to be set for a sequence to keep the blend flag its
        // caller asked for. The rest are written from a dozen places in Unit_C.cpp and are not
        // identified yet.
        uint32_t m_stateFlags = 0;
        // The vehicle this unit IS, when it is one (reference +0xf5c). Null for everything else --
        // and always, for now, since nothing creates one.
        CVehicle_C* m_vehicle = nullptr;
        // The unit's ride, while it is aboard a vehicle (reference +0xf60). Null means not riding,
        // which is always, until something creates one.
        CVehiclePassenger_C* m_vehiclePassenger = nullptr;
        // TODO
        float m_smoothFacing;
        // TODO
};

int32_t ReceiveEmote(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// Opcode classifiers for the unit's movement messages, named by the sets they test.

// ref: FUN_00990370
// The movement acknowledgements: teleport, the force-speed and force-rate acks, root and unroot,
// knockback, hover, feather fall, water walk, can-fly and swim/fly transition, and three more.
bool IsMovementAckOpcode(int32_t opcode);

// ref: FUN_00990420
// IsMovementAckOpcode's set plus CMSG_MOVE_NOT_ACTIVE_MOVER (0x2d1).
bool IsMovementAckOrNotActiveMoverOpcode(int32_t opcode);

// ref: FUN_009904e0
// Swim start and stop, the collision and swim cheats, CMSG_MOVE_SET_FLY and the two controlled
// vehicle opcodes.
bool IsMovementStateOpcode(int32_t opcode);

// ref: FUN_007151f0
// The speed and rate acks, knockback, can-fly, feather fall, water walk, swim/fly transition and
// 0x517: the acknowledgements that carry one more value after the movement block.
bool IsMovementAckWithValueOpcode(int32_t opcode);

// ref: FUN_00714ce0
// Folds bits 0x4, 0x8, 0x10 and 0x100 of `src` into `dst` as 0x2, 0x4, 0x8 and 0x20.
void ConvertAnimationFlags(uint32_t src, uint32_t* dst);

// ref: FUN_00714c80
// A weapon (item class 2) of a two-handed subclass: axe, mace, polearm, sword, staff, exotic,
// spear, fishing pole. `weapon` is the class byte followed by the subclass byte.
bool IsTwoHandedWeapon(const uint8_t* weapon);

// ref: FUN_00714e80
// The walk, run, shuffle, backpedal, jump and swim animations, and five more by id.
bool IsMovementAnimation(uint32_t animID);

// ref: FUN_00715d00
// The sheath state a unit may take with two items: melee with no first item and no second (or a
// holdable, inventory type 0x17) becomes unarmed; unarmed with a weapon (item class 2) in either
// or a shield (inventory type 0xe) second becomes melee. Each item is its class byte, with the
// inventory type at +4.
int32_t AdjustSheathState(int32_t state, const uint8_t* first, const uint8_t* second);

// ref: FUN_007152b0
// One packed spline offset from a movement message: 11, 11 and 10 signed bits in quarter yards,
// taken away from `base`.
void ReadPackedMovementOffset(CDataStore* msg, const C3Vector& base, C3Vector& out);

// ref: FUN_00717540
// The AnimationData row standing in for `animID` at behaviour tier `tier`: the row itself when it
// is its own tier-0 behaviour or already a tiered one, else the first row with that behaviour and
// tier. The reference passes animID in ESI.
bool ResolveAnimationBehavior(int32_t animID, int32_t tier, int32_t* out);

// ref: FUN_007176b0
// AnimationData behaviour id, 0x1fa when the row is missing.
int32_t GetAnimationBehavior(int32_t animID);

// ref: FUN_00718b30
// Faction.dbc: the faction has a reputation index.
bool FactionHasReputation(int32_t factionID);

// ref: FUN_0071c050
// Scale of the native race model a display dresses (display -> extra -> race -> male/female
// display), or 1. The reference passes the display record in EAX.
float GetNativeRaceModelScale(const CreatureDisplayInfoRec* display);

// Predicates over an animation's AnimationData behaviour id. The reference passes the id in EAX
// for the ones without a stack argument; names follow the ids each tests.

// ref: FUN_0071d2a0
bool IsJumpLandAnimation(int32_t animID);
// ref: FUN_0071d2e0
bool IsRangedAttackAnimation(int32_t animID);
// ref: FUN_0071d380
bool IsSpellCastAnimation(int32_t animID);
// ref: FUN_0071d410
bool IsReadySpellAnimation(int32_t animID);
// ref: FUN_0071d450
bool IsUnarmedAnimation(int32_t animID);
// ref: FUN_0071d550
bool IsSpecialAttackAnimation(int32_t animID);
// ref: FUN_0071d590
bool IsCombatAnimation(int32_t animID);
// ref: FUN_0071d6b0
bool IsBaseStateAnimation(int32_t animID);
// ref: FUN_0071d7c0
bool IsJumpAnimation(int32_t animID);
// ref: FUN_0071d800
bool IsActionAnimation(int32_t animID);
// ref: FUN_0071d940
bool IsEmoteAnimation(int32_t animID);
// ref: FUN_0071da20
bool IsThrownAnimation(int32_t animID);
// ref: FUN_0071da60
bool IsBowAnimation(int32_t animID);
// ref: FUN_0071daa0
bool IsRifleAnimation(int32_t animID);
// ref: FUN_0071dae0
bool IsReadyAnimation(int32_t animID);
// ref: FUN_0071db20
bool IsAnimationBehavior127Or201To202(int32_t animID);
// ref: FUN_0071db70
bool IsAnimationBehavior466To468Or472(int32_t animID);
// ref: FUN_0071dbc0
bool IsAnimationBehavior6Or132Or467To468Or472(int32_t animID);
// ref: FUN_0071dc20
bool IsAnimationBehavior37To40Or467(int32_t animID);
// ref: FUN_0071dd80
bool IsAnimationBehavior1Or131Or466To467(int32_t animID);
// ref: FUN_0071dde0
bool IsDeathAnimation(int32_t animID);
// ref: FUN_0071de50
bool IsAnimationBehavior133To134(int32_t animID);
// ref: FUN_0071d510
// 8 StandWound, 9 CombatWound, 10 CombatCritical.
bool IsWoundAnimation(int32_t animID);
// ref: FUN_0071dc70
// IsSpellCastAnimation's set plus 51 ReadySpellDirected and 52 ReadySpellOmni, which is
// IsReadySpellAnimation's set -- the reference tests the behaviour range inline rather than
// calling it, so this does too.
bool IsSpellCastOrReadySpellAnimation(int32_t animID);
// ref: FUN_0071dcc0
// IsSpellCastAnimation's set plus 107, 46 AttackBow and 49 AttackRifle.
bool IsSpellCastOrRangedAttackAnimation(int32_t animID);
// ref: FUN_0071dd30
// IsCombatAnimation's set plus 25 ReadyUnarmed through 29 ReadyBow, which is IsReadyAnimation's
// set, tested inline for the same reason.
bool IsCombatOrReadyAnimation(int32_t animID);

#endif
