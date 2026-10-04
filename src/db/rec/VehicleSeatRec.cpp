#include "db/rec/VehicleSeatRec.hpp"
#include "util/SFile.hpp"
#include <cstring>

const char* VehicleSeatRec::GetFilename() {
    return "DBFilesClient\\VehicleSeat.dbc";
}

uint32_t VehicleSeatRec::GetNumColumns() {
    return VehicleSeatRec::COLUMN_COUNT;
}

uint32_t VehicleSeatRec::GetRowSize() {
    return VehicleSeatRec::COLUMN_COUNT * 4;
}

bool VehicleSeatRec::NeedIDAssigned() {
    return false;
}

int32_t VehicleSeatRec::GetID() {
    return this->m_ID;
}

void VehicleSeatRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool VehicleSeatRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[VehicleSeatRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_flags = static_cast<int32_t>(columns[1]);
    this->m_attachmentID = static_cast<int32_t>(columns[2]);
    this->m_attachmentOffset[0] = *reinterpret_cast<float*>(&columns[3]);
    this->m_attachmentOffset[1] = *reinterpret_cast<float*>(&columns[4]);
    this->m_attachmentOffset[2] = *reinterpret_cast<float*>(&columns[5]);
    this->m_enterPreDelay = *reinterpret_cast<float*>(&columns[6]);
    this->m_enterSpeed = *reinterpret_cast<float*>(&columns[7]);
    this->m_enterGravity = *reinterpret_cast<float*>(&columns[8]);
    this->m_enterMinDuration = *reinterpret_cast<float*>(&columns[9]);
    this->m_enterMaxDuration = *reinterpret_cast<float*>(&columns[10]);
    this->m_enterMinArcHeight = *reinterpret_cast<float*>(&columns[11]);
    this->m_enterMaxArcHeight = *reinterpret_cast<float*>(&columns[12]);
    this->m_enterAnimStart = static_cast<int32_t>(columns[13]);
    this->m_enterAnimLoop = static_cast<int32_t>(columns[14]);
    this->m_rideAnimStart = static_cast<int32_t>(columns[15]);
    this->m_rideAnimLoop = static_cast<int32_t>(columns[16]);
    this->m_rideUpperAnimStart = static_cast<int32_t>(columns[17]);
    this->m_rideUpperAnimLoop = static_cast<int32_t>(columns[18]);
    this->m_exitPreDelay = *reinterpret_cast<float*>(&columns[19]);
    this->m_exitSpeed = *reinterpret_cast<float*>(&columns[20]);
    this->m_exitGravity = *reinterpret_cast<float*>(&columns[21]);
    this->m_exitMinDuration = *reinterpret_cast<float*>(&columns[22]);
    this->m_exitMaxDuration = *reinterpret_cast<float*>(&columns[23]);
    this->m_exitMinArcHeight = *reinterpret_cast<float*>(&columns[24]);
    this->m_exitMaxArcHeight = *reinterpret_cast<float*>(&columns[25]);
    this->m_exitAnimStart = static_cast<int32_t>(columns[26]);
    this->m_exitAnimLoop = static_cast<int32_t>(columns[27]);
    this->m_exitAnimEnd = static_cast<int32_t>(columns[28]);
    std::memcpy(&this->m_passengerYaw, &columns[29], sizeof(uint32_t) * (VehicleSeatRec::COLUMN_COUNT - 29));

    return true;
}
