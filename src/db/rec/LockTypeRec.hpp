#ifndef DB_REC_LOCK_TYPE_REC_HPP
#define DB_REC_LOCK_TYPE_REC_HPP

#include <cstdint>

class SFile;

// LockType.dbc (0x00ad4128): the skills a lock can need -- Lockpicking, Herbalism, Mining -- and
// the cursor ("PickLock", "GatherHerbs") a game object with one shows.
class LockTypeRec {
    public:
        static const int32_t COLUMN_COUNT = 53;
        static const int32_t COLUMN_NAME = 1;           // 17 locale columns
        static const int32_t COLUMN_RESOURCE_NAME = 18; // 17 locale columns
        static const int32_t COLUMN_VERB = 35;          // 17 locale columns
        static const int32_t COLUMN_CURSOR_NAME = 52;

        int32_t m_ID;                   // +0x00
        const char* m_name;             // +0x04
        const char* m_resourceName;     // +0x08
        const char* m_verb;             // +0x0c
        const char* m_cursorName;       // +0x10

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
