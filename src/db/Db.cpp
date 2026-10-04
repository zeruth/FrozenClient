#include "db/Db.hpp"
#include "db/WowClientDB_Base.hpp"
#include "console/Console.hpp"
#include <storm/Array.hpp>

WowClientDB<AchievementRec> g_achievementDB;
WowClientDB<AreaTableRec> g_areaTableDB;
WowClientDB<Cfg_CategoriesRec> g_cfg_CategoriesDB;
WowClientDB<Cfg_ConfigsRec> g_cfg_ConfigsDB;
WowClientDB<CharBaseInfoRec> g_charBaseInfoDB;
WowClientDB<CharHairGeosetsRec> g_charHairGeosetsDB;
WowClientDB<CharSectionsRec> g_charSectionsDB;
WowClientDB<CharStartOutfitRec> g_charStartOutfitDB;
WowClientDB<CharTitlesRec> g_charTitlesDB;
WowClientDB<ChatProfanityRec> g_chatProfanityDB;
WowClientDB<CharacterFacialHairStylesRec> g_characterFacialHairStylesDB;
WowClientDB<ChrClassesRec> g_chrClassesDB;
WowClientDB<ChrRacesRec> g_chrRacesDB;
WowClientDB<CreatureDisplayInfoRec> g_creatureDisplayInfoDB;
WowClientDB<CreatureDisplayInfoExtraRec> g_creatureDisplayInfoExtraDB;
WowClientDB<GameObjectArtKitRec> g_gameObjectArtKitDB;
WowClientDB<GameObjectDisplayInfoRec> g_gameObjectDisplayInfoDB;
WowClientDB<LockRec> g_lockDB;
WowClientDB<LockTypeRec> g_lockTypeDB;
WowClientDB<SpellRec> g_spellDB;
WowClientDB<SpellIconRec> g_spellIconDB;
WowClientDB<CreatureModelDataRec> g_creatureModelDataDB;
WowClientDB<AnimationDataRec> g_animationDataDB;
WowClientDB<EmotesRec> g_emotesDB;
WowClientDB<SkillLineRec> g_skillLineDB;
WowClientDB<SkillLineAbilityRec> g_skillLineAbilityDB;
WowClientDB<SpellVisualRec> g_spellVisualDB;
WowClientDB<SpellVisualKitRec> g_spellVisualKitDB;
WowClientDB<SpellVisualEffectNameRec> g_spellVisualEffectNameDB;
WowClientDB<SpellVisualKitModelAttachRec> g_spellVisualKitModelAttachDB;
WowClientDB<SpellVisualKitAreaModelRec> g_spellVisualKitAreaModelDB;
WowClientDB<SpellEffectCameraShakesRec> g_spellEffectCameraShakesDB;
WowClientDB<SpellCastTimesRec> g_spellCastTimesDB;
WowClientDB<SkillCostsDataRec> g_skillCostsDataDB;
WowClientDB<SkillRaceClassInfoRec> g_skillRaceClassInfoDB;
WowClientDB<SpellDescriptionVariablesRec> g_spellDescriptionVariablesDB;
WowClientDB<ResistancesRec> g_resistancesDB;
WowClientDB<SpellDifficultyRec> g_spellDifficultyDB;
WowClientDB<SpellDurationRec> g_spellDurationDB;
WowClientDB<SpellRadiusRec> g_spellRadiusDB;
WowClientDB<SpellCategoryRec> g_spellCategoryDB;
WowClientDB<ItemSubClassMaskRec> g_itemSubClassMaskDB;
WowClientDB<TotemCategoryRec> g_totemCategoryDB;
WowClientDB<PowerDisplayRec> g_powerDisplayDB;
WowClientDB<SpellRuneCostRec> g_spellRuneCostDB;
WowClientDB<SpellRangeRec> g_spellRangeDB;
WowClientDB<SpellChainEffectsRec> g_spellChainEffectsDB;
WowClientDB<VehicleSeatRec> g_vehicleSeatDB;
WowClientDB<LightRec> g_lightDB;
WowClientDB<LightParamsRec> g_lightParamsDB;
WowClientDB<CameraShakesRec> g_cameraShakesDB;
WowClientDB<WMOAreaTableRec> g_wmoAreaTableDB;
WowClientDB<LightSkyboxRec> g_lightSkyboxDB;
WowClientDB<LiquidMaterialRec> g_liquidMaterialDB;
WowClientDB<LiquidTypeRec> g_liquidTypeDB;
WowClientDB<WeatherRec> g_weatherDB;
WowClientDB<GroundEffectTextureRec> g_groundEffectTextureDB;
WowClientDB<GroundEffectDoodadRec> g_groundEffectDoodadDB;
WowClientDB<LightIntBandRec> g_lightIntBandDB;
WowClientDB<LightFloatBandRec> g_lightFloatBandDB;
WowClientDB<CreatureSoundDataRec> g_creatureSoundDataDB;
WowClientDB<CreatureFamilyRec> g_creatureFamilyDB;
WowClientDB<CreatureTypeRec> g_creatureTypeDB;
WowClientDB<CreatureMovementInfoRec> g_creatureMovementInfoDB;
WowClientDB<MaterialRec> g_materialDB;
WowClientDB<TerrainTypeRec> g_terrainTypeDB;
WowClientDB<FootstepTerrainLookupRec> g_footstepTerrainLookupDB;
WowClientDB<TransportAnimationRec> g_transportAnimationDB;
WowClientDB<TransportRotationRec> g_transportRotationDB;
WowClientDB<TaxiPathNodeRec> g_taxiPathNodeDB;
WowClientDB<TransportPhysicsRec> g_transportPhysicsDB;
WowClientDB<MapDifficultyRec> g_mapDifficultyDB;
WowClientDB<DestructibleModelDataRec> g_destructibleModelDataDB;
WowClientDB<CurrencyTypesRec> g_currencyTypesDB;
WowClientDB<SpellShapeshiftFormRec> g_spellShapeshiftFormDB;
WowClientDB<FactionGroupRec> g_factionGroupDB;
WowClientDB<FactionRec> g_factionDB;
WowClientDB<FactionTemplateRec> g_factionTemplateDB;
WowClientDB<GameTipsRec> g_gameTipsDB;
WowClientDB<GMTicketCategoryRec> g_gmTicketCategoryDB;
WowClientDB<ItemClassRec> g_itemClassDB;
WowClientDB<ItemDisplayInfoRec> g_itemDisplayInfoDB;
WowClientDB<ItemSubClassRec> g_itemSubClassDB;
WowClientDB<ItemRec> g_itemDB;
WowClientDB<ItemVisualsRec> g_itemVisualsDB;
WowClientDB<LoadingScreensRec> g_loadingScreensDB;
WowClientDB<ScreenEffectRec> g_screenEffectDB;
WowClientDB<MapRec> g_mapDB;
WowClientDB<NameGenRec> g_nameGenDB;
WowClientDB<NamesProfanityRec> g_namesProfanityDB;
WowClientDB<NamesReservedRec> g_namesReservedDB;
WowClientDB<PaperDollItemFrameRec> g_paperDollItemFrameDB;
WowClientDB<SoundEntriesRec> g_soundEntriesDB;
WowClientDB<SoundEntriesAdvancedRec> g_soundEntriesAdvancedDB;
WowClientDB<UnitBloodLevelsRec> g_unitBloodLevelsDB;

