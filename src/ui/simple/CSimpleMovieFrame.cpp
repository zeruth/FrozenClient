#include "ui/simple/CSimpleMovieFrame.hpp"

#include "ui/simple/CSimpleMovieFrameScript.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/simple/MovieDecoder.hpp"
#include "gx/Texture.hpp"
#include "util/CStatus.hpp"
#include "sound/SESound.hpp"
#include "util/SFile.hpp"
#include <fmod.hpp>
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <cstdio>

namespace {

// Hands the decoder's current picture to the texture when the device asks for it. The decode
// happens on the clock in AdvanceMovie; this only points at what is already there.
void MovieTextureCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel,
                          void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (cmd != GxTex_Latch) {
        return;
    }

    auto frame = static_cast<CSimpleMovieFrame*>(userArg);
    auto pixels = MovieDecoderPixels(frame->m_decoder);

    if (!pixels) {
        return;
    }

    texelStrideInBytes = static_cast<uint32_t>(MovieDecoderWidth(frame->m_decoder)) * 4;
    texels = pixels;
}

} // namespace

int32_t CSimpleMovieFrame::s_metatable;
int32_t CSimpleMovieFrame::s_objectType;

void CSimpleMovieFrame::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleMovieFrame::RegisterScriptMethods);
    CSimpleMovieFrame::s_metatable = ref;
}

int32_t CSimpleMovieFrame::GetObjectType() {
    if (!CSimpleMovieFrame::s_objectType) {
        CSimpleMovieFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleMovieFrame::s_objectType;
}

void CSimpleMovieFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleMovieFrameMethods,
                                              NUM_SIMPLE_MOVIE_FRAME_SCRIPT_METHODS);
}

CSimpleMovieFrame::CSimpleMovieFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
}

FrameScript_Object::ScriptIx* CSimpleMovieFrame::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnMovieFinished")) {
        return &this->m_onMovieFinished;
    }

    if (!SStrCmpI(name, "OnMovieShowSubtitle")) {
        data.wrapper = "return function(self, text) %s end";
        return &this->m_onMovieShowSubtitle;
    }

    if (!SStrCmpI(name, "OnMovieHideSubtitle")) {
        return &this->m_onMovieHideSubtitle;
    }

    return CSimpleFrame::GetScriptByName(name, data);
}

int32_t CSimpleMovieFrame::GetScriptMetaTable() {
    return CSimpleMovieFrame::s_metatable;
}

