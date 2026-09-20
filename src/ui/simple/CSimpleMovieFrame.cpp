#include "ui/simple/CSimpleMovieFrame.hpp"

#include "ui/simple/CSimpleMovieFrameScript.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/simple/MovieDecoder.hpp"
#include "gx/Texture.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>

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

bool CSimpleMovieFrame::StartMovie(const char* path) {
    this->StopMovie();

    if (!path || !*path) {
        return false;
    }

    if (!MovieAviOpen(path, this->m_movie)) {
        return false;
    }

    this->m_decoder = MovieDecoderCreate(this->m_movie);

    if (!this->m_decoder) {
        MovieAviClose(this->m_movie);

        return false;
    }

    // Decode the first picture before anything is shown, so the frame never appears with a texture
    // that has no content behind it.
    if (!MovieDecoderFrame(this->m_decoder, this->m_movie, 0)) {
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
        this->StopMovie();

        return false;
    }

    this->m_surface->SetAllPoints(this, 1);
    this->m_surface->Resize(0);
    this->m_surface->SetTextureHandle(texture);
    this->m_surface->Show();

    this->m_playing = true;

    return true;
}

void CSimpleMovieFrame::StopMovie() {
    this->m_playing = false;
    this->m_elapsed = 0.0f;
    this->m_frame = 0xFFFFFFFF;

    if (this->m_surface) {
        this->m_surface->Hide();
    }

    if (this->m_decoder) {
        MovieDecoderDestroy(this->m_decoder);
        this->m_decoder = nullptr;
    }

    MovieAviClose(this->m_movie);
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

    if (this->m_surface) {
        // The texture rereads the decoder's buffer through the callback.
        this->m_surface->OnRegionChanged();
    }

    return true;
}

void CSimpleMovieFrame::OnLayerUpdate(float elapsedSec) {
    CSimpleFrame::OnLayerUpdate(elapsedSec);

    if (!this->m_playing) {
        return;
    }

    if (!this->AdvanceMovie(elapsedSec)) {
        this->StopMovie();
        this->RunOnMovieFinishedScript();
    }
}
