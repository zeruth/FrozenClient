#ifndef DB_REC_SPELL_ICON_REC_HPP
#define DB_REC_SPELL_ICON_REC_HPP

#include <cstdint>

class SFile;

// SpellIcon.dbc: an icon id resolves to the texture the action bar draws.
//
// 3226 rows in 3.3.5a, 2 columns, 8 bytes a row. Verified against the shipped file: icon 185 is
// Interface\Icons\Spell_Fire_FlameBolt, which is Fireball's.
class SpellIconRec {
    public:
        int32_t m_ID;
        const char* m_textureFilename;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
