#ifndef DB_REC_SKILL_LINE_REC_HPP
#define DB_REC_SKILL_LINE_REC_HPP

#include <cstdint>

class SFile;

// SkillLine.dbc: the skill categories spells are filed under. A class's spellbook tabs are its class skill lines (categoryID 7); the rest are professions, weapon skills and languages.
class SkillLineRec {
    public:
        static const int32_t COLUMN_COUNT = 56;

        enum {
            CATEGORY_CLASS = 7,
        };

        int32_t m_ID;
        int32_t m_categoryID;
        int32_t m_skillCostsID;
        const char* m_displayName;
        const char* m_description;
        int32_t m_spellIconID;
        const char* m_alternateVerb;
        int32_t m_canLink;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
