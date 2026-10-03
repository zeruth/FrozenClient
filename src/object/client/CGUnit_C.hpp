#ifndef OBJECT_CLIENT_CG_UNIT_C_HPP
#define OBJECT_CLIENT_CG_UNIT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject_C.hpp"
#include "net/Types.hpp"
#include "object/client/CGUnit.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/Types.hpp"
#include "util/GUID.hpp"
#include <storm/Array.hpp>
#include <tempest/Box.hpp>

class CCharacterComponent;
class CEffect;
class CGPlayer_C;
class ItemDisplayInfoRec;
class SpellRec;
class CDataStore;
class SOUNDKITOBJECT;
class MountTransitionObject;
class ChrClassesRec;
class ChrRacesRec;
class CreatureDisplayInfoRec;
class CreatureDisplayInfoExtraRec;
class CreatureModelDataRec;
class CreatureSoundDataRec;
class CreatureStats_C;
class UnitBloodLevelsRec;

class FactionTemplateRec;
class CGGameObject_C;
class CVehicle_C;
class CVehiclePassenger_C;
class VehicleRec;
class VehicleSeatRec;
struct M2BoneSequenceState;

// One aura slot on a unit, as SMSG_AURA_UPDATE describes it (0x18 bytes in the reference, read
// by CAuraState::Read, FUN_00716510).
struct CAuraState {
    WOWGUID m_caster = 0;           // +0x00
    int32_t m_spellID = 0;          // +0x08, 0 for an empty slot
    uint8_t m_flags = 0;            // +0x0c: 0x1..0x4 the effects applied, 0x8 self-cast, 0x20 timed, 0x80 negative
    uint8_t m_level = 0;            // +0x0d
    uint8_t m_stacks = 0;           // +0x0e
    uint8_t m_pad0f = 0;
    int32_t m_maxDuration = 0;      // +0x10, ms
    uint32_t m_expireTime = 0;      // +0x14, the async clock when it runs out; 0 when it does not

    // ref: FUN_00716510
    void Read(const CGUnit_C* unit, uint32_t now, CDataStore* msg);
};

// What the model code knows of the item a hand holds (8 bytes a hand, the unit's at +0x9a4 and a
// player's at +0x1908): from Item.dbc for a creature's virtual items, from the item cache for a
// player's visible ones.
struct UNIT_WEAPON_INFO {
    uint8_t m_class = 0;            // +0x00
    uint8_t m_subclass = 0;         // +0x01
    uint8_t m_soundOverride = 0;    // +0x02
    uint8_t m_material = 0;         // +0x03
    uint8_t m_inventoryType = 0;    // +0x04: 0xe shield, 0x19 thrown, 0x1a ranged-right
    uint8_t m_sheathType = 0;       // +0x05
    uint8_t m_pad[2] = {};
};

// A pair the reference keeps per aura slot, twice (+0xdd8 and +0xe5c), initialised to
// {0x100, 0}. Its readers are elsewhere in Unit_C.cpp and not yet traced.
struct CAuraSlotState {
    uint32_t m_value = 0x100;
    uint32_t m_extra = 0;
};