void LoadDB(WowClientDB_Base* db, const char* filename, int32_t linenumber) {
    db->Load(filename, linenumber);
};

void StaticDBLoadAll(void (*loadFn)(WowClientDB_Base*, const char*, int32_t)) {
    loadFn(&g_achievementDB, __FILE__, __LINE__);
    loadFn(&g_areaTableDB, __FILE__, __LINE__);
    loadFn(&g_cfg_CategoriesDB, __FILE__, __LINE__);
    loadFn(&g_cfg_ConfigsDB, __FILE__, __LINE__);
    loadFn(&g_charBaseInfoDB, __FILE__, __LINE__);
    loadFn(&g_charHairGeosetsDB, __FILE__, __LINE__);
    loadFn(&g_charSectionsDB, __FILE__, __LINE__);
    loadFn(&g_charStartOutfitDB, __FILE__, __LINE__);
    loadFn(&g_charTitlesDB, __FILE__, __LINE__);
    loadFn(&g_chatProfanityDB, __FILE__, __LINE__);
    loadFn(&g_characterFacialHairStylesDB, __FILE__, __LINE__);
    loadFn(&g_chrClassesDB, __FILE__, __LINE__);
    loadFn(&g_chrRacesDB, __FILE__, __LINE__);
    loadFn(&g_creatureDisplayInfoDB, __FILE__, __LINE__);
    loadFn(&g_gameObjectArtKitDB, __FILE__, __LINE__);
    loadFn(&g_gameObjectDisplayInfoDB, __FILE__, __LINE__);
    loadFn(&g_lockDB, __FILE__, __LINE__);
    loadFn(&g_lockTypeDB, __FILE__, __LINE__);
    loadFn(&g_spellDB, __FILE__, __LINE__);
    loadFn(&g_spellIconDB, __FILE__, __LINE__);
    loadFn(&g_creatureDisplayInfoExtraDB, __FILE__, __LINE__);
    loadFn(&g_creatureModelDataDB, __FILE__, __LINE__);
    loadFn(&g_animationDataDB, __FILE__, __LINE__);
    loadFn(&g_emotesDB, __FILE__, __LINE__);
    loadFn(&g_skillLineDB, __FILE__, __LINE__);
    loadFn(&g_skillLineAbilityDB, __FILE__, __LINE__);
    loadFn(&g_spellVisualDB, __FILE__, __LINE__);
    loadFn(&g_spellVisualKitDB, __FILE__, __LINE__);
    loadFn(&g_spellVisualEffectNameDB, __FILE__, __LINE__);
    loadFn(&g_spellVisualKitModelAttachDB, __FILE__, __LINE__);
    loadFn(&g_spellVisualKitAreaModelDB, __FILE__, __LINE__);
    loadFn(&g_spellEffectCameraShakesDB, __FILE__, __LINE__);
    loadFn(&g_spellCastTimesDB, __FILE__, __LINE__);
    loadFn(&g_skillCostsDataDB, __FILE__, __LINE__);
    loadFn(&g_skillRaceClassInfoDB, __FILE__, __LINE__);
    loadFn(&g_spellDescriptionVariablesDB, __FILE__, __LINE__);
    loadFn(&g_resistancesDB, __FILE__, __LINE__);
    loadFn(&g_spellDifficultyDB, __FILE__, __LINE__);
    loadFn(&g_spellDurationDB, __FILE__, __LINE__);
    loadFn(&g_spellRadiusDB, __FILE__, __LINE__);
    loadFn(&g_spellCategoryDB, __FILE__, __LINE__);
    loadFn(&g_itemSubClassMaskDB, __FILE__, __LINE__);
    loadFn(&g_totemCategoryDB, __FILE__, __LINE__);
    loadFn(&g_powerDisplayDB, __FILE__, __LINE__);
    loadFn(&g_spellRuneCostDB, __FILE__, __LINE__);
    loadFn(&g_spellRangeDB, __FILE__, __LINE__);
    loadFn(&g_spellChainEffectsDB, __FILE__, __LINE__);
    loadFn(&g_vehicleSeatDB, __FILE__, __LINE__);
    loadFn(&g_lightDB, __FILE__, __LINE__);
    loadFn(&g_lightParamsDB, __FILE__, __LINE__);
    loadFn(&g_lightSkyboxDB, __FILE__, __LINE__);
    loadFn(&g_cameraShakesDB, __FILE__, __LINE__);
    loadFn(&g_wmoAreaTableDB, __FILE__, __LINE__);
    loadFn(&g_liquidTypeDB, __FILE__, __LINE__);
    loadFn(&g_liquidMaterialDB, __FILE__, __LINE__);
    loadFn(&g_weatherDB, __FILE__, __LINE__);
    loadFn(&g_groundEffectTextureDB, __FILE__, __LINE__);
    loadFn(&g_groundEffectDoodadDB, __FILE__, __LINE__);
    loadFn(&g_lightIntBandDB, __FILE__, __LINE__);
    loadFn(&g_lightFloatBandDB, __FILE__, __LINE__);
    loadFn(&g_creatureSoundDataDB, __FILE__, __LINE__);
    loadFn(&g_creatureFamilyDB, __FILE__, __LINE__);
    loadFn(&g_creatureTypeDB, __FILE__, __LINE__);
    loadFn(&g_creatureMovementInfoDB, __FILE__, __LINE__);
    loadFn(&g_materialDB, __FILE__, __LINE__);
    loadFn(&g_terrainTypeDB, __FILE__, __LINE__);
    loadFn(&g_footstepTerrainLookupDB, __FILE__, __LINE__);
    loadFn(&g_transportAnimationDB, __FILE__, __LINE__);
    loadFn(&g_transportRotationDB, __FILE__, __LINE__);
    loadFn(&g_taxiPathNodeDB, __FILE__, __LINE__);
    loadFn(&g_transportPhysicsDB, __FILE__, __LINE__);
    loadFn(&g_mapDifficultyDB, __FILE__, __LINE__);
    loadFn(&g_destructibleModelDataDB, __FILE__, __LINE__);
    loadFn(&g_currencyTypesDB, __FILE__, __LINE__);
    loadFn(&g_spellShapeshiftFormDB, __FILE__, __LINE__);
    loadFn(&g_factionGroupDB, __FILE__, __LINE__);
    loadFn(&g_factionDB, __FILE__, __LINE__);
    loadFn(&g_factionTemplateDB, __FILE__, __LINE__);
    loadFn(&g_gameTipsDB, __FILE__, __LINE__);
    loadFn(&g_gmTicketCategoryDB, __FILE__, __LINE__);
    loadFn(&g_itemClassDB, __FILE__, __LINE__);
    loadFn(&g_itemDisplayInfoDB, __FILE__, __LINE__);
    loadFn(&g_itemSubClassDB, __FILE__, __LINE__);
    loadFn(&g_itemDB, __FILE__, __LINE__);
    loadFn(&g_itemVisualsDB, __FILE__, __LINE__);
    loadFn(&g_loadingScreensDB, __FILE__, __LINE__);
    loadFn(&g_screenEffectDB, __FILE__, __LINE__);
    loadFn(&g_mapDB, __FILE__, __LINE__);
    loadFn(&g_nameGenDB, __FILE__, __LINE__);
    loadFn(&g_namesProfanityDB, __FILE__, __LINE__);
    loadFn(&g_namesReservedDB, __FILE__, __LINE__);
    loadFn(&g_paperDollItemFrameDB, __FILE__, __LINE__);
    loadFn(&g_soundEntriesDB, __FILE__, __LINE__);
    loadFn(&g_soundEntriesAdvancedDB, __FILE__, __LINE__);
    loadFn(&g_unitBloodLevelsDB, __FILE__, __LINE__);
};

