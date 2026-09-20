#ifndef UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_MOVIE_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include "ui/simple/MovieAvi.hpp"

#include <string>
#include <vector>

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

        // The soundtrack. The AVI's audio stream is a bare MP3 elementary stream, which FMOD
        // decodes itself -- so this needs no decoder of its own, and works on every platform the
        // sound backend already covers.
        void* m_sound = nullptr;
        void* m_channel = nullptr;

        // One line of the .sbt beside the movie. The reference keeps a 12-byte record per caption
        // -- start, end, text -- and its own struct is named MOVIECAPTION.
        struct MovieCaption {
            int32_t start;
            int32_t end;
            std::string text;
        };

        std::vector<MovieCaption> m_captions;

        // Which caption is on screen, or -1 for none. The reference starts this at -1 too.
        int32_t m_caption = -1;
        CSimpleTexture* m_surface = nullptr;

        // The surface's texture, kept so a decoded frame can be pushed to the device each step.
        // Not owned here -- the region closes it.
        void* m_texture = nullptr;
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
        //
        // volume is 0..255, the range the reference's usage string names.
        bool StartMovie(const char* path, int32_t volume);

        // Stop and release. Does not run OnMovieFinished; the caller decides whether this was the
        // movie ending or the player skipping it.
        void StopMovie();

        // Step the clock and decode up to the frame it lands on. Returns false when the movie has
        // run out, which is when OnMovieFinished is due.
        bool AdvanceMovie(float elapsedSec);

        virtual void OnLayerUpdate(float elapsedSec);

    private:
        void StartMovieAudio(int32_t volume);
        void StopMovieAudio();

        // Read "<movie>.sbt" if there is one. Silence is not an error: most movies have none, and
        // the reference does not complain either.
        void LoadCaptions(const char* basePath);

        // Show or hide the caption for the current time, firing the frame's subtitle scripts on
        // each change the way the reference does.
        void UpdateCaption();
};

#endif
