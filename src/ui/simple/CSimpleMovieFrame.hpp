#ifndef UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include "ui/simple/MovieAvi.hpp"

class CSimpleTexture;
struct MovieDecoder;

// The cinematic surface. FrameXML declares exactly one of these (MovieFrame) with three
// movie-specific script elements, and with the factory returning nullptr the frame did not exist at
// all -- so anything that tried to play a cinematic threw on a nil global.
//
// The frame, its scripts, its Lua surface and playback are implemented here. The container is read
// by MovieAvi and the pictures come out of MovieDecoder; this drives the clock, decodes in step
// with it, and keeps the result on a texture region covering the frame.
//
// StartMovie still reports failure and fires OnMovieFinished when a movie cannot be opened or
// decoded, which is the path FrameXML already takes for a missing file.
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

        // Playback
        MovieAvi m_movie;
        MovieDecoder* m_decoder = nullptr;
        CSimpleTexture* m_surface = nullptr;
        bool m_playing = false;
        float m_elapsed = 0.0f;
        uint32_t m_frame = 0xFFFFFFFF;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);

        // Member functions
        CSimpleMovieFrame(CSimpleFrame* parent);
        void RunOnMovieFinishedScript();

        // Open and begin. False when the file is missing, is not an AVI, or carries a codec this
        // client cannot decode -- the caller reports failure to Lua in every one of those cases.
        bool StartMovie(const char* path);

        // Stop and release. Does not run OnMovieFinished; the caller decides whether this was the
        // movie ending or the player skipping it.
        void StopMovie();

        // Step the clock and decode up to the frame it lands on. Returns false when the movie has
        // run out, which is when OnMovieFinished is due.
        bool AdvanceMovie(float elapsedSec);

        virtual void OnLayerUpdate(float elapsedSec);
};

#endif
