// MapDifficulty.dbc -- the difficulties a map offers: per map and difficulty, the message shown
// when it cannot be entered, how long a raid lock lasts, how many players it takes, and the
// difficulty's name. Sorted by map (FUN_00634950 walks a map's run).
//
// Twenty-three columns (the message is a localized string); 0x1c bytes in memory.
#ifndef DB_REC_MAP_DIFFICULTY_REC_HPP
#define DB_REC_MAP_DIFFICULTY_REC_HPP

#include <cstdint>

class SFile;

class MapDifficultyRec {
    public:
        int32_t m_ID;
        int32_t m_mapID;
        int32_t m_difficulty;
        const char* m_message;
        int32_t m_raidDuration;
        int32_t m_maxPlayers;
        const char* m_difficultyString;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

// ref: FUN_00634950
// The MapDifficulty row for a map at a difficulty (and its index), or null.
const MapDifficultyRec* MapDifficultyFind(int32_t mapID, int32_t difficulty, int32_t* index);

#endif