class CGUnit_C : public CGObject_C, public CGUnit {
    // Vehicle_C.cpp reads the owner unit's animation state inline (m_animFlags and the pending flag
    // at +0xfa4), so CVehicle_C reaches them directly here rather than through accessors the
    // reference does not have.
    friend class CVehicle_C;
    friend class CGCamera;

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
        // ref: FUN_007370d0
        virtual void UpdateWorldObject(int32_t noRelink);
        // TODO
        virtual int32_t GetModelFileName(const char*& name) const;
        // TODO
        virtual int32_t CanHighlight();
        virtual int32_t CanBeTargetted();
        // ref: FUN_006e6f80
        // The model the world draws for the unit: its mount when it rides one, the unit's own
        // model otherwise.
        virtual CM2Model* GetObjectModel();
        // ref: FUN_0073e410
        // Rebuild the unit's model from its display (`force`, or when the display has changed):
        // the creature skin, its scale, its pose, its aura and channel visuals, and its mount.
        virtual void UpdateModel(int32_t force);
        // ref: FUN_0073e840
        virtual void OnModelLoaded(CM2Model* model);
        // ref: FUN_00730f30
        // The unit waits hidden until its character component is built and its textures are in.
        virtual void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther);
        // ref: FUN_00715b50
        // The display's opacity (CreatureDisplayInfo column 5) over 255, or fully opaque.
        virtual float GetFadeInAlpha();

        // The unit's own slots, past the base's, in the reference's order (0x00a34d90).
        virtual void Virtual108() {}                                                    // 0x108
        virtual void Virtual10C() {}                                                    // 0x10c
        virtual void Virtual110() {}                                                    // 0x110
        virtual void Virtual114() {}                                                    // 0x114
        // ref: FUN_00747310
        // One of the creature's own sounds (CreatureSoundData), by kind; unless `force`, only as
        // often as the kind's chance allows (0x00adb5d8).
        virtual void PlayUnitSound(int32_t kind, int32_t force);                        // 0x118
        // ref: FUN_007464d0
        // The armour foley of the unit's model, when Sound_EnableArmorFoleySoundForOthers is on.
        virtual void PlayArmorFoley();                                                  // 0x11c
        // ref: FUN_007463e0
        // The impact sound family its sound data names (0x00a37170), 0 for none.
        virtual int32_t GetImpactSoundType();                                           // 0x120
        virtual int32_t Virtual124() { return 0; }                                      // 0x124
        // ref: FUN_00716490
        virtual bool IsDead() const;                                                    // 0x128
        // ref: FUN_0071f440
        // The item `hand` holds (0 main, 1 off, 2 ranged), or null when it holds nothing or, unless
        // `ignoreHidden`, the hand is hidden (FUN_00718fc0).
        virtual const UNIT_WEAPON_INFO* GetWeaponInfo(int32_t hand, int32_t ignoreHidden); // 0x12c
        // ref: FUN_0071f540
        virtual const ItemDisplayInfoRec* GetWeaponDisplay(int32_t hand);               // 0x130
        // ref: FUN_00718b10
        virtual int32_t GetWeaponDisplayID(int32_t hand);                               // 0x134
        // ref: FUN_0071a380
        virtual uint8_t GetStandStateByte() const;                                      // 0x138
        // ref: FUN_0071aa70
        // The skill the unit casts `spell` at: five per level, capped at five per the spell's own
        // maximum level.
        virtual int32_t GetSpellSkill(const SpellRec* spell);                           // 0x13c
        // ref: FUN_00734f70
        virtual void GetDefenseSkill(int32_t* skill, int32_t* bonus);                   // 0x140
        // ref: FUN_00734fa0
        virtual void GetWeaponSkill(int32_t attack, int32_t* skill, int32_t* bonus);    // 0x144
        // ref: FUN_0071ad20
        // How long the unit's cast of `spell` takes: its SpellCastTimes row at the unit's skill,
        // scaled by its casting speed.
        virtual int32_t GetSpellCastTime(const SpellRec* spell);                        // 0x148
        // ref: FUN_006e6fc0
        virtual float GetMovementPitch() const;                                         // 0x14c
        // ref: FUN_0071c0e0
        // The display's scale times the object's, and the mount's while the unit rides.
        virtual float GetScale() const;
        // ref: FUN_0071fd80
        // Put the unit's model (its mount's, when it rides) where the unit is, leaning with the
        // ground it stands on.
        virtual int32_t PlaceModel(float elapsed);
        // TODO

        // Public member functions
        CGUnit_C(uint32_t time, CClientObjCreate& objCreate);
        int32_t GetDisplayID() const;
        // ref: FUN_004d43c0
        int32_t IsActiveMover() const;
        CreatureModelDataRec* GetModelData() const;

        // ref: FUN_0072a000
        // The unit's name: a player's from the name cache (with its realm in `realm` when it has
        // one), a pet's from the pet name cache, a creature's from its template. Each cache that
        // has not answered yet is asked, and the unit hears back through OnNameArrived. Never null:
        // a name that has not arrived reads as UNKNOWNOBJECT.
        const char* GetUnitName(const char** realm, int32_t checkPossess);

        // ref: FUN_00728ca0
        // The name cache's callback for a unit's name.
        static void OnNameArrived(uint32_t id, const WOWGUID* guid, void* param, bool found);

        // ref: FUN_0072cde0
        // The creature cache's callback for a unit's template, registered by RefreshDataPointers.
        static void OnCreatureStatsArrived(uint32_t id, const WOWGUID* guid, void* param, bool found);

        // +0x964: the unit's creature template, once the creature cache has it. Null for players
        // and for a creature whose record has not arrived.
        const CreatureStats_C* m_creatureStats = nullptr;
        float GetModelScale() const;
        float GetRawSmoothFacing() const;
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void PostMovementUpdate(const CClientObjCreate& init, int32_t activeMover);
        void SetStorage(uint32_t* storage, uint32_t* saved);



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

        // ref: FUN_0074b8b0
        // The unit rides another unit: its transport guid is a vehicle's, or a player's.
        bool IsTransportUnit() const;

        // ref: FUN_00717e50
        // The unit's smoothed facing in world space: its own plus that of every unit it rides,
        // turned by the facing of the transport at the bottom of the chain.
        float GetWorldSmoothFacing();

        // ref: FUN_00716190
        // Stand state 1 Sit, or 4 through 6 (the chair heights).
        bool IsSittingStandState() const;

        // ref: FUN_00722180
        // Something other than the unit drives its pose: the vehicle it is, the creature
        // template's no-animation bit (type flags 0x8), or animation 0x79 already playing.
        bool IsAnimationLocked();

        // ref: FUN_00736640
        // The unit flinches: StandWound (8), CombatWound (9) or, when `critical`, CombatCritical
        // (10), on its upper body unless that is playing a ready pose. Nothing plays while the
        // unit is dead, locked or running an effect that holds its animation (flag 0x4).
        void PlayWoundAnimation(int32_t critical);

        // ref: FUN_00735bb0
        // Re-time `model`'s sequence on `boneId`, and when it is this vehicle's own model, the
        // sequence of every live passenger with it.
        void SetBoneSequenceTimeOnPassengers(CM2Model* model, uint32_t boneId, int32_t time, int32_t fromPassenger);

        // ref: FUN_00735dd0
        // Freeze (`hold`) or release `model`'s animation, and its live passengers' with it.
        void SetAnimationHoldOnPassengers(CM2Model* model, int32_t hold, int32_t fromPassenger);

        // ref: FUN_007202c0
        // The unit draws its body through a character component: a player whose display is a
        // character model, a unit that has reverted from a form, or one wearing its own native
        // display.
        bool DrawsThroughComponent() const;

        // ref: FUN_00723730
        // Put the unit's own item back in a component section an item visual had taken.
        void ReapplyItemSection(int32_t section);

        // The weapon layer.

        // ref: FUN_00718fc0
        // The hand is hidden: the main hand while the unit is disarmed (flags 0x200000) and holds a
        // two-hander in it, the off hand likewise or while disarmed in it (flags 2 0x80); never
        // the ranged.
        int32_t IsHandHidden(int32_t hand);

        // ref: FUN_00725010
        // A creature's virtual item in `hand` changed from `oldEntry`: the hand's display and item
        // record follow, the old item comes off and the new one goes on.
        void UpdateVirtualItem(int32_t hand, int32_t oldEntry);

        // ref: FUN_0072dbc0
        // Put the item `hand` holds where the sheath state wants it: in the hand, or in its sheath.
        void AttachHandItem(int32_t hand);

        // ref: FUN_0072b7f0
        // The ranged weapon, in the hand (`inHand`) or its sheath; `heldRight` says whether it is a
        // gun or a thrown weapon, which is held in the right hand.
        int32_t AttachRangedWeapon(int32_t inHand, uint8_t* heldRight);

        // ref: FUN_0072afe0
        void ReleaseRangedWeapon();

        // ref: FUN_007310a0
        // Move the item in `hand` between the hand and its sheath (`toSheath`).
        int32_t MoveHandItem(int32_t hand, int32_t toSheath);

        // ref: FUN_00721ed0
        // The unit fights unarmed: its upper body plays an unarmed animation, or a combat one with
        // nothing in the main hand.
        bool IsFightingUnarmed();

        // ref: FUN_00736d30
        // Sheathe or draw: 0 sheathed, 1 melee, 2 ranged. `animate` plays the draw (the weapons move
        // at once otherwise); `fromServer` skips telling the server.
        void SetSheathState(int32_t state, int32_t animate, int32_t fromServer);

        // ref: FUN_00731f40
        void AttachWeaponsForSheath();

        // ref: FUN_00736b60 / FUN_007367b0 / FUN_007368b0 / FUN_007369b0
        void PlaySheathAnimation();
        int32_t PlayMainHandSheath();
        int32_t PlayOffHandSheath();
        void PlayDrawAnimation();

        // The model layer.

        // ref: FUN_0072a480
        // The display the unit should wear is not the one its model is.
        bool NeedsModelUpdate();

        // ref: FUN_007179d0
        void SetUnitModel(CM2Model* model);

        // ref: FUN_00722ae0 / FUN_0072cbb0
        // The display's scale, and the unit eased to it.
        float GetDisplayScale(int32_t displayID);
        void UpdateDisplayScale(int32_t keepScale);

        // ref: FUN_00728e70
        // Every aura's visual again, after the model changed under them.
        void ReplayAuraVisuals();

        // ref: FUN_0072bc70
        // The visual of the spell the unit is channelling.
        void UpdateChannelVisual();

        // ref: FUN_00717310
        // How far through its current animation the unit is, 0..1.
        float GetAnimationProgress();

        // ref: FUN_00730100
        // Dress the unit's model: its character component for a character display, then its items
        // and the weapons in its hands. 0 while the model is not in.
        int32_t BuildComponent();

        // ref: FUN_0071d010
        // The character component, from the display's extra row or, for `player`, the player's
        // appearance; `raceSexFromExtra` takes race and sex from the extra row.
        int32_t InitComponent(CGPlayer_C* player, int32_t raceSexFromExtra);

        // ref: FUN_0071a430
        // A player wearing a character display (model data flag 4, extra flag 1).
        bool IsCharacterDisplayPlayer() const;

        // ref: FUN_00716e20
        // The item visuals among the unit's effects retake their sections.
        void ApplyItemVisualEffects();

        // ref: FUN_00716f10
        // Ask the server for a mirror image's appearance (CMSG_GET_MIRRORIMAGE_DATA).
        void RequestMirrorImageData();

        // ref: FUN_00715670 / FUN_00715690
        // The mount transition the unit is playing, and the effect that plays it.
        void SetMountTransition(MountTransitionObject* transition, CEffect* effect);
        void ClearMountTransition();

        // ref: FUN_007412b0
        // A finished mount transition hands the unit the mount it was leading to.
        void ReleaseMountTransition();

        // ref: FUN_00715d90 / FUN_00715db0
        // Creature template type flags 0x400000 and 0x2000000, which pick the unit's world flags.
        uint32_t GetCreatureTypeFlag22() const;
        uint32_t GetCreatureTypeFlag25() const;

        // ref: FUN_00717ad0
        // How tall the unit's model data says it is (its geometry box), scaled.
        float GetModelHeight();

        // ref: FUN_0071a7f0
        // The model carries the attachment a spell visual names: the attachment itself when
        // `worldAttach`, else the spell attachment table's entry for it (0x00adaa20).
        int32_t HasSpellAttachment(int32_t attachment, int32_t worldAttach);

        // ref: FUN_0071a860
        // Where a spell visual's attachment is, with `offset` in its space.
        C3Vector& GetSpellAttachmentWorldPosition(C3Vector& out, int32_t attachment, const C3Vector& offset,
                                                  int32_t worldAttach);

        // ref: FUN_00746bd0
        // An attachment's world position with `offset` in its space; a model without it falls
        // back on the unit's position, raised by the attachment's default height (0x00adb630).
        C3Vector& GetAttachmentPosition(C3Vector& out, uint32_t attachment, const C3Vector* offset);

        // The mount layer.

        // ref: FUN_00740450
        // Ride `displayID` (0 to dismount): the old mount comes off, the new one is built and the
        // unit sits on it, the mount sound and name plate follow.
        void SetMountDisplay(int32_t displayID);

        // ref: FUN_0073d5d0
        // Build the mount model for m_mountDisplayID and seat the unit's model on it.
        void Mount(int32_t displayID, int32_t checkCollision);

        // ref: FUN_0073d940
        // Take the unit's model off its mount and drop the mount model.
        void Dismount(int32_t restoreCollision);

        // ref: FUN_00717910
        void SetMountModel(CM2Model* model);

        // ref: FUN_007195d0
        // The CreatureSoundData row of the mount, from its display or its model data.
        const CreatureSoundDataRec* GetMountSoundData() const;

        // ref: FUN_00720330
        // The blob shadow's radius from the unit's shadow box, softened past 1.5 yards.
        void UpdateShadowRadius();

        // ref: FUN_00715fd0
        // Which of bones 0x1b..0x22 the unit's model carries.
        void UpdateBoneMask();

        // ref: FUN_007467f0
        // The mount's looping sound plays while the unit rides and is not in flight.
        void UpdateMountSound();

        // ref: FUN_007470d0
        void PlayDismountSound();

        // ref: FUN_007412e0
        // The active player asks the server to dismount (CMSG_CANCEL_MOUNT_AURA) and comes off now.
        void RequestDismount();

        // ref: FUN_0071e5b0
        // The unit's ObjectEffect package follows the three sequences it is playing.
        void UpdateObjectEffects();

        // The aura layer.

        // ref: FUN_0072f5d0
        // An aura update for the unit: every slot when `all` (SMSG_AURA_UPDATE_ALL), else the
        // slots the message names. Auras that went are removed and auras that came are applied,
        // each with its visuals, then UNIT_AURA is queued.
        void UpdateAuras(CDataStore* msg, int32_t all);

        // ref: FUN_0072f360
        // Grow the aura arrays to hold `slot`.
        void GrowAuras(uint32_t slot);

        // ref: FUN_00727e70
        // Which aura types the unit carries, one bit per type over every applied effect.
        void RebuildAuraTypeMask();

        // ref: FUN_005a1120
        bool HasAuraType(uint32_t auraType) const;

        // ref: FUN_00556e10
        // The aura in `slot`, or null past the end.
        const CAuraState* GetAura(uint32_t slot) const;

        // ref: FUN_00727760 / FUN_00722090
        // An aura was applied to or removed from `slot`: its visual, the minimap tracking spell
        // and the shapeshift bar follow.
        void OnAuraApplied(uint32_t slot, int32_t negative, const CAuraState* aura, int32_t spellID);
        void OnAuraRemoved(uint32_t slot, int32_t negative, const CAuraState* aura, int32_t spellID);

        // ref: FUN_00724820
        // The aura in `slot` shows: its spell's state kit plays on the unit, and its effects that
        // concern the active player take hold (first-person look, screen effect, mount pose).
        void AddAuraVisual(uint32_t slot, const SpellRec* spell);

        // ref: FUN_0071e930
        // The aura in `slot` stops showing: its effects stop and its state-done kit plays.
        void RemoveAuraVisual(uint32_t slot, const SpellRec* spell);

        // ref: FUN_00720400
        // The auras whose visual is hidden while the unit is sheathed and idle (SpellVisual flag
        // 0x8) show or stop as that changes.
        void UpdateSheathedAuraVisuals(int32_t show, int32_t force);

        // ref: FUN_0071ab80
        // Release the transparency effects a spell gave the unit.
        void RemoveAlphaEffects(int32_t spellID);

        // ref: FUN_0071abe0
        // The unit fades to its display's alpha times the strongest transparency effect's.
        void ApplyAlphaEffects();

        // ref: FUN_007178e0
        // Release the colour effects a spell gave the unit.
        void RemoveColorEffects(int32_t spellID);

        // ref: FUN_0071a3f0
        // The CreatureSoundData row the unit's sounds come from: its mount's while it rides, a
        // pet's own pet row when its sound data names one, else its own.
        const CreatureSoundDataRec* GetSoundData() const;

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
        int32_t IsAnimationRooting() const;

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

        // The animation chooser. UpdateAnimation is the entry point the rest of the client calls when
        // a unit's state changes; ChooseAnimation runs the candidate chain below and SetAnimation
        // applies what it picked. `allow` is a mask of which classes of change the caller accepts --
        // a candidate whose bit is clear still stops the chain, but writes nothing, which pins the
        // unit to what it is already playing.

        // ref: FUN_0073c140
        // The handler a unit registers on its model: the sequence on one of its bones has ended.
        // Static, because the model calls it with the owner's GUID rather than a pointer.
        static void OnModelSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animID, int32_t interrupted,
                                        int32_t overshoot, WOWGUID owner);
        static void OnSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animID, int32_t a4,
                                   int32_t interrupted, WOWGUID owner);

        // ref: FUN_0073bff0
        // What OnSequenceDone does once it has the unit: clear the "playing X" bit the finished
        // animation set, and -- when it ran to its end rather than being cut off -- hand the unit
        // back its 0x10/0x20/0x40 permissions so the chooser may pick what follows.
        void OnAnimationFinished(CM2Model* model, uint32_t boneId, int32_t animID,
                                 int32_t interrupted);

        // ref: FUN_0073ac30
        void UpdateAnimation(uint32_t setFlags, uint32_t allow);

        // ref: FUN_00724500
        int32_t ChooseAnimation(uint32_t allow, uint32_t* decidedFlags, uint8_t* kitFlags);

        // The candidates, in the order the chain runs them.
        // ref: FUN_00724060
        bool GetDeathAnimation(uint32_t allow, int32_t* out, uint32_t* decidedFlags);
        // ref: FUN_00716fd0
        bool GetStandState10Animation(uint32_t allow, int32_t* out);
        // ref: FUN_0071dff0
        bool GetStandState9Animation(uint32_t allow, int32_t* out);
        // ref: FUN_00724200
        bool GetForcedMotionAnimation(int32_t* out);
        // ref: FUN_00717050
        bool GetMovementAnimation(uint32_t allow, int32_t* out);
        // ref: FUN_00724280
        bool GetLootAnimation(uint32_t allow, int32_t* out);
        // ref: FUN_00724330
        bool GetSpellCastAnimation(uint32_t allow, int32_t* out, const uint8_t* decidedFlags);
        // ref: FUN_0071e0d0
        bool GetCombatAnimation(uint32_t allow, int32_t* out, const uint32_t* decidedFlags);
        // ref: FUN_0071e180
        bool GetTurnAnimation(uint32_t allow, int32_t* out);
        // ref: FUN_00714f90
        bool GetRangedReadyAnimation(uint32_t allow, int32_t* out);
        // ref: FUN_0071e1f0
        bool GetStandStateAnimation(uint32_t allow, int32_t* out);
        // ref: FUN_007171c0
        bool GetEmoteStateAnimation(uint32_t allow, int32_t* out);

        // What the candidates ask about the unit.
        // ref: FUN_007172b0
        // The animation on the model's root bone, ignoring any upper-body split. -1 when unloaded.
        uint32_t GetModelAnimationId() const;
        // ref: FUN_0071b6b0
        bool IsLooting() const;
        // ref: FUN_007222a0
        // The loot the unit has open is something a loot animation suits: not a fishing bobber, not
        // a living unit, not an item.
        bool CanShowLootAnimation() const;
        // ref: FUN_00714dd0
        // The ready pose for whatever the unit is holding: 26 Ready1H, 27 Ready2H, 28 Ready2HL, or
        // 25 ReadyUnarmed.
        int32_t GetReadyWeaponAnimation() const;
        // ref: FUN_007224d0
        // The animation the unit's current cast or channel asks for, out of its SpellVisual's kit,
        // and that kit's flags. False when it is casting nothing.
        bool GetSpellVisualAnimation(int32_t* animOut, uint32_t* kitFlagsOut);

        // ref: FUN_007385c0
        // THE animation selector: turns "play this animation" into up to three bone sequences --
        // the mount's, the unit's body, its upper body -- and applies them. `flags`: 0x1 no blend,
        // 0x2 take the id as given, 0x4 skip the fallback resolve, 0x8 do not re-pick a special
        // attack from what is in hand, 0x20 forwarded to the trail trigger.
        void SetAnimation(uint32_t animID, uint32_t flags);

        // ref: FUN_0071e340
        // The pose a unit holds when it is not doing anything else, written into `out`: 41 SwimIdle
        // while swimming or flying, 120 while creeping, 193 Hover while hovering, 0 Stand otherwise.
        // Leaves `out` alone entirely when the seat is animating the rider or the unit is mid-jump or
        // mid-fall, and keeps whatever is playing when its behaviour is 464 (unless `ignore464`).
        void GetPostureAnimation(uint32_t* out, int32_t ignore464) const;

        // ref: FUN_0071de90
        // The unit may turn in place: it is turning or jumping, is not hovering, swimming or falling
        // slowly, is not having its pose driven by a vehicle, and is not already playing an emote, a
        // cast or a ranged attack.
        bool CanPlayTurnAnimation() const;

        // ref: FUN_0071dfc0
        // The model carries the airborne death animation (behaviour 466, once resolved).
        bool HasAirborneDeathAnimation();

        // ref: FUN_00715880
        // Stand states 0 Stand, 1 Sit, and 4 through 6 (the three chair heights). Notably not 2
        // Sit-chair or 3 Sleep.
        bool IsStandStateUprightOrSeated() const;

        // ref: FUN_00723e30
        // Whether the unit may take an action animation now rather than keep what it is playing.
        // The gate the selector puts in front of every action, emote and combat pose: a seat that
        // animates its rider, a vehicle animating through the rider's bone, and anything dying
        // refuse outright; otherwise it weighs the unit's movement, what it is holding, and whether
        // it is mounted.
        bool CanPlayActionAnimation(int32_t animID, int32_t currentAnimID);

        // ref: FUN_007225e0
        // One of the localised strings that come in a male and a female form, picked by this
        // unit's gender. UNIT_FIELD_BYTES_0 byte 2 is the gender and 1 is female, which lands on
        // GENDER_FEMALE (3) and GENDER_MALE (2) -- the same two values the reference passes.
        //
        // The reference reads the gender off the player info block (+0x1008) when the unit IS the
        // active player and off the descriptor otherwise. frozen reads the descriptor either way:
        // it is the same unit's own gender byte, so the two agree, and frozen has no cached copy
        // to prefer.
        const char* GetGenderedText(const char* key, int32_t count) const;

        // ref: FUN_0071af90
        // The unit is swinging at something.
        bool IsAttacking() const;

        // ref: FUN_0071afb0
        // IsAttacking, or the unit's pet is in combat (UNIT_FIELD_FLAGS 0x800).
        bool IsAttackingOrPetInCombat() const;
        void PlayFollowUpAnimation(CM2Model* model, uint32_t boneId, int32_t animID);
        void UpdateSmoothFacing(const float* seatOffset);
        void PlayBoneFollowUpAnimation(CM2Model* model, uint32_t boneId, int32_t behavior);
        void ShowLootSparkle();
        void ReturnSwingWeapon(const C3Vector* position, int32_t hand, uint32_t drawnBit);
        float GetModelScale(const CreatureDisplayInfoRec* display, const CreatureModelDataRec* modelData);
        float GetCollisionScale(const CreatureDisplayInfoRec* display, const CreatureModelDataRec* modelData,
                                float* modelScale);
        int32_t UpdatePlayerCollision(int32_t skipFit, int32_t force);
        int32_t UpdateMountedCollision(float mountHeight, int32_t skipFit);
        int32_t UpdateCollisionBox(int32_t skipFit, int32_t force);
        void OnScaleChanged(float oldScale);
        void OnLevelChanged();
        void UpdateReaction(int32_t refreshOthers);
        void OnActivePlayerFlagsChanged(uint32_t changed);
        void OnFlagsChanged(uint32_t old);
        void OnFlags2Changed(uint32_t old);
        void OnVisibilityChanged(uint8_t old);
        void OnPvpFlagsChanged(uint8_t old);
        void OnNpcFlagsChanged(uint32_t old);
        void HideLootSparkle();
        void PlayDeathPose(int32_t force);
        void OnResurrect(int32_t silent);
        void OnDeath();
        void OnDynamicFlagsUpdate(uint32_t old);
        void UpdateFishingLine();
        void OnChannelObjectChanged(WOWGUID oldObject, int32_t oldSpell);
        void OnChannelSpellChanged(int32_t oldSpell);
        bool FacesTarget() const;
        WOWGUID GetActiveLootTarget() const;
        void OnModelAnimationFinished(CM2Model* model, uint32_t boneId, int32_t animID, int32_t interrupted);
        int32_t GetCastVisualAnimation() const;

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


        // ---- movement (Unit_C.cpp) --------------------------------------------------------

        // ref: FUN_0072e5d0, FUN_0072e680, FUN_0072e730, FUN_0072e7e0, FUN_0072e900, FUN_0072e9b0
        // The input's movement starts: each cancels click-to-move and closes the loot window for
        // the local player, then queues the event on the movement.
        void StartMove(int32_t time, int32_t forward);
        void StartStrafe(int32_t time, int32_t left);
        void StartAscend(int32_t time, int32_t up);
        void StartTurn(int32_t time, int32_t left);
        void StartPitch(int32_t time, int32_t up);
        void StopPitch(int32_t time);
        // ref: FUN_0071ae10, FUN_0071ae20, FUN_0071ae30, FUN_0071ae40
        void StopMove(int32_t time);
        void StopStrafe(int32_t time);
        void StopAscend(int32_t time);
        void StopTurn(int32_t time);
        // ref: FUN_00718860
        void SetFlying(int32_t time, int32_t fly);
        // ref: FUN_0072eb80
        void Jump(int32_t time);
        // ref: FUN_007272c0
        void CancelClickToMove(int32_t face, int32_t stop);
        void SetFacingTo(int32_t time, float facing);
        void SetPitchTo(int32_t time, float pitch);
        void TurnTo(int32_t time, float facing);
        void PitchTo(int32_t time, float pitch);

        // ref: FUN_007413f0
        // Report a movement change: the packet (deferred for turns and pitches the server lets the
        // client batch), then the animation and input it implies.
        int32_t SendMovement(uint32_t time, int32_t opcode, uint8_t send, float value, uint32_t counter,
                             WOWGUID guid, uint8_t seat);
        // ref: FUN_0071f0c0
        // The movement packet itself: header, status, the extras some opcodes carry, and send.
        int32_t SendMovementStatus(uint32_t time, int32_t opcode, float value, uint32_t counter,
                                   WOWGUID guid, uint8_t seat);
        // ref: FUN_0071ef80
        int32_t WriteMovementHeader(uint32_t time, int32_t opcode, CDataStore& msg, float value, uint32_t counter);
        // ref: FUN_00717d90
        void SendTimeSkipped(uint32_t ms);
        // ref: FUN_0071f210
        void SendSplineDone(uint32_t time, uint32_t id);
        // ref: FUN_00721b90
        // Send a turn or pitch report that has waited long enough.
        void SendDeferredMovement(uint32_t time);
        // ref: FUN_00721c20
        int32_t FlushDeferredMovement(uint32_t time);
        // ref: FUN_007219f0
        int32_t DeferTurn(uint32_t time, int32_t opcode);
        // ref: FUN_00721ac0
        int32_t DeferPitch(uint32_t time, int32_t opcode);
        // ref: FUN_0071ae80
        // The facing (and, swimming or flying, the pitch) is within 0.1 of what was last sent.
        int32_t IsOrientationUnchanged();
        // ref: FUN_00721300
        void UpdateMovementEffects();
        // ref: FUN_0073ed10
        void OnMovementPacketSent(int32_t opcode);
        // ref: FUN_0073ad00
        void UpdateFallAnimation();
        // ref: FUN_0073d2b0
        void PlayLandingAnimation(uint32_t oldFlags, uint32_t jumping);
        // ref: FUN_0073d3d0
        void OnLanded(uint32_t oldFlags, uint32_t jumping);
        // ref: FUN_0073d4a0
        int32_t AcknowledgeLanding(uint32_t time, uint32_t oldFlags, uint16_t oldFlags2, uint32_t jumping);
        // ref: FUN_0073ab20
        void OnMovementStep(uint32_t time, int32_t a2, int32_t a3);
        // The liquid surface under the unit (CWorld::GetObjectFloor on its world object).
        int32_t GetFloorHeight(float* height);

        // ref: FUN_00717c50
        // Make `guid` the unit the input moves, and tell the server.
        static void SetActiveMover(WOWGUID guid);
        // Movement messages (0x00740d30 .. 0x00741c90, 0x007307a0)
        int32_t OnRemoteMoveMessage(int32_t opcode, CDataStore* msg);
        int32_t OnForcedMoveMessage(int32_t opcode, CDataStore* msg);
        int32_t OnSpeedMessage(int32_t opcode, CDataStore* msg);
        int32_t OnSplineSpeedMessage(int32_t opcode, float speed);
        int32_t OnSplineFlagMessage(int32_t opcode);
        int32_t RemoteKnockback(int32_t time, const CMovementStatus& status, CDataStore* msg);
        int32_t RemoteStartTurn(int32_t time, const CMovementStatus& status, int32_t left);
        int32_t RemoteStartPitch(int32_t time, const CMovementStatus& status, int32_t up);
        int32_t RemoteUnroot(int32_t time, const CMovementStatus& status);
        int32_t RemoteTeleport(int32_t time, const CMovementStatus& status);
        void ReceiveKnockback(int32_t time, uint32_t counter, CDataStore* msg);
        void ReceiveTeleportAck(int32_t time, uint32_t counter, CDataStore* msg);
        void OnStandStateUpdate(uint8_t standState);
        void SetReportedPower(int32_t powerType, int32_t value);
        static C3Vector* FitSplineToPosition(WOWGUID transport, const C3Vector& position, C3Vector* points,
                                             uint32_t* count);
        void FaceSplineTarget(WOWGUID target, int32_t flush);
        void SetSplineAnimationTier(uint8_t tier);
        void OnMonsterMove(CDataStore* msg, int32_t opcode, WOWGUID transport, uint8_t seat, int32_t flush);

        // Movement members. +0x948 / +0x94c: when a turn and a pitch report were deferred, +0x950 /
        // +0x954 their opcodes; +0x9bc the last teleport acknowledgement; +0xa50 / +0xa54 the
        // facing and pitch last sent.
        uint32_t m_deferredTurnTime = 0;
        uint32_t m_deferredPitchTime = 0;
        int32_t m_deferredTurnOpcode = 0;
        int32_t m_deferredPitchOpcode = 0;
        uint32_t m_teleportAckTime = 0;
        float m_sentFacing = 0.0f;
        float m_sentPitch = 0.0f;

    protected:
        // Protected member functions
        int32_t GetLocalDisplayID() const;
        void RefreshDataPointers();

    // The reference has no access control; ports elsewhere in the client read these fields
    // directly, the way the original does.
    public:
        // Member variables
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
        CM2Model* m_mountModel = nullptr;           // +0x98c
        // +0x990: the mount's model scale (CreatureDisplayInfo column 4), which the rider is
        // scaled back out of.
        float m_mountScale = 1.0f;
        // +0x978: the mount's CreatureSoundData row (GetMountSoundData).
        const CreatureSoundDataRec* m_mountSoundData = nullptr;
        // +0x9c0: the creature display the unit rides, 0 for none (UNIT_FIELD_MOUNTDISPLAYID).
        int32_t m_mountDisplayID = 0;
        // +0x9c4 / +0x9c8: the mount transition playing, and the effect playing it.
        MountTransitionObject* m_mountTransition = nullptr;
        CEffect* m_mountTransitionEffect = nullptr;
        // +0x9f4: set when the unit reverts from a form to its native display (FUN_00721cf0).
        uint8_t m_formReverted = 0;
        // +0xac4: the mount's looping sound. The reference embeds it; frozen holds it by pointer
        // so the unit header does not carry the sound engine's (FMOD's) headers into every library
        // that includes it. Made with the unit, freed with it.
        SOUNDKITOBJECT* m_mountSound = nullptr;
        // +0xad8..+0xae0: the mount's footprint texture, and its length and width.
        int32_t m_mountFootprintTexture = 0;
        float m_mountFootprintLength = 0.0f;
        float m_mountFootprintWidth = 0.0f;
        // +0x9d8: the axis the model leans along, eased toward the movement's up vector.
        C3Vector m_tiltAxis = { 0.0f, 0.0f, 1.0f };
        // +0xb0c: the blob shadow's radius (UpdateShadowRadius).
        float m_shadowRadius = 0.0f;
        // +0x984: the transparency effects on the unit (char proc 14), and +0xafc the colour
        // effects (char proc 1), linked through CEffect::m_linkNext.
        CEffect* m_alphaEffects = nullptr;
        CEffect* m_colorEffects = nullptr;
        // +0x994: the strongest transparency effect hides the unit's shadow as well.
        uint8_t m_alphaHidesShadow = 0;
        // +0xa8c: a spell whose state kit waits for the model to load (a kit with char proc 11).
        int32_t m_pendingStateKitSpell = 0;
        // +0xc50: the unit's aura slots; +0xdd8 / +0xe5c a pair of per-slot arrays; +0xedc the spell
        // whose visual each slot is showing; +0xf20 one bit per aura type the unit carries. The
        // reference keeps sixteen of each inline and moves to the heap past that.
        TSGrowableArray<CAuraState> m_auras;
        TSGrowableArray<CAuraSlotState> m_auraSlotStates[2];
        TSGrowableArray<int32_t> m_auraVisualSpells;
        uint8_t m_auraTypeMask[40] = {};
        // +0x998: the display of what each hand holds, and +0x9a4 the items themselves, from the
        // unit's virtual items (UpdateVirtualItem).
        int32_t m_weaponDisplays[3] = {};
        UNIT_WEAPON_INFO m_weaponInfo[3];
        // +0xa3c..+0xa4c: the unit's footprint: its texture, length, width and particle scale.
        int32_t m_footprintTexture = 0;
        float m_footprintLength = 0.0f;
        float m_footprintWidth = 0.0f;
        float m_footprintParticleScale = 0.0f;
        // +0xb3c: the scale of the display the unit wears (GetDisplayScale), which GetScale applies.
        float m_displayScale = 1.0f;
        // +0xb40: the ranged weapon's model while it is in the hand; +0xb44 its arrow.
        CM2Model* m_rangedModel = nullptr;
        CM2Model* m_rangedAmmoModel = nullptr;
        // +0xb58: the sheath state before the current one.
        int32_t m_previousSheathState = 0;
        // +0x9e4: when the unit last changed target (UNIT_FIELD_TARGET, FUN_00716900).
        uint32_t m_targetChangeTime = 0;
        // +0xf7c: which of bones 0x1b..0x22 the model carries, and a value per bone (+0xf80).
        uint32_t m_boneMask = 0;
        uint32_t m_boneValues[8] = {};
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
        // The stand state the unit had last time its animation was updated (reference +0x9f8), which
        // is how the chooser tells sitting down from being seated.
        int32_t m_lastStandState = 0;
        // The spell the unit is casting (reference +0xa60), 0 for none. Nothing writes it yet.
        int32_t m_castSpellID = 0;
        // Set by UpdateAnimation to the animation it applied when that matched m_pendingAnimID
        // (reference +0xb90); the chooser consults it before falling back to the unit's posture.
        int32_t m_deferredAnimID = -1;
        // Asked for while the model was still loading (reference +0xb8c); replayed and cleared the
        // next time the selector runs with the model there. -1 is nothing pending.
        int32_t m_pendingAnimID = -1;
        // Asked for and refused because the unit was mid-swing (reference +0xb88); the swing is sped
        // up instead. -1 is nothing held.
        int32_t m_heldAnimID = -1;
        // The animation a rider holds while mounted (reference +0xb7c), applied when the mount model
        // is built; a mount aura's state kit sets it (AddAuraVisual).
        int32_t m_mountedAnimID = 0;
        // The unit's sheath state (reference +0xb5c): 0 sheathed, 1 melee drawn, 2 ranged drawn,
        // and +0xb58 the state before it. The sheath setter (FUN_00736d30) writes both; it was
        // named for an attack cycle until that writer was read.
        int32_t m_sheathState = 0;
        // ref +0xfa4, not identified: the reference tests it against -1 as a "something is pending"
        // flag, alongside m_animFlags 0x400, when deciding whether a vehicle's owner is driving the
        // pose. Nothing ported writes it, so it stays -1 and the term reads false.
        int32_t m_intFA4 = -1;
        // +0xac0: when the unit's death animation first finished (the world clock), 0 before.
        uint32_t m_deathTime = 0;
        // +0xc40 / +0xc48: the unit's master looter and the looter whose turn it is (SMSG_LOOT_LIST).
        WOWGUID m_masterLooter = 0;
        WOWGUID m_roundRobinLooter = 0;
        // +0xfb0: the health SMSG_HEALTH_UPDATE reports; +0xfb4 the powers SMSG_POWER_UPDATE does.
        uint32_t m_reportedHealth = 0;
        int32_t m_reportedPower[7] = {};
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
        // +0xaa4: the smoothing step the turn is taking (0 when it is not smoothing).
        float m_smoothFacingStep = 0.0f;
        // +0xaa8: the last four turns the facing average took, newest first; [0] == 0 starts over.
        float m_smoothFacingHistory[4] = {};
        // +0x980: the line from a fishing rod to its bobber while the unit channels at one.
        CEffect* m_fishingLine = nullptr;
        // +0xab8: the facing a unit with state flag 0x1 holds while standing. Nothing writes it yet.
        float m_heldFacing = 0.0f;
        // +0xabc: when the active player last stood still with nothing steering it.
        uint32_t m_turnStillTime = 0;
        // TODO
};

int32_t ReceiveEmote(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// ref: FUN_007300a0
// SMSG_AURA_UPDATE_ALL (0x495) and SMSG_AURA_UPDATE (0x496): the unit named by the packed guid
// takes the update; one that is not in view drops it.
int32_t ReceiveAuraUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// ref: FUN_00747860
// The unit sound module's start: the FootstepSounds CVar and the dismount sound.
void UnitSoundInitialize();

// ref: FUN_00742220
// The unit module's start for a game: its field handlers and the unit sound start.
// PARTIAL: the message handlers, the unit heap, the threat and combat tables and the six other
// module starts it makes (FUN_008100e0, FUN_00756bd0, FUN_007165d0, FUN_00703b80, FUN_00756e30,
// FUN_0074a070, FUN_00759160, FUN_006fc8e0, FUN_0074b880) are ported with their modules.
void UnitInitialize();

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
