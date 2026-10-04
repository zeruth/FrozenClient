#ifndef DB_REC_RESISTANCES_REC_HPP
#define DB_REC_RESISTANCES_REC_HPP

#include <cstdint>

class SFile;

// Resistances.dbc: the seven damage schools; flag 0x1 marks the one armor stands for.
class ResistancesRec {
    public:
        static const int32_t COLUMN_COUNT = 20;

        int32_t m_ID;
        uint32_t m_flags;
        int32_t m_fizzleSoundID;
        const char* m_name;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
