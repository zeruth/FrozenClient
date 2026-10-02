#ifndef DB_REC_WMO_AREA_TABLE_REC_HPP
#define DB_REC_WMO_AREA_TABLE_REC_HPP

#include <cstdint>

class SFile;

// WMOAreaTable.dbc: which zone a group of a building belongs to, keyed by the building's WMO id,
// its name set and the group's WMOGroupID. 28 columns: eleven integers and the localised name.
class WMOAreaTableRec {
    public:
        int32_t m_ID;
        int32_t m_wmoID;
        int32_t m_nameSetID;
        int32_t m_wmoGroupID;
        int32_t m_soundProviderPref;
        int32_t m_soundProviderPrefUnderwater;
        int32_t m_ambienceID;
        int32_t m_zoneMusic;
        int32_t m_introSound;
        int32_t m_flags;
        int32_t m_areaTableID;
        const char* m_areaName = nullptr;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
