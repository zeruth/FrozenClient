#ifndef OBJECT_CLIENT_CG_GAME_OBJECT_C_HPP
#define OBJECT_CLIENT_CG_GAME_OBJECT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGGameObject.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/movement/CPassenger.hpp"
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>

class CGGameObjectType;
class CGUnit_C;
class GameObjectStats_C;
class LockRec;

// GameObject_C.cpp. The vtable is 0x00a34640; the slots it overrides are marked with theirs. Most
// of them hand on to the type behaviour at +0x1a0 (GameObjectTypes.hpp), which the constructor
// picks by the object's GAMEOBJECT_TYPE_*.
class CGGameObject_C : public CGObject_C, public CGGameObject {
    public:
        // Virtual public member functions
        virtual ~CGGameObject_C();
        void Disable() override;                                                // 0x004
        void Reenable() override;                                               // 0x008
        void PostReenable() override;                                           // 0x00c
        void UpdateWorldObject(int32_t noRelink) override;                      // 0x014
        C3Vector GetHeadPosition() const override;                              // 0x020
        C3Vector GetPosition() const override;                                  // 0x02c
        C3Vector GetRawPosition() const override;                               // 0x030
        float GetFacing() const override;                                       // 0x034
        float GetRawFacing() const override;                                    // 0x038
        WOWGUID GetTransportGUID() const override;                              // 0x040
        C4Quaternion GetRotation() const override;                              // 0x044
        int32_t CanHaveQuestStatus() override;                                  // 0x04c
        void OnReenable() override;                                             // 0x050
        void UpdateQuestMarker() override;                                      // 0x054
        void AttachQuestMarker() override;                                      // 0x058
        int32_t GetModelFileName(const char*& name) const override;             // 0x060
        void OnModelLoaded(CM2Model* model) override;                           // 0x080
        int32_t PlaceModel(float elapsed) override;                             // 0x08c
        void GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) override; // 0x090
        int32_t Virtual09C() override;                                          // 0x09c
        int32_t Virtual0A0(int32_t a2) override;                                // 0x0a0
        int32_t CanHighlight() override;                                        // 0x0a4
        int32_t Virtual0AC() override;                                          // 0x0ac
        void Virtual0B0() override;                                             // 0x0b0
        int32_t Virtual0B4() override;                                          // 0x0b4
        void GetWorldMatrix(C44Matrix& matrix) const override;                  // 0x0c4
        const char* GetName() override;                                         // 0x0d8
        int32_t Virtual0DC(int32_t a2) override;                                // 0x0dc
        int32_t Virtual0E4() override;                                          // 0x0e4
        float GetFadeInAlpha() override;                                        // 0x0e8
        bool Virtual0EC() override;                                             // 0x0ec
        int32_t Virtual0F0(int32_t a2) override;                                // 0x0f0
        int32_t Virtual0F4(CPassenger* passenger, int32_t mode) override;       // 0x0f4
        float Virtual0F8() override;                                            // 0x0f8

        // Public static functions
        // The object's GameObjectStats_C record has arrived from the server.
        static void StatsCallback(uint32_t id, const WOWGUID* guid, void* param, bool found);
        static void AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId,
                                      uint32_t eventData, const C3Vector* position, uint32_t a6,
                                      WOWGUID owner);
        static void SequenceDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId,
                                         int32_t a4, int32_t a5, WOWGUID owner);

        // Public member functions
        CGGameObject_C(uint32_t time, CClientObjCreate& objCreate);
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);

        // Each frame, from the visible-object walk.
        void UpdateForFrame(uint32_t time);
        void OnStatsLoaded(const GameObjectStats_C* stats);
        // The sparkle over an object worth looking at (GO_DYNFLAG_LO_SPARKLE, or one the player
        // tracks) comes and goes.
        void UpdateHighlightModel();
        void AttachHighlightModel();
        // The art kit's textures and models onto `model`.
        void ApplyArtKit(CM2Model* model);
        // GAMEOBJECT_BYTES_1's state has changed: the type hears it.
        void UpdateState();
        // Play custom animation `index` (0..3) now, or once the model has loaded.
        void SetPendingCustomAnim(int32_t index);
        void SetAnimProgress(float progress);
        // The display's model, or null (with a complaint) when the display is missing.
        const char* GetDisplayModelName() const;
        int32_t GetData(int32_t meaning) const;
        const LockRec* GetLockRec() const;
        // Whether a lock entry's action may be performed in the object's state.
        bool IsLockActionAllowed(int32_t action) const;
        // Whether the lock keeps the active player out. When it does not, `spell` is the spell
        // that opens it (0 for none); the skill and its level are reported where they apply.
        bool CheckLock(int32_t* spell, int32_t* skillValue, int32_t* skillNeeded, int32_t* item, uint32_t* entry);
        // How the object regards `unit`: through its maker when a unit made it, otherwise its
        // faction's; 3 (neutral) with neither.
        int32_t GetReaction(CGUnit_C* unit);
        bool IsUsableInCombat() const;
        bool IsUsableUnderFlag31() const;
        bool IsUsableMounted() const;

        // Public member variables
        // +0xd8: where the object is, and on what transport.
        CPassenger m_passenger;
        // TODO +0x108: twelve lists the destructor empties (FUN_005bd800), and the link at +0x198
        // (FUN_0079f820 in Disable); what joins them is not identified.
        CGGameObjectType* m_type = nullptr;                 // +0x1a0
        const GameObjectStats_C* m_stats = nullptr;         // +0x1a4
        C44Matrix m_worldMatrix;                            // +0x1a8
        CAaBox m_worldBox = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } }; // +0x1e8
        // +0x200: the server's time less the client's, for the transports' paths.
        int32_t m_timeOffset = 0;
        int32_t m_state = 0;                                // +0x204
        // +0x208: an index into the custom animation states, played once the model loads.
        int32_t m_pendingAnim = 0;
        // +0x20c: whether the object collides (its box is not degenerate; a door only while shut).
        int32_t m_collidable = 0;
        CM2Model* m_highlightModel = nullptr;               // +0x210
        int32_t m_field214 = 0;                             // +0x214
};

// The game object field and message handlers.
void GameObjectInitialize();

void GameObjectShutdown();

#endif
