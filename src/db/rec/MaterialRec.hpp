// Material.dbc -- an item's material (UNIT_WEAPON_INFO +0x03) and the sounds it makes.
//
// Five columns of twenty bytes. FUN_004d07b0 plays the sheathe sound (+0x0c) when a weapon goes
// back to its sheath and the unsheathe sound (+0x10) when it comes out.
#ifndef DB_REC_MATERIAL_REC_HPP
#define DB_REC_MATERIAL_REC_HPP

#include <cstdint>

class SFile;

class MaterialRec {
    public:
        int32_t m_ID;
        int32_t m_flags;
        int32_t m_foleySoundID;
        int32_t m_sheatheSoundID;
        int32_t m_unsheatheSoundID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
