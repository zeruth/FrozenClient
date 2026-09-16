#ifndef UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"

// The cinematic surface. FrameXML declares exactly one of these (MovieFrame) with three
// movie-specific script elements, and with the factory returning nullptr the frame did not exist at
// all -- so anything that tried to play a cinematic threw on a nil global.
//
// The frame, its scripts and its Lua surface are implemented here. **Video decoding is not.** There
// is no decoder in this client, so StartMovie reports failure and fires OnMovieFinished, which is
// the path FrameXML already takes for a missing movie file. That keeps the interface working and
// keeps the absence honest rather than pretending a movie played.
class CSimpleMovieFrame : public CSimpleFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        ScriptIx m_onMovieFinished;
        ScriptIx m_onMovieShowSubtitle;
        ScriptIx m_onMovieHideSubtitle;
        bool m_subtitlesEnabled = false;
        float m_offsetX = 0.0f;
        float m_offsetY = 0.0f;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);

        // Member functions
        CSimpleMovieFrame(CSimpleFrame* parent);
        void RunOnMovieFinishedScript();
};

#endif
