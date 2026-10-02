#ifndef GX_TEXTURE_HPP
#define GX_TEXTURE_HPP

#include "gx/Types.hpp"
#include "gx/texture/CGxTex.hpp"
#include "gx/texture/CTexture.hpp"

typedef HOBJECT HTEXTURE;

typedef void (TEXTURE_CALLBACK)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&);

class C2Vector;
class CImVector;
class CStatus;
class SFile;

// The module's shared state. Reference addresses are given on the definitions in Texture.cpp.
namespace Texture {
    extern int32_t s_createBlpAsync;
    extern MipBits* s_mipBits;
    extern int32_t s_mipBitsValid;
    extern int32_t s_asyncBytesInFlight;
    extern int32_t s_gxTexCacheSize;
    extern int32_t s_gxTexCacheBudget;
    extern uint32_t s_gxTexCacheTime;
    extern STORM_EXPLICIT_LIST(CTexture, m_link) s_textureList;
}

// The async read callbacks a texture installs, and the cleanup a cancelled read gets.
void AsyncTextureCleanup(CAsyncObject* object);

void AsyncTextureStartRead(CAsyncObject* object, int32_t a2);

void AsyncTextureWait(CTexture*);

int32_t FileError(CStatus* status, const char* kind, const char* fileName);

int32_t FindSubstitution(char* dest, const char* fileName);

MipBits* GetDefaultTexture(uint32_t width, uint32_t height);

int32_t GetBlpMips(char* fileExt, const char* fileName, int32_t openFlag, MipBits** images, uint32_t* width, uint32_t* height, EGxTexFormat* gxTexFormat, int32_t* isOpaque, uint32_t* alphaBits, PIXEL_FORMAT* pixFormat);

void GetTextureFormats(PIXEL_FORMAT* pixFormat, EGxTexFormat* gxTexFormat, PIXEL_FORMAT preferredFormat, int32_t alphaBits);

void GxTexCacheFlush();

uint32_t GxTexMemSize(EGxTexFormat format, uint32_t width, uint32_t filter, uint32_t height);

uint32_t PixelFormatBlockBytes(PIXEL_FORMAT format);

int32_t ReloadMips(char* fileName, int32_t openFlag, MipBits** images);

void RequestImageDimensions(uint32_t* width, uint32_t* height, uint32_t* bestMip);

void TextureDestroy();

void TextureFlushGxTexCache();

int32_t TextureGetAtlasCoords(HTEXTURE handle, C2Vector* offset, float* scale);

int32_t TextureGetDefaultCacheSize();

const char* TextureGetFilename(HTEXTURE handle);

int32_t TextureHasGxTexCallback(HTEXTURE handle);

int32_t TextureIsAtlased(HTEXTURE handle);

int32_t TextureIsGxTexUploaded(HTEXTURE handle);

int32_t TextureIsLoaded(HTEXTURE handle);

int32_t TextureLoadBlob(const char* fileName);

SFile* TextureOpenFile(const char* fileName, int32_t openFlag);

void TextureReloadAtlases();

void TextureSetAtlasEnable(int32_t enable);

void TextureSetCacheSize(int32_t size);

void TextureSetFilterMode(int32_t mode);

void TextureSetMaxAnisotropy(uint32_t maxAnisotropy);

void TextureSetSubstitution(const char* name, const char* replacement);

void TextureSetUpdateCallback(HTEXTURE handle, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), void* userArg);

void TextureTrimGxTexCache();

int32_t TextureUnloadBlob(const char* fileName);

HTEXTURE TextureCreateFromBlob(const char* fileName, CGxTexFlags texFlags, CStatus* status, int32_t useFilterMode);

// Bits per pixel for a BLP pixel format. Zero for a format with no fixed size.
uint32_t PixelFormatBitsPerPixel(PIXEL_FORMAT);

// The byte size of one mip level of a `width` x `height` image in `format`.
uint32_t PixelFormatLevelSize(uint32_t level, uint32_t width, uint32_t height, PIXEL_FORMAT);

// The total bytes of `levelCount` mip levels of a `width` x `height` image in this format.
uint32_t PixelFormatChainSize(uint32_t levelCount, uint32_t width, uint32_t height, PIXEL_FORMAT);

// Allocate a whole mip chain and return its pointer table. The data is 16-byte ALIGNED, which is
// the difference between this and BuildMipLevelPointers below.
void** AllocMipChain(PIXEL_FORMAT, uint32_t width, uint32_t height, const char* fileName,
                     int32_t lineNo);

// Fill a mip pointer table IN PLACE: `levels` is the head of a buffer whose pointer table is
// followed immediately by the level data, and each entry is pointed at its own level.
void BuildMipLevelPointers(PIXEL_FORMAT, uint32_t width, uint32_t height, void** levels);

uint32_t CalcLevelCount(uint32_t, uint32_t);

// The same mip count WITHOUT the cube-map strip fold. A separate reference function, not a
// duplicate -- see the note at the definition.
uint32_t CalcLevelCountFlat(uint32_t, uint32_t);

