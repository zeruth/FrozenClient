#include "db/Db.hpp"
#include "db/WowClientDB_Base.hpp"

WowClientDB<AchievementRec> g_achievementDB;
WowClientDB<AreaTableRec> g_areaTableDB;
WowClientDB<Cfg_CategoriesRec> g_cfg_CategoriesDB;
WowClientDB<Cfg_ConfigsRec> g_cfg_ConfigsDB;
WowClientDB<CharBaseInfoRec> g_charBaseInfoDB;
WowClientDB<CharHairGeosetsRec> g_charHairGeosetsDB;
WowClientDB<CharSectionsRec> g_charSectionsDB;
WowClientDB<CharStartOutfitRec> g_charStartOutfitDB;
WowClientDB<ChatProfanityRec> g_chatProfanityDB;
WowClientDB<CharacterFacialHairStylesRec> g_characterFacialHairStylesDB;
WowClientDB<ChrClassesRec> g_chrClassesDB;
WowClientDB<ChrRacesRec> g_chrRacesDB;
WowClientDB<CreatureDisplayInfoRec> g_creatureDisplayInfoDB;
WowClientDB<CreatureDisplayInfoExtraRec> g_creatureDisplayInfoExtraDB;
WowClientDB<GameObjectDisplayInfoRec> g_gameObjectDisplayInfoDB;
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
WowClientDB<LightRec> g_lightDB;
WowClientDB<LightParamsRec> g_lightParamsDB;
WowClientDB<LightSkyboxRec> g_lightSkyboxDB;
WowClientDB<LiquidTypeRec> g_liquidTypeDB;
WowClientDB<WeatherRec> g_weatherDB;
WowClientDB<GroundEffectTextureRec> g_groundEffectTextureDB;
WowClientDB<GroundEffectDoodadRec> g_groundEffectDoodadDB;
WowClientDB<LightIntBandRec> g_lightIntBandDB;
WowClientDB<LightFloatBandRec> g_lightFloatBandDB;
WowClientDB<CreatureSoundDataRec> g_creatureSoundDataDB;
WowClientDB<FactionGroupRec> g_factionGroupDB;
WowClientDB<FactionTemplateRec> g_factionTemplateDB;
WowClientDB<GameTipsRec> g_gameTipsDB;
WowClientDB<ItemDisplayInfoRec> g_itemDisplayInfoDB;
WowClientDB<ItemRec> g_itemDB;
WowClientDB<ItemVisualsRec> g_itemVisualsDB;
WowClientDB<LoadingScreensRec> g_loadingScreensDB;
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
    loadFn(&g_chatProfanityDB, __FILE__, __LINE__);
    loadFn(&g_characterFacialHairStylesDB, __FILE__, __LINE__);
    loadFn(&g_chrClassesDB, __FILE__, __LINE__);
    loadFn(&g_chrRacesDB, __FILE__, __LINE__);
    loadFn(&g_creatureDisplayInfoDB, __FILE__, __LINE__);
    loadFn(&g_gameObjectDisplayInfoDB, __FILE__, __LINE__);
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
    loadFn(&g_lightDB, __FILE__, __LINE__);
    loadFn(&g_lightParamsDB, __FILE__, __LINE__);
    loadFn(&g_lightSkyboxDB, __FILE__, __LINE__);
    loadFn(&g_liquidTypeDB, __FILE__, __LINE__);
    loadFn(&g_weatherDB, __FILE__, __LINE__);
    loadFn(&g_groundEffectTextureDB, __FILE__, __LINE__);
    loadFn(&g_groundEffectDoodadDB, __FILE__, __LINE__);
    loadFn(&g_lightIntBandDB, __FILE__, __LINE__);
    loadFn(&g_lightFloatBandDB, __FILE__, __LINE__);
    loadFn(&g_creatureSoundDataDB, __FILE__, __LINE__);
    loadFn(&g_factionGroupDB, __FILE__, __LINE__);
    loadFn(&g_factionTemplateDB, __FILE__, __LINE__);
    loadFn(&g_gameTipsDB, __FILE__, __LINE__);
    loadFn(&g_itemDisplayInfoDB, __FILE__, __LINE__);
    loadFn(&g_itemDB, __FILE__, __LINE__);
    loadFn(&g_itemVisualsDB, __FILE__, __LINE__);
    loadFn(&g_loadingScreensDB, __FILE__, __LINE__);
    loadFn(&g_mapDB, __FILE__, __LINE__);
    loadFn(&g_nameGenDB, __FILE__, __LINE__);
    loadFn(&g_namesProfanityDB, __FILE__, __LINE__);
    loadFn(&g_namesReservedDB, __FILE__, __LINE__);
    loadFn(&g_paperDollItemFrameDB, __FILE__, __LINE__);
    loadFn(&g_soundEntriesDB, __FILE__, __LINE__);
    loadFn(&g_soundEntriesAdvancedDB, __FILE__, __LINE__);
    loadFn(&g_unitBloodLevelsDB, __FILE__, __LINE__);
};

void ClientDBInitialize() {
    // TODO

    StaticDBLoadAll(LoadDB);

    // TODO
}
