#ifndef DB_REC_LOCK_REC_HPP
#define DB_REC_LOCK_REC_HPP

#include <cstdint>

class SFile;

// Lock.dbc (0x00ad4104): what opening a locked game object takes. Each of the eight entries is a
// kind (1 an item, 2 a LockType -- a skill such as Herbalism or Lockpicking, 3 a spell), what it
// names, the skill it needs and the action it performs.
class LockRec {
    public:
        enum {
            TYPE_ITEM = 1,
            TYPE_LOCKTYPE = 2,
            TYPE_SPELL = 3,
        };

        int32_t m_ID;               // +0x00
        int32_t m_type[8];          // +0x04
        int32_t m_index[8];         // +0x24
        int32_t m_skill[8];         // +0x44
        int32_t m_action[8];        // +0x64

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