bool CSimpleMovieFrame::IsA(int32_t type) {
    return type == CSimpleMovieFrame::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

void CSimpleMovieFrame::RunOnMovieFinishedScript() {
    if (this->m_onMovieFinished.luaRef) {
        this->RunScript(this->m_onMovieFinished, 0, nullptr);
    }
}

bool CSimpleMovieFrame::StartMovie(const char* path, int32_t volume) {
    this->StopMovie();

    if (!path || !*path) {
        return false;
    }

    if (!MovieAviOpen(path, this->m_movie)) {
        fprintf(stderr, "Movie: could not demux %s\n", path);

        return false;
    }

    this->m_decoder = MovieDecoderCreate(this->m_movie);

    if (!this->m_decoder) {
        fprintf(stderr, "Movie: no decoder for %s (codec %s, %dx%d, %u frames)\n",
                path, MovieAviFourCC(this->m_movie.videoCodec),
                this->m_movie.width, this->m_movie.height, this->m_movie.videoChunkCount);

        MovieAviClose(this->m_movie);

        return false;
    }

    // Decode the first picture before anything is shown, so the frame never appears with a texture
    // that has no content behind it.
    if (!MovieDecoderFrame(this->m_decoder, this->m_movie, 0)) {
        fprintf(stderr, "Movie: first frame failed to decode in %s\n", path);
        this->StopMovie();

        return false;
    }

    this->m_frame = 0;
    this->m_elapsed = 0.0f;

    // The surface is a texture region covering the frame, so the movie is composited by the same
    // path as every other piece of interface art rather than by a special case in the renderer.
    if (!this->m_surface) {
        auto m = SMemAlloc(sizeof(CSimpleTexture), __FILE__, __LINE__, 0x0);

        this->m_surface = new (m) CSimpleTexture(this, DRAWLAYER_ARTWORK, 1);
    }

    if (!this->m_surface) {
        fprintf(stderr, "Movie: could not make the surface region for %s\n", path);
        this->StopMovie();

        return false;
    }

    CGxTexFlags flags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

    auto texture = TextureCreate(
        static_cast<uint32_t>(MovieDecoderWidth(this->m_decoder)),
        static_cast<uint32_t>(MovieDecoderHeight(this->m_decoder)),
        GxTex_Argb8888,
        GxTex_Argb8888,
        flags,
        this,
        MovieTextureCallback,
        "Movie",
        0
    );

    if (!texture) {
        fprintf(stderr, "Movie: could not create a %dx%d texture for %s\n",
                MovieDecoderWidth(this->m_decoder), MovieDecoderHeight(this->m_decoder), path);
        this->StopMovie();

        return false;
    }

    this->m_surface->SetAllPoints(this, 1);
    this->m_surface->Resize(0);
    this->m_surface->SetTextureHandle(texture);
    this->m_surface->Show();

    // Kept so each decoded frame can be pushed to the device; the region owns the handle.
    this->m_texture = texture;

    this->StartMovieAudio(volume);

    // Captions are keyed off the movie's own name, so they are read from the path without the
    // extension the caller appended.
    char base[512];
    SStrCopy(base, path, sizeof(base));

    size_t baseLength = SStrLen(base);

    if (baseLength > 4) {
        base[baseLength - 4] = '\0';
    }

    this->LoadCaptions(base);

    this->m_playing = true;

    return true;
}

// The soundtrack, played straight from the buffer the demuxer already separated.
//
// FMOD decodes MP3 itself, so the audio needs nothing vendored and nothing platform specific --
// wherever the sound backend runs, this runs. A movie with no audio track, or a client whose sound
// failed to start, simply plays silent: the video is not held up for it.
void CSimpleMovieFrame::StartMovieAudio(int32_t volume) {
    if (!this->m_movie.audioData || !this->m_movie.audioSize) {
        return;
    }

    auto system = SESound::s_pGameSystem;

    if (!SESound::IsInitialized() || !system) {
        return;
    }

    FMOD_CREATESOUNDEXINFO info = {};
    info.cbsize = sizeof(info);
    info.length = this->m_movie.audioSize;

    FMOD::Sound* sound = nullptr;

    // OPENMEMORY rather than a stream: the bytes are already resident, and a stream would read
    // from a buffer this frame owns and could outlive.
    FMOD_MODE mode = FMOD_OPENMEMORY | FMOD_CREATESAMPLE | FMOD_LOOP_OFF | FMOD_2D;

    if (system->createSound(reinterpret_cast<const char*>(this->m_movie.audioData), mode, &info, &sound) != FMOD_OK) {
        return;
    }

    FMOD::Channel* channel = nullptr;

    if (system->playSound(sound, nullptr, false, &channel) != FMOD_OK) {
        sound->release();

        return;
    }

    if (channel) {
        // The interface counts volume 0..255; FMOD wants 0..1.
        float level = static_cast<float>(volume) / 255.0f;

        channel->setVolume(level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level));
    }

    this->m_sound = sound;
    this->m_channel = channel;
}