// The schools by index, and which one armor stands for (-1 until the table is read).
static TSFixedArray<ResistancesRec*> s_resistances;    // ref: DAT_00c5d668
static int32_t s_physicalResistance = -1;              // ref: DAT_00ad2f90

void ClientDBInitialize() {
    // TODO

    StaticDBLoadAll(LoadDB);

    // TODO the rest of the post-load steps (0x00634e00..0x00634e34)

    ResistancesInitialize();
}

// ref: FUN_00634ae0
// Indexes the seven schools by row and remembers the one flagged as physical.
void ResistancesInitialize() {
    uint32_t count = g_resistancesDB.GetNumRecords();
    s_physicalResistance = -1;

    if (count != 7) {
        ConsolePrintf("Warning: The Resistances table has the wrong number of entries");

        if (count > 7) {
            count = 7;
        }
    }

    s_resistances.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        auto rec = g_resistancesDB.GetRecordByIndex(static_cast<int32_t>(i));

        if (rec->m_flags & 0x1) {
            s_physicalResistance = static_cast<int32_t>(i);
        }

        s_resistances[i] = rec;
    }
}

// ref: FUN_006337a0
int32_t ResistancesGetPhysicalIndex() {
    return s_physicalResistance;
}

// ref: FUN_004cfbb0
// Expands a run-length packed record into size bytes. A byte equal to the one before it starts a
// run: the byte after it counts further copies, and the byte after that is copied through.
void DbUnpackRecord(const char* src, int32_t size, char* dst) {
    *dst = *src;
    char* out = dst + 1;

    while (out < dst + size) {
        const char* prev = src;
        src = prev + 1;

        *out++ = *src;

        if (*src == *prev) {
            for (uint32_t count = static_cast<uint8_t>(prev[2]); count != 0; count--) {
                *out++ = *src;
            }

            src = prev + 3;

            if (out < dst + size) {
                *out++ = *src;
            }
        }
    }
}
