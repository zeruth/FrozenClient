#include "db/rec/SpellRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"
#include <cstring>

const char* SpellRec::GetFilename() {
    return "DBFilesClient\\Spell.dbc";
}

uint32_t SpellRec::GetNumColumns() {
    return SpellRec::COLUMN_COUNT;
}

uint32_t SpellRec::GetRowSize() {
    return SpellRec::COLUMN_COUNT * 4;
}

bool SpellRec::NeedIDAssigned() {
    return false;
}

int32_t SpellRec::GetID() {
    return this->m_ID;
}

void SpellRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    memcpy(&this->m_ID, &columns[0], sizeof(this->m_ID));
    memcpy(&this->m_category, &columns[1], sizeof(this->m_category));
    memcpy(&this->m_dispel, &columns[2], sizeof(this->m_dispel));
    memcpy(&this->m_mechanic, &columns[3], sizeof(this->m_mechanic));
    memcpy(&this->m_attributes, &columns[4], sizeof(this->m_attributes));
    memcpy(&this->m_attributesEx, &columns[5], sizeof(this->m_attributesEx));
    memcpy(&this->m_attributesEx2, &columns[6], sizeof(this->m_attributesEx2));
    memcpy(&this->m_attributesEx3, &columns[7], sizeof(this->m_attributesEx3));
    memcpy(&this->m_attributesEx4, &columns[8], sizeof(this->m_attributesEx4));
    memcpy(&this->m_attributesEx5, &columns[9], sizeof(this->m_attributesEx5));
    memcpy(&this->m_attributesEx6, &columns[10], sizeof(this->m_attributesEx6));
    memcpy(&this->m_attributesEx7, &columns[11], sizeof(this->m_attributesEx7));
    memcpy(this->m_stances, &columns[12], sizeof(this->m_stances));
    memcpy(this->m_stancesNot, &columns[14], sizeof(this->m_stancesNot));
    memcpy(&this->m_targets, &columns[16], sizeof(this->m_targets));
    memcpy(&this->m_targetCreatureType, &columns[17], sizeof(this->m_targetCreatureType));
    memcpy(&this->m_requiresSpellFocus, &columns[18], sizeof(this->m_requiresSpellFocus));
    memcpy(&this->m_facingCasterFlags, &columns[19], sizeof(this->m_facingCasterFlags));
    memcpy(&this->m_casterAuraState, &columns[20], sizeof(this->m_casterAuraState));
    memcpy(&this->m_targetAuraState, &columns[21], sizeof(this->m_targetAuraState));
    memcpy(&this->m_excludeCasterAuraState, &columns[22], sizeof(this->m_excludeCasterAuraState));
    memcpy(&this->m_excludeTargetAuraState, &columns[23], sizeof(this->m_excludeTargetAuraState));
    memcpy(&this->m_casterAuraSpell, &columns[24], sizeof(this->m_casterAuraSpell));
    memcpy(&this->m_targetAuraSpell, &columns[25], sizeof(this->m_targetAuraSpell));
    memcpy(&this->m_excludeCasterAuraSpell, &columns[26], sizeof(this->m_excludeCasterAuraSpell));
    memcpy(&this->m_excludeTargetAuraSpell, &columns[27], sizeof(this->m_excludeTargetAuraSpell));
    memcpy(&this->m_castingTimeIndex, &columns[28], sizeof(this->m_castingTimeIndex));
    memcpy(&this->m_recoveryTime, &columns[29], sizeof(this->m_recoveryTime));
    memcpy(&this->m_categoryRecoveryTime, &columns[30], sizeof(this->m_categoryRecoveryTime));
    memcpy(&this->m_interruptFlags, &columns[31], sizeof(this->m_interruptFlags));
    memcpy(&this->m_auraInterruptFlags, &columns[32], sizeof(this->m_auraInterruptFlags));
    memcpy(&this->m_channelInterruptFlags, &columns[33], sizeof(this->m_channelInterruptFlags));
    memcpy(&this->m_procTypeMask, &columns[34], sizeof(this->m_procTypeMask));
    memcpy(&this->m_procChance, &columns[35], sizeof(this->m_procChance));
    memcpy(&this->m_procCharges, &columns[36], sizeof(this->m_procCharges));
    memcpy(&this->m_maxLevel, &columns[37], sizeof(this->m_maxLevel));
    memcpy(&this->m_baseLevel, &columns[38], sizeof(this->m_baseLevel));
    memcpy(&this->m_spellLevel, &columns[39], sizeof(this->m_spellLevel));
    memcpy(&this->m_durationIndex, &columns[40], sizeof(this->m_durationIndex));
    memcpy(&this->m_powerType, &columns[41], sizeof(this->m_powerType));
    memcpy(&this->m_manaCost, &columns[42], sizeof(this->m_manaCost));
    memcpy(&this->m_manaCostPerLevel, &columns[43], sizeof(this->m_manaCostPerLevel));
    memcpy(&this->m_manaPerSecond, &columns[44], sizeof(this->m_manaPerSecond));
    memcpy(&this->m_manaPerSecondPerLevel, &columns[45], sizeof(this->m_manaPerSecondPerLevel));
    memcpy(&this->m_rangeIndex, &columns[46], sizeof(this->m_rangeIndex));
    memcpy(&this->m_speed, &columns[47], sizeof(this->m_speed));
    memcpy(&this->m_modalNextSpell, &columns[48], sizeof(this->m_modalNextSpell));
    memcpy(&this->m_cumulativeAura, &columns[49], sizeof(this->m_cumulativeAura));
    memcpy(this->m_totem, &columns[50], sizeof(this->m_totem));
    memcpy(this->m_reagent, &columns[52], sizeof(this->m_reagent));
    memcpy(this->m_reagentCount, &columns[60], sizeof(this->m_reagentCount));
    memcpy(&this->m_equippedItemClass, &columns[68], sizeof(this->m_equippedItemClass));
    memcpy(&this->m_equippedItemSubclass, &columns[69], sizeof(this->m_equippedItemSubclass));
    memcpy(&this->m_equippedItemInvTypes, &columns[70], sizeof(this->m_equippedItemInvTypes));
    memcpy(this->m_effect, &columns[71], sizeof(this->m_effect));
    memcpy(this->m_effectDieSides, &columns[74], sizeof(this->m_effectDieSides));
    memcpy(this->m_effectRealPointsPerLevel, &columns[77], sizeof(this->m_effectRealPointsPerLevel));
    memcpy(this->m_effectBasePoints, &columns[80], sizeof(this->m_effectBasePoints));
    memcpy(this->m_effectMechanic, &columns[83], sizeof(this->m_effectMechanic));
    memcpy(this->m_effectImplicitTargetA, &columns[86], sizeof(this->m_effectImplicitTargetA));
    memcpy(this->m_effectImplicitTargetB, &columns[89], sizeof(this->m_effectImplicitTargetB));
    memcpy(this->m_effectRadiusIndex, &columns[92], sizeof(this->m_effectRadiusIndex));
    memcpy(this->m_effectAura, &columns[95], sizeof(this->m_effectAura));
    memcpy(this->m_effectAuraPeriod, &columns[98], sizeof(this->m_effectAuraPeriod));
    memcpy(this->m_effectAmplitude, &columns[101], sizeof(this->m_effectAmplitude));
    memcpy(this->m_effectChainTargets, &columns[104], sizeof(this->m_effectChainTargets));
    memcpy(this->m_effectItemType, &columns[107], sizeof(this->m_effectItemType));
    memcpy(this->m_effectMiscValue, &columns[110], sizeof(this->m_effectMiscValue));
    memcpy(this->m_effectMiscValueB, &columns[113], sizeof(this->m_effectMiscValueB));
    memcpy(this->m_effectTriggerSpell, &columns[116], sizeof(this->m_effectTriggerSpell));
    memcpy(this->m_effectPointsPerCombo, &columns[119], sizeof(this->m_effectPointsPerCombo));
    memcpy(this->m_effectSpellClassMask, &columns[122], sizeof(this->m_effectSpellClassMask));
    memcpy(this->m_spellVisualID, &columns[131], sizeof(this->m_spellVisualID));
    memcpy(&this->m_spellIconID, &columns[133], sizeof(this->m_spellIconID));
    memcpy(&this->m_activeIconID, &columns[134], sizeof(this->m_activeIconID));
    memcpy(&this->m_spellPriority, &columns[135], sizeof(this->m_spellPriority));
    this->m_name = stringBuffer ? &stringBuffer[columns[136 + CURRENT_LANGUAGE]] : "";
    this->m_rank = stringBuffer ? &stringBuffer[columns[153 + CURRENT_LANGUAGE]] : "";
    this->m_description = stringBuffer ? &stringBuffer[columns[170 + CURRENT_LANGUAGE]] : "";
    this->m_auraDescription = stringBuffer ? &stringBuffer[columns[187 + CURRENT_LANGUAGE]] : "";
    memcpy(&this->m_manaCostPct, &columns[204], sizeof(this->m_manaCostPct));
    memcpy(&this->m_startRecoveryCategory, &columns[205], sizeof(this->m_startRecoveryCategory));
    memcpy(&this->m_startRecoveryTime, &columns[206], sizeof(this->m_startRecoveryTime));
    memcpy(&this->m_maxTargetLevel, &columns[207], sizeof(this->m_maxTargetLevel));
    memcpy(&this->m_spellClassSet, &columns[208], sizeof(this->m_spellClassSet));
    memcpy(this->m_spellClassMask, &columns[209], sizeof(this->m_spellClassMask));
    memcpy(&this->m_maxTargets, &columns[212], sizeof(this->m_maxTargets));
    memcpy(&this->m_defenseType, &columns[213], sizeof(this->m_defenseType));
    memcpy(&this->m_preventionType, &columns[214], sizeof(this->m_preventionType));
    memcpy(&this->m_stanceBarOrder, &columns[215], sizeof(this->m_stanceBarOrder));
    memcpy(this->m_effectChainAmplitude, &columns[216], sizeof(this->m_effectChainAmplitude));
    memcpy(&this->m_minFactionID, &columns[219], sizeof(this->m_minFactionID));
    memcpy(&this->m_minReputation, &columns[220], sizeof(this->m_minReputation));
    memcpy(&this->m_requiredAuraVision, &columns[221], sizeof(this->m_requiredAuraVision));
    memcpy(this->m_requiredTotemCategoryID, &columns[222], sizeof(this->m_requiredTotemCategoryID));
    memcpy(&this->m_requiredAreasID, &columns[224], sizeof(this->m_requiredAreasID));
    memcpy(&this->m_schoolMask, &columns[225], sizeof(this->m_schoolMask));
    memcpy(&this->m_runeCostID, &columns[226], sizeof(this->m_runeCostID));
    memcpy(&this->m_spellMissileID, &columns[227], sizeof(this->m_spellMissileID));
    memcpy(&this->m_powerDisplayID, &columns[228], sizeof(this->m_powerDisplayID));
    memcpy(this->m_effectBonusCoefficient, &columns[229], sizeof(this->m_effectBonusCoefficient));
    memcpy(&this->m_descriptionVariablesID, &columns[232], sizeof(this->m_descriptionVariablesID));
    memcpy(&this->m_difficulty, &columns[233], sizeof(this->m_difficulty));

    return true;
}