// ref: FUN_0095e880
//
// The format, from the shipped files and the parse that reads them:
//
//   HH:MM:SS:FF - HH:MM:SS:FF      a line beginning with '0'
//   the caption text               the line after it
//   (blank)
//
// Fields are read at FIXED OFFSETS, not by scanning for colons: 0, 3, 6, 9 for the start and 14,
// 17, 20, 23 for the end, which is what the reference's contiguous two-byte buffers amount to. A
// line that does not begin with '0' is skipped, which is how the blank separators and the UTF-8
// BOM line are passed over.
//
// The fourth field is multiplied by 41, NOT by 10. That is the reference's own arithmetic
// (0x29), and it reads as frames at 24fps -- 1000/24 is 41.67 -- even though the shipped files
// put values above 23 there, which only makes sense as hundredths. Kept as the reference has it
// rather than "corrected": a caption that appears a beat late in both clients is parity, and a
// caption that appears at a different time from the reference is not. If the timing ever needs
// changing, this is the line and this is why.
void CSimpleMovieFrame::LoadCaptions(const char* basePath) {
    this->m_captions.clear();
    this->m_caption = -1;

    char path[512];
    SStrPrintf(path, sizeof(path), "%s.sbt", basePath);

    void* raw = nullptr;
    size_t size = 0;

    if (!SFile::Load(nullptr, path, &raw, &size, 1, SFILE_OPEN_ALLOW_LOCAL, nullptr) || !raw) {
        return;
    }

    auto text = static_cast<char*>(raw);

    // Skip the UTF-8 byte order mark. The reference tests the first byte alone.
    if (size >= 3 && static_cast<uint8_t>(text[0]) == 0xEF) {
        text += 3;
    }

    auto field = [](const char* line, size_t length, size_t at) -> int32_t {
        if (at + 2 > length) {
            return 0;
        }

        return (line[at] - '0') * 10 + (line[at + 1] - '0');
    };

    char* cursor = text;

    while (cursor && *cursor) {
        char* line = cursor;

        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }

        size_t length = static_cast<size_t>(cursor - line);

        while (*cursor == '\r' || *cursor == '\n') {
            *cursor = '\0';
            cursor++;
        }

        if (length < 24 || line[0] != '0') {
            continue;
        }

        MovieCaption caption;
        caption.start = field(line, length, 9) * 41
            + (field(line, length, 6) + (field(line, length, 3) + field(line, length, 0) * 60) * 60) * 1000;
        caption.end = field(line, length, 23) * 41
            + (field(line, length, 20) + (field(line, length, 17) + field(line, length, 14) * 60) * 60) * 1000;

        // The text is the line after the timings.
        char* body = cursor;

        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }

        size_t bodyLength = static_cast<size_t>(cursor - body);

        while (*cursor == '\r' || *cursor == '\n') {
            *cursor = '\0';
            cursor++;
        }

        caption.text.assign(body, bodyLength);

        this->m_captions.push_back(caption);
    }

    SFile::Unload(raw);
}

// ref: the caption half of the movie's per-frame work
//
// OnMovieShowSubtitle carries the text; OnMovieHideSubtitle takes none. FrameXML fades the caption
// in and out on those two, so they have to fire on the change and not every frame.
void CSimpleMovieFrame::UpdateCaption() {
    if (this->m_captions.empty()) {
        return;
    }

    auto now = static_cast<int32_t>(this->m_elapsed * 1000.0f);
    int32_t wanted = -1;

    for (size_t i = 0; i < this->m_captions.size(); i++) {
        const MovieCaption& caption = this->m_captions[i];

        if (now >= caption.start && now < caption.end) {
            wanted = static_cast<int32_t>(i);

            break;
        }
    }

    if (wanted == this->m_caption) {
        return;
    }

    this->m_caption = wanted;

    if (wanted < 0) {
        if (this->m_onMovieHideSubtitle.luaRef) {
            this->RunScript(this->m_onMovieHideSubtitle, 0, nullptr);
        }

        return;
    }

    if (this->m_onMovieShowSubtitle.luaRef) {
        this->RunScript(this->m_onMovieShowSubtitle, 0, this->m_captions[wanted].text.c_str());
    }
}

void CSimpleMovieFrame::StopMovieAudio() {
    if (this->m_channel) {
        static_cast<FMOD::Channel*>(this->m_channel)->stop();
        this->m_channel = nullptr;
    }

    if (this->m_sound) {
        static_cast<FMOD::Sound*>(this->m_sound)->release();
        this->m_sound = nullptr;
    }
}

// ref: FUN_0095eba0
//
// The whole body is guarded on "was it playing", and that guard is the only thing standing between
// this and infinite recursion: the script this runs at the end is OnMovieFinished, and FrameXML
// answers that with MovieFrame_PlayNextMovie, which calls StopMovie straight back. The second call
// sees the flag already cleared and does nothing.
//
// So the order matters as much as the guard -- the flag is cleared BEFORE the script runs, exactly
// as the reference clears its +0x2a0 before running its +0x3b4.
void CSimpleMovieFrame::StopMovie() {
    if (!this->m_playing) {
        return;
    }

    this->StopMovieAudio();

    // Take the last caption off the screen before the frame goes, so nothing is left behind.
    if (this->m_caption >= 0 && this->m_onMovieHideSubtitle.luaRef) {
        this->RunScript(this->m_onMovieHideSubtitle, 0, nullptr);
    }

    this->m_captions.clear();
    this->m_caption = -1;

    this->m_playing = false;
    this->m_elapsed = 0.0f;
    this->m_frame = 0xFFFFFFFF;

    if (this->m_surface) {
        this->m_surface->Hide();
    }

    // The region owns the texture and closes it when the next is set, so this only forgets it.
    this->m_texture = nullptr;

    if (this->m_decoder) {
        MovieDecoderDestroy(this->m_decoder);
        this->m_decoder = nullptr;
    }

    MovieAviClose(this->m_movie);

    // What lets the sequence move on. Pressing SPACE or ENTER reaches StopMovie through the
    // binding and nothing else would advance the movie list; without this the picture simply
    // stopped and sat there, which is what it did.
    this->RunOnMovieFinishedScript();
}

