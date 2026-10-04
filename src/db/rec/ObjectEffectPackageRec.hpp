// ObjectEffectPackage.dbc -- the effect set a creature or game object display carries.
//
// Two columns of 8 bytes.
#ifndef DB_REC_OBJECT_EFFECT_PACKAGE_REC_HPP
#define DB_REC_OBJECT_EFFECT_PACKAGE_REC_HPP

#include <cstdint>

class SFile;

class ObjectEffectPackageRec {
    public:
        int32_t m_ID;
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
