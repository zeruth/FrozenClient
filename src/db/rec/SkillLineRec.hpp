#ifndef DB_REC_SKILL_LINE_REC_HPP
#define DB_REC_SKILL_LINE_REC_HPP

#include <cstdint>

class SFile;

// SkillLine.dbc: the skill categories spells are filed under. A class's spellbook tabs are its
// class skill lines (categoryID 7 -- Frost, Unholy and Blood for a death knight); the rest are
// professions, weapon skills and languages.
class SkillLineRec {
    public:
        static const int32_t COLUMN_COUNT = 56;
        static const int32_t COLUMN_CATEGORY = 1;
        static const int32_t COLUMN_DISPLAY_NAME = 3;   // 17 locale columns
        static const int32_t COLUMN_ICON = 37;          // SpellIcon.dbc id

        enum {
            CATEGORY_CLASS = 7,
        };

        int32_t m_ID;
        int32_t m_categoryID;
        const char* m_displayName;
        int32_t m_spellIconID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