uint32_t CalcLevelOffset(uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t CalcLevelSize(uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t GetBitDepth(uint32_t);

uint32_t GxCalcTexelStrideInBytes(EGxTexFormat, uint32_t);

int32_t GxTexCreate(CGxTexParms const&, CGxTex*&);

int32_t GxTexCreate(EGxTexTarget, uint32_t, uint32_t, uint32_t, EGxTexFormat, EGxTexFormat, CGxTexFlags, void*, void (*)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char*, CGxTex*&);

// A square-or-not power-of-two 2D texture with the same format in memory and on the device.
int32_t GxTexCreate(uint32_t width, uint32_t height, EGxTexFormat format, CGxTexFlags flags, void* userArg, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), CGxTex*& texId);

void GxTexDestroy(CGxTex* texId);

void GxTexParameters(const CGxTex* texId, CGxTexParms& parms);

bool GxTexReusable(const CGxTexParms&);

bool GxTexReusable(const CGxTex* texId);

int32_t GxTexHasCallback(const CGxTex* texId);

int32_t GxTexIsUploaded(const CGxTex* texId);

void GxTexSetCallback(CGxTex* texId, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), void* userArg);

void GxTexSetDataFormat(CGxTex* texId, EGxTexFormat dataFormat);

void GxTexSetFlags(CGxTex* texId, CGxTexFlags flags);

void GxTexSetWrap(CGxTex* texId, EGxTexWrapMode wrapU, EGxTexWrapMode wrapV);

void GxTexUpdate(CGxTex*, int32_t, int32_t, int32_t, int32_t, int32_t);

void GxTexUpdate(CGxTex*, CiRect&, int32_t);

TEXTURE_CALLBACK GxuUpdateSingleColorTexture;

MipBits* MippedImgAllocA(uint32_t, uint32_t, uint32_t, const char*, int32_t);

uint32_t MippedImgCalcSize(uint32_t, uint32_t, uint32_t);

void MippedImgSet(MipBits* images, uint32_t fourCC, uint32_t width, uint32_t height);

CGxTex* TextureAllocGxTex(EGxTexTarget, uint32_t, uint32_t, uint32_t, EGxTexFormat, CGxTexFlags, void*, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), EGxTexFormat);

MipBits* TextureAllocMippedImg(PIXEL_FORMAT pixelFormat, uint32_t width, uint32_t height);

// Give a mip chain back. Chains of the cached formats and sizes are kept for the next
// TextureAllocMippedImg of the same shape; anything else is freed.
void TextureFreeMippedImg(MipBits* image, PIXEL_FORMAT pixelFormat, uint32_t width, uint32_t height);

// Read an image file into a fresh mip chain: the name's .blp first, then its .tga. A null
// `dataFormat` or PIXEL_UNSPECIFIED lets a BLP choose from its alpha depth.
MipBits* TextureLoadImage(const char* filename, uint32_t* width, uint32_t* height, PIXEL_FORMAT* dataFormat, int32_t* isOpaque, class CStatus* status, uint32_t* alphaBits, int32_t openFlag);

HTEXTURE TextureCacheGetTexture(char*, char*, CGxTexFlags);

HTEXTURE TextureCacheGetTexture(const CImVector&);

void TextureCacheNewTexture(CTexture*, CGxTexFlags);

// Re-insert a texture under its own name and flags (the device-reset path of the liquids).
void TextureCacheNewTexture(CTexture*);

void TextureCacheNewTexture(CTexture*, const CImVector&);

uint32_t TextureCalcMipCount(uint32_t width, uint32_t height);

HTEXTURE TextureCreate(const char*, CGxTexFlags, CStatus*, int32_t);

HTEXTURE TextureCreate(uint32_t, uint32_t, EGxTexFormat, EGxTexFormat, CGxTexFlags, void*, void (*)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char*, int32_t);

HTEXTURE TextureCreate(EGxTexTarget, uint32_t, uint32_t, uint32_t, EGxTexFormat, EGxTexFormat, CGxTexFlags, void*, void (*)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char*, int32_t);

HTEXTURE TextureCreateSolid(const CImVector&);
// A texture the client GENERATED, fetched by the name it was registered under rather than read off
// disk. ref: FUN_004b6f30
HTEXTURE TextureCacheGetProcedural(char* name);

int32_t TextureGetDimensions(HTEXTURE, uint32_t*, uint32_t*, int32_t);

void TextureIncreasePriority(CTexture*);

void TextureInitialize(void);

int32_t TextureIsSame(HTEXTURE textureHandle, const char* fileName);

void TextureFreeGxTex(CGxTex* texId, const char* name = nullptr);

CGxTex* TextureGetGxTex(CTexture*, int32_t, CStatus*);

CGxTex* TextureGetGxTex(HTEXTURE, int32_t, CStatus*);

// Whether the texture's own data carries an alpha channel. ref: FUN_004b54f0
int32_t TextureHasAlpha(HTEXTURE handle);

CTexture* TextureGetTexturePtr(HTEXTURE);

void TextureFreeMem(void* ptr);

void TextureGetTexFlags(HTEXTURE handle, CGxTexFlags* flags);
// Whether the texture has a read in flight and somewhere to put it -- an async object and a gx
// texture both. A name that resolved to no file gets neither, so this is how a caller walking a
// numbered sequence finds out where the sequence stops. Named for what it tests; the reference
// gives it no name. ref: FUN_004b5800
int32_t TextureHasPendingData(HTEXTURE handle);

int32_t TextureGetRefCount(HTEXTURE handle);

#endif
