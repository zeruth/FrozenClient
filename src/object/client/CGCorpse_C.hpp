#ifndef OBJECT_CLIENT_CG_CORPSE_C_HPP
#define OBJECT_CLIENT_CG_CORPSE_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGCorpse.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/movement/CPassenger.hpp"

class CCharacterComponent;

// Corpse_C.cpp: a player's corpse -- the body where the player died, dressed as it was, or the
// bones a released corpse leaves (CORPSE_FIELD_FLAGS 0x1). Vtable 0x00a331c8.
class CGCorpse_C : public CGObject_C, public CGCorpse {
    public:
        // Virtual public member functions
        virtual ~CGCorpse_C();
        void Disable() override;                                                // 0x004
        void Reenable() override;                                               // 0x008
        void PostReenable() override;                                           // 0x00c
        int32_t ShouldFadeOut() override;                                       // 0x018
        C3Vector GetPosition() const override;                                  // 0x02c
        C3Vector GetRawPosition() const override;                               // 0x030
        float GetFacing() const override;                                       // 0x034
        float GetRawFacing() const override;                                    // 0x038
        WOWGUID GetTransportGUID() const override;                              // 0x040
        int32_t GetModelFileName(const char*& name) const override;             // 0x060
        void Virtual06C() override;                                             // 0x06c
        float GetScale() const override;                                        // 0x07c
        void OnModelLoaded(CM2Model* model) override;                           // 0x080
        int32_t PlaceModel(float elapsed) override;                             // 0x08c
        void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) override; // 0x090
        int32_t CanHighlight() override;                                        // 0x0a4
        void Virtual0B0() override;                                             // 0x0b0

        // Public member functions
        CGCorpse_C(uint32_t time, CClientObjCreate& objCreate);
        const char* GetPortraitTextureName() const;
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);
        float GetDisplayScale() const;
        int32_t IsLootable() const;
        void AttachLootSparkle();
        void ReleaseLootSparkle();
        void RebuildModel();
        void OnDynamicFlagsChanged(uint32_t old);
        void ApplyGuildTabard(bool query);
        C3Vector GetGroundNormal();

        // Public member variables
        CPassenger m_passenger;                         // +0xd8
        CCharacterComponent* m_component = nullptr;     // +0x270
        // +0x274: the ground's normal under the corpse, the average of the facets facing up.
        C3Vector m_groundNormal = { 0.0f, 0.0f, 1.0f };
        // +0x280: 0x1 the ground normal is taken, 0x2 the corpse floats (in water).
        uint32_t m_corpseFlags = 0;
        float m_selectionRadius = 0.0f;                 // +0x284
        CM2Model* m_lootSparkle = nullptr;              // +0x288
};

// ref: FUN_007062c0
// The corpse module's start: its two field handlers.
void CorpseInitialize();

#endif
