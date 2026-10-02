#include "common/DataMgr.hpp"
#include <storm/Error.hpp>

void DataMgrGetCoord(HDATAMGR mgr, uint32_t fieldId, C3Vector* coord) {
    auto mgrPtr = reinterpret_cast<CDataMgr*>(mgr);

    STORM_ASSERT(mgrPtr);
    STORM_ASSERT(fieldId < mgrPtr->m_managedArray.Count());
    STORM_ASSERT(mgrPtr->m_managedArray[fieldId]);
    STORM_ASSERT(CBaseManaged::COORD == mgrPtr->m_managedArray[fieldId]->m_dataTypeId);

    auto field = static_cast<TManaged<C3Vector>*>(mgrPtr->m_managedArray[fieldId]);

    if (field->m_flags & CBaseManaged::REQUIRESUPDATE) {
        if (field->m_flags & CBaseManaged::READONLY) {
            // TODO
        } else {
            // TODO
        }
    }

    *coord = field->m_data;
}

// ref: FUN_004c11f0
// A float field's value, or 0 when the field is missing or not a float (the reference's shared
// field lookup, FUN_004c1180, sets error 0x57 then).
float DataMgrGetFloat(HDATAMGR mgr, uint32_t fieldId) {
    auto mgrPtr = reinterpret_cast<CDataMgr*>(mgr);

    if (!mgrPtr || fieldId >= mgrPtr->m_managedArray.Count() || !mgrPtr->m_managedArray[fieldId]
        || mgrPtr->m_managedArray[fieldId]->m_dataTypeId != CBaseManaged::FLOAT) {
        return 0.0f;
    }

    auto field = static_cast<TManaged<float>*>(mgrPtr->m_managedArray[fieldId]);

    return field->m_data;
}

// ref: FUN_004c12b0
// Identified 2026-09-27 after the call-graph matcher bound this address to
// CM2Model::AnimateCamerasST instead -- a callee taking its caller's name, which is the matcher
// failure mode worth knowing. Three things settle it and none of them is position in the queue:
// the reference function's signature is (mgr in ECX, fieldId, C3Vector*, flags byte); it tests
// bit 0x4 of that flags byte to decide whether to keep the EXISTING z rather than the passed one,
// which is this function's per-component keep logic and nothing AnimateCamerasST does; and it is
// 172 bytes ending at 0x004c135c, immediately before DataMgrSetFloat at FUN_004c1360, which was
// already tagged. Adjacent definitions in the same module, in source order.
void DataMgrSetCoord(HDATAMGR mgr, uint32_t fieldId, const C3Vector& coord, uint32_t coordFlags) {
    auto dataMgr = reinterpret_cast<CDataMgr*>(mgr);
    auto typeId = CBaseManaged::COORD;

    C3Vector cur = { 0.0f, 0.0f, 0.0f };
    DataMgrGetCoord(mgr, fieldId, &cur);

    C3Vector setTo = {
        (coordFlags & 0x1) ? cur.x : coord.x,
        (coordFlags & 0x2) ? cur.y : coord.y,
        (coordFlags & 0x4) ? cur.z : coord.z
    };

    STORM_ASSERT(dataMgr);
    STORM_ASSERT(fieldId < dataMgr->m_managedArray.Count());
    STORM_ASSERT(dataMgr->m_managedArray[fieldId]);
    STORM_ASSERT(typeId == dataMgr->m_managedArray[fieldId]->m_dataTypeId);
    STORM_ASSERT(!(dataMgr->m_managedArray[fieldId]->m_flags & CBaseManaged::READONLY));

    auto field = static_cast<TManaged<C3Vector>*>(dataMgr->m_managedArray[fieldId]);

    field->m_updateFcn = nullptr;
    field->m_updateData = nullptr;
    field->m_updatePriority = 0.0f;
    field->Set(setTo);
}

// ref: FUN_004c1360
void DataMgrSetFloat(HDATAMGR mgr, uint32_t fieldId, float val) {
    auto dataMgr = reinterpret_cast<CDataMgr*>(mgr);
    auto typeId = CBaseManaged::FLOAT;

    STORM_ASSERT(dataMgr);
    STORM_ASSERT(fieldId < dataMgr->m_managedArray.Count());
    STORM_ASSERT(dataMgr->m_managedArray[fieldId]);
    STORM_ASSERT(typeId == dataMgr->m_managedArray[fieldId]->m_dataTypeId);
    STORM_ASSERT(!(dataMgr->m_managedArray[fieldId]->m_flags & CBaseManaged::READONLY));

    auto field = static_cast<TManaged<float>*>(dataMgr->m_managedArray[fieldId]);

    field->m_updateFcn = nullptr;
    field->m_updateData = nullptr;
    field->m_updatePriority = 0.0f;
    field->Set(val);
}
