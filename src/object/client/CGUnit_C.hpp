#ifndef OBJECT_CLIENT_CG_UNIT_C_HPP
#define OBJECT_CLIENT_CG_UNIT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/Types.hpp"
#include "util/GUID.hpp"

class CCharacterComponent;
class ChrClassesRec;
class ChrRacesRec;
class CreatureDisplayInfoRec;
class CreatureDisplayInfoExtraRec;
class CreatureModelDataRec;
class CreatureSoundDataRec;
class UnitBloodLevelsRec;

class CGUnit_C : public CGObject_C, public CGUnit {
    public:
        // Public static variables
        static WOWGUID s_activeMover;

        // Public static functions
        static const char* GetDisplayClassNameFromRecord(const ChrClassesRec* classRec, UNIT_SEX sex, UNIT_SEX* displaySex);
        static const char* GetDisplayRaceNameFromRecord(const ChrRacesRec* raceRec, UNIT_SEX sex, UNIT_SEX* displaySex);

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

        // Duration (ms) of the model's animation with the given AnimationData id, or 0 if it has none.
        uint32_t GetSequenceDuration(int32_t animID);

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
        bool m_wasDead = false;         // dead state last update, to detect the moment of death
        uint32_t m_deathStartTime = 0;  // scene time (ms) the Death fall began
        uint32_t m_deathDuration = 0;   // duration (ms) of the model's Death animation
        int32_t m_localDisplayID = 0;
        // TODO
        float m_smoothFacing;
        // TODO
};

#endif
