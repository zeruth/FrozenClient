#ifndef DB_REC_EMOTES_REC_HPP
#define DB_REC_EMOTES_REC_HPP

#include <cstdint>

class SFile;

// Emotes.dbc: each emote (including the looping "state" emotes stored in UNIT_NPC_EMOTESTATE) maps
// to an M2 animation via m_animID (an AnimationData.dbc id, which is the same id SetBoneSequence
// resolves). Lets an idle NPC hold its scripted pose (sit, kneel, talk, ...) like the reference.
class EmotesRec {
    public:
        int32_t m_ID;
        int32_t m_nameOffset;   // string ref (unused for rendering)
        int32_t m_animID;       // AnimationData.dbc id = M2 animation id
        int32_t m_flags;
        int32_t m_specProc;
        int32_t m_specParam;
        int32_t m_soundID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
