// ObjectEffectPackageElem.dbc -- one group of a package and the object state that turns it on.
//
// Four columns of 16 bytes.
#ifndef DB_REC_OBJECT_EFFECT_PACKAGE_ELEM_REC_HPP
#define DB_REC_OBJECT_EFFECT_PACKAGE_ELEM_REC_HPP

#include <cstdint>

class SFile;

class ObjectEffectPackageElemRec {
    public:
        int32_t m_ID;
        int32_t m_objectEffectPackageID;
        int32_t m_objectEffectGroupID;
        int32_t m_stateType;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
