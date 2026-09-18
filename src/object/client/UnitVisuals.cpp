#include "object/client/UnitVisuals.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "object/client/AuraCache.hpp"
#include "object/client/CGUnit_C.hpp"
#include <storm/String.hpp>
#include <cstdio>
#include <map>
#include <vector>

namespace {

// M2 attachment ids the kit's effect slots land on. These are the model attachment ids (the
// numbering the M2 attachment table uses), not GEOCOMPONENTLINKS -- the two agree for the low
// slots but the spell points sit above the equipment ones.
enum {
    ATTACHMENT_HAND_RIGHT   = 1,
    ATTACHMENT_HAND_LEFT    = 2,
    ATTACHMENT_BREATH       = 17,
    ATTACHMENT_BASE         = 19,
    ATTACHMENT_HEAD         = 20,
    ATTACHMENT_SPELL_LEFT   = 21,
    ATTACHMENT_SPELL_RIGHT  = 22,
    ATTACHMENT_SPECIAL_1    = 23,
    ATTACHMENT_SPECIAL_2    = 24,
    ATTACHMENT_SPECIAL_3    = 25,
    ATTACHMENT_CHEST        = 34,
};

struct Effect {
    int32_t spellID;
    int32_t effectNameID;
    uint32_t attachment;
    CM2Model* model;
};

struct UnitState {
    // What the aura set looked like when the effects were last built, so a frame with no change
    // costs one comparison per aura and nothing else.
    std::vector<int32_t> auraSpells;
    std::vector<Effect> effects;
};

std::map<WOWGUID, UnitState> s_units;

// Every (effect name, attachment) pair a kit asks for.
void KitEffects(const SpellVisualKitRec* kit, std::vector<std::pair<int32_t, uint32_t>>& out) {
    if (!kit) {
        return;
    }

    const std::pair<int32_t, uint32_t> slots[] = {
        { kit->m_headEffect,        ATTACHMENT_HEAD },
        { kit->m_chestEffect,       ATTACHMENT_CHEST },
        { kit->m_baseEffect,        ATTACHMENT_BASE },
        { kit->m_leftHandEffect,    ATTACHMENT_SPELL_LEFT },
        { kit->m_rightHandEffect,   ATTACHMENT_SPELL_RIGHT },
        { kit->m_breathEffect,      ATTACHMENT_BREATH },
        { kit->m_leftWeaponEffect,  ATTACHMENT_HAND_LEFT },
        { kit->m_rightWeaponEffect, ATTACHMENT_HAND_RIGHT },
        { kit->m_specialEffect[0],  ATTACHMENT_SPECIAL_1 },
        { kit->m_specialEffect[1],  ATTACHMENT_SPECIAL_2 },
        { kit->m_specialEffect[2],  ATTACHMENT_SPECIAL_3 },
    };

    for (const auto& slot : slots) {
        if (slot.first > 0) {
            out.push_back(slot);
        }
    }
}

// The data ships .mdx paths; the models are .m2 files.
void ModelPath(const char* fileName, char* out, size_t outSize) {
    SStrCopy(out, fileName, static_cast<int32_t>(outSize));
    size_t len = SStrLen(out);

    if (len > 4 && !SStrCmpI(out + len - 4, ".mdx", 4)) {
        SStrCopy(out + len - 4, ".m2", static_cast<int32_t>(outSize - (len - 4)));
    }
}

CM2Model* Attach(CGUnit_C* unit, const SpellVisualEffectNameRec* effect, uint32_t attachment) {
    CM2Model* parent = unit->m_model;

    if (!parent || !parent->m_scene || !effect->m_fileName || !*effect->m_fileName) {
        return nullptr;
    }

    char path[260];
    ModelPath(effect->m_fileName, path, sizeof(path));

    CM2Model* model = parent->m_scene->CreateModel(path, 0);

    if (!model) {
        return nullptr;
    }

    // The unit's model already animates and draws every frame; an attached child is animated
    // and drawn with it (CM2Model::AnimateST walks m_attachList), so nothing else to schedule.
    model->AttachToParent(parent, attachment, nullptr, 0);

    // A loaded parent without that attachment point refuses the attach (attachIndex 0xFFFF); a
    // parent still loading accepts it and resolves the index when its data lands. Keeping an
    // unattached model would leave it drawing at the origin and force a rebuild every frame.
    if (model->m_attachParent != parent) {
        model->Release();
        return nullptr;
    }

    return model;
}

void ReleaseEffects(UnitState& state) {
    for (auto& e : state.effects) {
        if (e.model) {
            e.model->DetachFromParent();
            e.model->Release();
        }
    }

    state.effects.clear();
}

} // namespace

void UnitVisualsUpdate(CGUnit_C* unit) {
    if (!unit || !unit->m_model) {
        return;
    }

    WOWGUID guid = unit->GetGUID();

    // The aura set as the cache holds it now.
    std::vector<int32_t> spells;
    int32_t count = AuraCacheCount(guid, 0, 0);

    for (int32_t i = 0; i < count; i++) {
        const ClientAura* aura = AuraCacheGet(guid, i, 0, 0);

        if (aura && aura->spellID) {
            spells.push_back(aura->spellID);
        }
    }

    auto it = s_units.find(guid);

    if (it == s_units.end()) {
        if (spells.empty()) {
            return;
        }

        it = s_units.emplace(guid, UnitState()).first;
    }

    UnitState& state = it->second;

    // Unchanged aura set and the model it was built against still attached: nothing to do.
    if (state.auraSpells == spells) {
        bool intact = true;

        for (auto& e : state.effects) {
            if (e.model && e.model->m_attachParent != unit->m_model) {
                intact = false;
                break;
            }
        }

        if (intact) {
            return;
        }
    }

    // Rebuild from scratch: aura sets change rarely and are short.
    ReleaseEffects(state);
    state.auraSpells = spells;

    for (int32_t spellID : spells) {
        auto spell = g_spellDB.GetRecord(spellID);

        if (!spell) {
            continue;
        }

        for (int32_t v = 0; v < 2; v++) {
            auto visual = spell->m_spellVisualID[v] > 0 ? g_spellVisualDB.GetRecord(spell->m_spellVisualID[v]) : nullptr;

            if (!visual || visual->m_stateKit <= 0) {
                continue;
            }

            std::vector<std::pair<int32_t, uint32_t>> wanted;
            KitEffects(g_spellVisualKitDB.GetRecord(visual->m_stateKit), wanted);

            for (const auto& w : wanted) {
                auto effect = g_spellVisualEffectNameDB.GetRecord(w.first);

                if (!effect) {
                    continue;
                }

                CM2Model* model = Attach(unit, effect, w.second);

                if (model) {
                    state.effects.push_back({ spellID, w.first, w.second, model });
                }
            }
        }
    }

    if (state.effects.empty() && state.auraSpells.empty()) {
        s_units.erase(it);
    }
}

void UnitVisualsRelease(WOWGUID guid) {
    auto it = s_units.find(guid);

    if (it != s_units.end()) {
        ReleaseEffects(it->second);
        s_units.erase(it);
    }
}

void UnitVisualsReleaseAll() {
    for (auto& entry : s_units) {
        ReleaseEffects(entry.second);
    }

    s_units.clear();
}
