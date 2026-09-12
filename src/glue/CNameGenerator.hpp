#ifndef GLUE_C_NAME_GENERATOR_HPP
#define GLUE_C_NAME_GENERATOR_HPP

#include <storm/Array.hpp>
#include <cstdint>
#include <cstddef>

// A trigram of a lower-cased name and how many times it occurred in the source names
struct DictionaryRecord {
    char key[4];
    int32_t count;
};

// Generates random character names by walking trigrams gathered from NameGen.dbc
class CNameGenerator {
    public:
        // Member variables
        TSGrowableArray<const char*> m_names;
        TSGrowableArray<DictionaryRecord> m_records;

        // Member functions
        void BuildDictionary();
        void Clear();
        void Generate(char* name, size_t size);
        void Initialize(int32_t raceID, int32_t sexID);
};

#endif
