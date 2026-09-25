#ifndef DB_DB_HPP
#define DB_DB_HPP

#include "db/WowClientDB.hpp"
#include "db/rec/AchievementRec.hpp"
#include "db/rec/AreaTableRec.hpp"
#include "db/rec/Cfg_CategoriesRec.hpp"
#include "db/rec/Cfg_ConfigsRec.hpp"
#include "db/rec/CharBaseInfoRec.hpp"
#include "db/rec/CharHairGeosetsRec.hpp"
#include "db/rec/CharSectionsRec.hpp"
#include "db/rec/CharStartOutfitRec.hpp"
#include "db/rec/CharTitlesRec.hpp"
#include "db/rec/ChatProfanityRec.hpp"
#include "db/rec/CharacterFacialHairStylesRec.hpp"
#include "db/rec/ChrClassesRec.hpp"
#include "db/rec/ChrRacesRec.hpp"
#include "db/rec/CreatureDisplayInfoRec.hpp"
#include "db/rec/CreatureDisplayInfoExtraRec.hpp"
#include "db/rec/GameObjectDisplayInfoRec.hpp"
#include "db/rec/SpellRec.hpp"
#include "db/rec/SpellIconRec.hpp"
#include "db/rec/CreatureModelDataRec.hpp"
#include "db/rec/AnimationDataRec.hpp"
#include "db/rec/EmotesRec.hpp"
#include "db/rec/SkillLineRec.hpp"
#include "db/rec/SkillLineAbilityRec.hpp"
#include "db/rec/SpellVisualRec.hpp"
#include "db/rec/SpellVisualKitRec.hpp"
#include "db/rec/SpellVisualEffectNameRec.hpp"
#include "db/rec/LightRec.hpp"
#include "db/rec/LightParamsRec.hpp"
#include "db/rec/LightSkyboxRec.hpp"
#include "db/rec/LiquidTypeRec.hpp"
#include "db/rec/WeatherRec.hpp"
#include "db/rec/GroundEffectTextureRec.hpp"
#include "db/rec/GroundEffectDoodadRec.hpp"
#include "db/rec/LightIntBandRec.hpp"
#include "db/rec/LightFloatBandRec.hpp"
#include "db/rec/CreatureSoundDataRec.hpp"
#include "db/rec/CreatureFamilyRec.hpp"
#include "db/rec/CreatureTypeRec.hpp"
#include "db/rec/CurrencyTypesRec.hpp"
#include "db/rec/SpellShapeshiftFormRec.hpp"
#include "db/rec/FactionGroupRec.hpp"
#include "db/rec/FactionRec.hpp"
#include "db/rec/FactionTemplateRec.hpp"
#include "db/rec/GameTipsRec.hpp"
#include "db/rec/GMTicketCategoryRec.hpp"
#include "db/rec/ItemClassRec.hpp"
#include "db/rec/ItemDisplayInfoRec.hpp"
#include "db/rec/ItemSubClassRec.hpp"
#include "db/rec/ItemRec.hpp"
#include "db/rec/ItemVisualsRec.hpp"
#include "db/rec/LoadingScreensRec.hpp"
#include "db/rec/MapRec.hpp"
#include "db/rec/NameGenRec.hpp"
#include "db/rec/NamesProfanityRec.hpp"
#include "db/rec/NamesReservedRec.hpp"
#include "db/rec/PaperDollItemFrameRec.hpp"
#include "db/rec/SoundEntriesRec.hpp"
#include "db/rec/SoundEntriesAdvancedRec.hpp"
#include "db/rec/UnitBloodLevelsRec.hpp"

