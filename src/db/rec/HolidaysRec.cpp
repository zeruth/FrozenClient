#include "db/rec/HolidaysRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* HolidaysRec::GetFilename() {
    return "DBFilesClient\\Holidays.dbc";
}

uint32_t HolidaysRec::GetNumColumns() {
    return HolidaysRec::COLUMN_COUNT;
}

uint32_t HolidaysRec::GetRowSize() {
    return 220;
}

bool HolidaysRec::NeedIDAssigned() {
    return false;
}

int32_t HolidaysRec::GetID() {
    return this->m_ID;
}

void HolidaysRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool HolidaysRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t textureFilenameOfs;
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[0], sizeof(this->m_duration[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[1], sizeof(this->m_duration[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[2], sizeof(this->m_duration[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[3], sizeof(this->m_duration[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[4], sizeof(this->m_duration[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[5], sizeof(this->m_duration[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[6], sizeof(this->m_duration[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[7], sizeof(this->m_duration[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[8], sizeof(this->m_duration[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_duration[9], sizeof(this->m_duration[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[0], sizeof(this->m_date[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[1], sizeof(this->m_date[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[2], sizeof(this->m_date[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[3], sizeof(this->m_date[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[4], sizeof(this->m_date[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[5], sizeof(this->m_date[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[6], sizeof(this->m_date[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[7], sizeof(this->m_date[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[8], sizeof(this->m_date[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[9], sizeof(this->m_date[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[10], sizeof(this->m_date[10]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[11], sizeof(this->m_date[11]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[12], sizeof(this->m_date[12]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[13], sizeof(this->m_date[13]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[14], sizeof(this->m_date[14]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[15], sizeof(this->m_date[15]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[16], sizeof(this->m_date[16]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[17], sizeof(this->m_date[17]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[18], sizeof(this->m_date[18]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[19], sizeof(this->m_date[19]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[20], sizeof(this->m_date[20]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[21], sizeof(this->m_date[21]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[22], sizeof(this->m_date[22]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[23], sizeof(this->m_date[23]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[24], sizeof(this->m_date[24]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_date[25], sizeof(this->m_date[25]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_region, sizeof(this->m_region), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_looping, sizeof(this->m_looping), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[0], sizeof(this->m_calendarFlags[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[1], sizeof(this->m_calendarFlags[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[2], sizeof(this->m_calendarFlags[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[3], sizeof(this->m_calendarFlags[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[4], sizeof(this->m_calendarFlags[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[5], sizeof(this->m_calendarFlags[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[6], sizeof(this->m_calendarFlags[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[7], sizeof(this->m_calendarFlags[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[8], sizeof(this->m_calendarFlags[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFlags[9], sizeof(this->m_calendarFlags[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_holidayNameID, sizeof(this->m_holidayNameID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_holidayDescriptionID, sizeof(this->m_holidayDescriptionID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &textureFilenameOfs, sizeof(textureFilenameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_priority, sizeof(this->m_priority), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_calendarFilterType, sizeof(this->m_calendarFilterType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_textureFilename = stringBuffer ? &stringBuffer[textureFilenameOfs] : "";

    return true;
}
