#ifndef OBJECT_CLIENT_CG_DYNAMIC_OBJECT_C_HPP
#define OBJECT_CLIENT_CG_DYNAMIC_OBJECT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGDynamicObject.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/movement/CPassenger.hpp"

class BlizzardObject;
class SOUNDKITOBJECT;
class SpellRec;
class SpellVisualEffectNameRec;

// DynamicObject_C.cpp: a spell's area on the ground (a Blizzard, a Consecration, a Flare): the
// persistent-area kit's model scaled to the area's radius, its sound, and for a kit that asks for
// it (char proc 9) the falling-shard particles. Vtable 0x00a33020.
class CGDynamicObject_C : public CGObject_C, public CGDynamicObject {
    public:
        // Virtual public member functions
        virtual ~CGDynamicObject_C();
        void Disable() override;                                                // 0x004
        void Reenable() override;                                               // 0x008
        void PostReenable() override;                                           // 0x00c
        void HandleOutOfRange(OUT_OF_RANGE_TYPE type) override;                 // 0x010
        C3Vector GetPosition() const override;                                  // 0x02c
        C3Vector GetRawPosition() const override;                               // 0x030
        float GetFacing() const override;                                       // 0x034
        float GetRawFacing() const override;                                    // 0x038
        float GetBaseScale() const override;                                    // 0x03c
        WOWGUID GetTransportGUID() const override;                              // 0x040
        void SetParticleRelative(const C44Matrix* relative) override;           // 0x048
        int32_t GetModelFileName(const char*& name) const override;             // 0x060
        void OnModelLoaded(CM2Model* model) override;                           // 0x080
        void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) override; // 0x090
        void GetWorldMatrix(C44Matrix& matrix) const override;                  // 0x0c4

        // Public static functions
        static void AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId,
                                      uint32_t eventData, const C3Vector* position, uint32_t a6,
                                      WOWGUID owner);
        static void SequenceDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId,
                                         int32_t a4, int32_t a5, WOWGUID owner);
        static void DespawnDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId,
                                        int32_t a4, int32_t a5, WOWGUID owner);

        // Public member functions
        CGDynamicObject_C(uint32_t time, CClientObjCreate& objCreate);
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);
        void UpdateForFrame(uint32_t time);
        int32_t IsSpellPending() const;
        void PlayIdle();
        void StartAreaVisuals();

        // Public member variables
        CPassenger m_passenger;                         // +0xd8
        // +0x150: 0x1 the model has its spell sequence (0x9e), 0x2 the spell has not gone yet.
        uint32_t m_dynamicFlags = 0;
        float m_areaScale = 1.0f;                       // +0x154
        BlizzardObject* m_blizzard = nullptr;           // +0x158
        // +0x15c: the area's sound. Held by pointer, as the unit's sounds are.
        SOUNDKITOBJECT* m_sound = nullptr;
};

// ref: FUN_00704b30
// The model a spell's area wears: its persistent-area kit's world effect, else its base effect.
const SpellVisualEffectNameRec* DynamicObjectGetEffectName(const SpellRec* spell);

#endif