bool CSimpleMovieFrame::AdvanceMovie(float elapsedSec) {
    if (!this->m_playing || !this->m_decoder) {
        return false;
    }

    this->m_elapsed += elapsedSec;

    float rate = this->m_movie.frameRate > 0.0f ? this->m_movie.frameRate : 24.0f;
    auto target = static_cast<uint32_t>(this->m_elapsed * rate);

    if (target >= this->m_movie.videoChunkCount) {
        return false;
    }

    // Every frame but a keyframe is coded against the one before it, so a late frame cannot be
    // reached by jumping -- the ones in between have to be decoded even though they are not shown.
    // Capped so a long stall (a device reset, a loading hitch) cannot spend the whole frame
    // catching up; the clock is pulled back to match rather than the picture running ahead.
    const uint32_t MAX_CATCHUP = 4;

    uint32_t behind = target - this->m_frame;

    if (behind > MAX_CATCHUP) {
        target = this->m_frame + MAX_CATCHUP;
        this->m_elapsed = static_cast<float>(target) / rate;
    }

    while (this->m_frame != target) {
        uint32_t next = this->m_frame + 1;

        if (!MovieDecoderFrame(this->m_decoder, this->m_movie, next)) {
            return false;
        }

        this->m_frame = next;
    }

    // Push the new picture to the device.
    //
    // A callback texture is latched ONCE, when it is created. OnRegionChanged only re-lays-out the
    // region -- it does not ask the device for the pixels again, so every frame after the first
    // kept showing the first. That is why the movie played its sound over a still black image:
    // frame 0 of these cinematics IS black, and it was the only frame ever uploaded.
    //
    // GxTexUpdate is what re-runs the callback. Immediate, because the next thing that happens is
    // the frame being drawn.
    if (this->m_texture) {
        auto texture = TextureGetTexturePtr(static_cast<HTEXTURE>(this->m_texture));

        if (texture) {
            CStatus status;
            auto gxTex = TextureGetGxTex(texture, 0, &status);

            if (gxTex) {
                GxTexUpdate(gxTex, 0, 0, MovieDecoderWidth(this->m_decoder),
                            MovieDecoderHeight(this->m_decoder), 1);
            }
        }
    }

    if (this->m_subtitlesEnabled) {
        this->UpdateCaption();
    }

    return true;
}

// DIVERGENCE, asked for. FrameXML's MovieFrame_OnKeyUp answers ESCAPE, SPACE and ENTER and
// ignores everything else, so most of the keyboard did nothing during a cinematic. These two make
// any key and any mouse button behave the way SPACE does -- stop the current movie, which advances
// to the next one and leaves the movie screen after the last.
//
// Deliberately not ESCAPE's behaviour: that hides the frame and abandons the whole sequence, which
// would make a stray keypress skip every remaining movie rather than the one playing.
int32_t CSimpleMovieFrame::OnLayerKeyDown(const CKeyEvent& evt) {
    if (this->m_playing) {
        this->StopMovie();

        return 1;
    }

    return CSimpleFrame::OnLayerKeyDown(evt);
}

int32_t CSimpleMovieFrame::OnLayerMouseDown(const CMouseEvent& evt, const char* btn) {
    if (this->m_playing) {
        this->StopMovie();

        return 1;
    }

    return CSimpleFrame::OnLayerMouseDown(evt, btn);
}

void CSimpleMovieFrame::OnLayerUpdate(float elapsedSec) {
    CSimpleFrame::OnLayerUpdate(elapsedSec);

    if (!this->m_playing) {
        return;
    }

    // StopMovie runs OnMovieFinished itself, so the end of a movie and a skip take the same
    // path. Firing it here too would deliver it twice.
    if (!this->AdvanceMovie(elapsedSec)) {
        this->StopMovie();
    }
}