extern WowClientDB<AchievementRec> g_achievementDB;
extern WowClientDB<AreaTableRec> g_areaTableDB;
extern WowClientDB<Cfg_CategoriesRec> g_cfg_CategoriesDB;
extern WowClientDB<Cfg_ConfigsRec> g_cfg_ConfigsDB;
extern WowClientDB<CharBaseInfoRec> g_charBaseInfoDB;
extern WowClientDB<CharHairGeosetsRec> g_charHairGeosetsDB;
extern WowClientDB<CharSectionsRec> g_charSectionsDB;
extern WowClientDB<CharStartOutfitRec> g_charStartOutfitDB;
extern WowClientDB<CharTitlesRec> g_charTitlesDB;
extern WowClientDB<ChatProfanityRec> g_chatProfanityDB;
extern WowClientDB<CharacterFacialHairStylesRec> g_characterFacialHairStylesDB;
extern WowClientDB<ChrClassesRec> g_chrClassesDB;
extern WowClientDB<ChrRacesRec> g_chrRacesDB;
extern WowClientDB<CreatureDisplayInfoRec> g_creatureDisplayInfoDB;
extern WowClientDB<CreatureDisplayInfoExtraRec> g_creatureDisplayInfoExtraDB;
extern WowClientDB<GameObjectDisplayInfoRec> g_gameObjectDisplayInfoDB;
extern WowClientDB<SpellRec> g_spellDB;
extern WowClientDB<SpellIconRec> g_spellIconDB;
extern WowClientDB<CreatureModelDataRec> g_creatureModelDataDB;
extern WowClientDB<AnimationDataRec> g_animationDataDB;
extern WowClientDB<EmotesRec> g_emotesDB;
extern WowClientDB<SkillLineRec> g_skillLineDB;
extern WowClientDB<SkillLineAbilityRec> g_skillLineAbilityDB;
extern WowClientDB<SpellVisualRec> g_spellVisualDB;
extern WowClientDB<SpellVisualKitRec> g_spellVisualKitDB;
extern WowClientDB<SpellVisualEffectNameRec> g_spellVisualEffectNameDB;
extern WowClientDB<LightRec> g_lightDB;
extern WowClientDB<LightParamsRec> g_lightParamsDB;
extern WowClientDB<LightSkyboxRec> g_lightSkyboxDB;
extern WowClientDB<LiquidTypeRec> g_liquidTypeDB;
extern WowClientDB<WeatherRec> g_weatherDB;
extern WowClientDB<GroundEffectTextureRec> g_groundEffectTextureDB;
extern WowClientDB<GroundEffectDoodadRec> g_groundEffectDoodadDB;
extern WowClientDB<LightIntBandRec> g_lightIntBandDB;
extern WowClientDB<LightFloatBandRec> g_lightFloatBandDB;
extern WowClientDB<CreatureSoundDataRec> g_creatureSoundDataDB;
extern WowClientDB<CreatureFamilyRec> g_creatureFamilyDB;
extern WowClientDB<CreatureTypeRec> g_creatureTypeDB;
extern WowClientDB<CurrencyTypesRec> g_currencyTypesDB;
extern WowClientDB<SpellShapeshiftFormRec> g_spellShapeshiftFormDB;
extern WowClientDB<FactionGroupRec> g_factionGroupDB;
extern WowClientDB<FactionRec> g_factionDB;
extern WowClientDB<FactionTemplateRec> g_factionTemplateDB;
extern WowClientDB<GameTipsRec> g_gameTipsDB;
extern WowClientDB<GMTicketCategoryRec> g_gmTicketCategoryDB;
extern WowClientDB<ItemClassRec> g_itemClassDB;
extern WowClientDB<ItemDisplayInfoRec> g_itemDisplayInfoDB;
extern WowClientDB<ItemSubClassRec> g_itemSubClassDB;
extern WowClientDB<ItemRec> g_itemDB;
extern WowClientDB<ItemVisualsRec> g_itemVisualsDB;
extern WowClientDB<LoadingScreensRec> g_loadingScreensDB;
extern WowClientDB<MapRec> g_mapDB;
extern WowClientDB<NameGenRec> g_nameGenDB;
extern WowClientDB<NamesProfanityRec> g_namesProfanityDB;
extern WowClientDB<NamesReservedRec> g_namesReservedDB;
extern WowClientDB<PaperDollItemFrameRec> g_paperDollItemFrameDB;
extern WowClientDB<SoundEntriesRec> g_soundEntriesDB;
extern WowClientDB<SoundEntriesAdvancedRec> g_soundEntriesAdvancedDB;
extern WowClientDB<UnitBloodLevelsRec> g_unitBloodLevelsDB;

void ClientDBInitialize();

void DbUnpackRecord(const char* src, int32_t size, char* dst);

#endif
