#include "glue/CNameGenerator.hpp"
#include "db/Db.hpp"
#include "util/Random.hpp"
#include <storm/String.hpp>
#include <tempest/Random.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

static int32_t CompareDictionaryRecords(const void* a, const void* b) {
    auto recordA = static_cast<const DictionaryRecord*>(a);
    auto recordB = static_cast<const DictionaryRecord*>(b);

    return SStrCmp(recordA->key, recordB->key, sizeof(recordA->key));
}

void CNameGenerator::BuildDictionary() {
    for (uint32_t i = 0; i < this->m_names.Count(); i++) {
        // Names are prefixed with an underscore so that the trigrams that can start a name
        // sort together and can be told apart from the rest
        char buffer[64];
        SStrPrintf(buffer, sizeof(buffer), "_%s", this->m_names[i]);
        SStrLower(buffer);

        int32_t length = static_cast<int32_t>(SStrLen(buffer)) - 1;

        for (int32_t j = 0; j < length; j++) {
            DictionaryRecord record = {};
            SStrCopy(record.key, &buffer[j], sizeof(record.key));
            record.count = 1;

            bool found = false;

            for (uint32_t k = 0; k < this->m_records.Count(); k++) {
                if (!SStrCmp(this->m_records[k].key, record.key, STORM_MAX_STR)) {
                    this->m_records[k].count++;
                    found = true;
                    break;
                }
            }

            if (!found) {
                *this->m_records.New() = record;
            }
        }
    }

    qsort(this->m_records.m_data, this->m_records.Count(), sizeof(DictionaryRecord), &CompareDictionaryRecords);
}

void CNameGenerator::Clear() {
    this->m_records.SetCount(0);
}

void CNameGenerator::Generate(char* name, size_t size) {
    int32_t index = 0;

    memset(name, 0, size);

    if (!this->m_records.Count()) {
        return;
    }

    while (true) {
        int32_t numRecords = this->m_records.Count();

        // Find the run of records that can follow the last two characters of the name so far
        // (or that can start a name when nothing has been generated yet)
        int32_t first = numRecords;
        int32_t last = 0;

        if (!*name) {
            for (int32_t i = 0; i < numRecords; i++) {
                if (this->m_records[i].key[0] == '_') {
                    first = std::min(first, i);
                    last = std::max(last, i);
                } else if (first < last) {
                    break;
                }
            }
        } else {
            size_t length = SStrLen(name);

            for (int32_t i = 0; i < numRecords; i++) {
                if (!SStrCmp(this->m_records[i].key, &name[length - 2], 2)) {
                    first = std::min(first, i);
                    last = std::max(last, i);
                } else if (first < last) {
                    break;
                }
            }
        }

        if (last < first) {
            break;
        }

        // Pick a record from the run, weighted by how often each trigram occurred
        int32_t total = 0;

        for (int32_t i = first; i <= last; i++) {
            total += this->m_records[i].count;
        }

        int32_t roll = total > 0 ? CRandom::dice(total, g_rndSeed) : 0;

        for (int32_t i = first; i <= last; i++) {
            if (roll < this->m_records[i].count) {
                index = i;
                break;
            }

            roll -= this->m_records[i].count;
        }

        auto& record = this->m_records[index];

        if (!*name) {
            SStrCopy(name, record.key, size);
            continue;
        }

        // A trigram with no third character marks the end of a name
        if (!record.key[2]) {
            break;
        }

        SStrPack(name, &record.key[2], size);

        if (SStrLen(name) >= size - 1) {
            break;
        }
    }

    // Drop the leading underscore and capitalize the name
    memmove(name, name + 1, size - 1);
    name[0] = static_cast<char>(toupper(name[0]));
}

void CNameGenerator::Initialize(int32_t raceID, int32_t sexID) {
    this->m_names.SetCount(0);
    this->Clear();

    int32_t numRecords = g_nameGenDB.GetNumRecords();
    int32_t first = -1;

    for (int32_t i = 0; i < numRecords; i++) {
        auto rec = g_nameGenDB.GetRecordByIndex(i);

        if (rec->m_raceID == raceID && rec->m_sexID == sexID) {
            first = i;
            break;
        }
    }

    if (first < 0) {
        return;
    }

    // Records for a race and sex are stored contiguously
    for (int32_t i = first; i < numRecords; i++) {
        auto rec = g_nameGenDB.GetRecordByIndex(i);

        if (rec->m_raceID != raceID || rec->m_sexID != sexID) {
            break;
        }

        *this->m_names.New() = rec->m_name;
    }

    this->BuildDictionary();
}
