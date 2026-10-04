#ifndef DB_REC_SPELL_DESCRIPTION_VARIABLES_REC_HPP
#define DB_REC_SPELL_DESCRIPTION_VARIABLES_REC_HPP

#include <cstdint>

class SFile;

// SpellDescriptionVariables.dbc: the $name= definitions a spell's description text reads as $<name>.
class SpellDescriptionVariablesRec {
    public:
        static const int32_t COLUMN_COUNT = 2;

        int32_t m_ID;
        const char* m_variables;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
