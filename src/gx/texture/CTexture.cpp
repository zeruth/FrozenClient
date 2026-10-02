#include "gx/texture/CTexture.hpp"
#include "gx/Texture.hpp"
#include "gx/texture/CTextureAtlas.hpp"
#include <storm/Memory.hpp>

EGxTexFilter CTexture::s_filterMode = GxTex_LinearMipNearest;
int32_t CTexture::s_maxAnisotropy = 1;

// Inlined into TSHashTable<CTexture, HASHKEY_TEXTUREFILE>::Ptr (FUN_004b6d90): the names compare
// case-blind over a path's length, and the flags only matter when neither side asked for a match on
// the name alone.
bool HASHKEY_TEXTUREFILE::operator==(const HASHKEY_TEXTUREFILE& key) {
    if (SStrCmpI(this->m_filename, key.m_filename, STORM_MAX_PATH)) {
        return false;
    }

    if ((this->m_anyFlags & 0x1) || (key.m_anyFlags & 0x1)) {
        return true;
    }

    return this->m_texFlags == key.m_texFlags;
}

// ref: FUN_004b8770
// Every texture joins the list of live textures (reference 0x00ac3348) as it is made, so that
// TextureDestroy can name any that were never released.
CTexture::CTexture() {
    this->filename[0] = '\0';

    Texture::s_textureList.LinkToTail(this);
}

// ref: FUN_004b8830
CTexture::~CTexture() {
    if (this->gxTex) {
        TextureFreeGxTex(this->gxTex, this->filename);
    }

    if (this->atlas) {
        CTextureAtlas::Free(this->atlas, this);
    }

    if (this->asyncObject) {
        if (this->flags & 0x20) {
            // A streaming read is never cancelled: the object is left to finish, and to clean
            // itself up when it does.
            this->asyncObject->userArg = this->asyncObject;
            this->asyncObject->userFailedCallback = reinterpret_cast<ASYNC_CALLBACK>(&AsyncTextureCleanup);
            this->asyncObject->userPostloadCallback = this->asyncObject->userFailedCallback;
        } else {
            void* buffer = this->asyncObject->buffer;

            if (AsyncFileReadCancel(this->asyncObject, &AsyncTextureCleanup)) {
                // The reference takes the size off the in-flight total whether or not the read
                // was ever started (a deferred read has no buffer and was never added to it).
                Texture::s_asyncBytesInFlight -= this->asyncObject->size;
                SMemFree(buffer, __FILE__, __LINE__, 0);
            }
        }
    }

    // m_link, loadStatus and the hash links unlink and tear down as members, in the reference's
    // order.
}
