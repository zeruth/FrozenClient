#include "world/map/CMapArea.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/CWorldScene.hpp"
#include "async/AsyncFile.hpp"
#include "async/CAsyncObject.hpp"
#include "gx/Gx.hpp"
#include "gx/Texture.hpp"
#include "gx/texture/CGxTex.hpp"
#include "util/SFile.hpp"
#include <common/Handle.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Vector.hpp>

static const char* AREA_TEXTURE_TAG = ".?AVCMapAreaTexture@@";

// ref: FUN_007d7050
CMapArea::CMapArea() {
    // Every field starts at zero; the reference constructor writes them out one by one
}

// ref: FUN_007d6e10
CMapArea::~CMapArea() {
    if (this->m_liquid) {
        SMemFree(this->m_liquid, __FILE__, __LINE__, 0x0);
        this->m_liquid = nullptr;
    }

    if (this->m_fileBuffer) {
        CMap::MapMemFree(this->m_fileBuffer);
    }

    this->m_unk8c = 0;
    this->m_doodadDefs = nullptr;
    this->m_mapObjDefs = nullptr;
    this->m_chunkInfo = nullptr;
    this->m_doodadDefCount = 0;
    this->m_mapObjDefCount = 0;
    this->m_doodadNames = nullptr;
    this->m_mapObjNames = nullptr;
    this->m_doodadNameOffsets = nullptr;
    this->m_mapObjNameOffsets = nullptr;

    this->m_chunkLinkList.UnlinkAll();

    if (this->m_textures) {
        SMemFree(this->m_textures, AREA_TEXTURE_TAG, -2, 0x0);
    }
}

// ref: FUN_007d9a20
// Starts the tile's file coming in: World\Maps\<map>\<map>_<x>_<y>.adt
void CMapArea::Load() {
    char path[256];

    SStrPrintf(path, sizeof(path), "%s\\%s_%d_%d.adt", CMap::s_mapPath, CMap::s_mapName, this->m_areaX, this->m_areaY);

    this->BeginLoad(path);
}

// ref: FUN_007d7150
// Opens the file, sizes a buffer for the whole of it, and hands the read to the async reader;
// LoadCallback runs once the bytes are in.
void CMapArea::BeginLoad(const char* path) {
    CMap::SafeOpen(path, &this->m_file);

    this->m_fileSize = SFile::GetFileSize(this->m_file, nullptr);
    this->m_fileBuffer = static_cast<uint8_t*>(CMap::MapMemAlloc(this->m_fileSize));

    this->m_asyncObject = AsyncFileReadAllocObject();
    this->m_asyncObject->file = this->m_file;
    this->m_asyncObject->buffer = this->m_fileBuffer;
    this->m_asyncObject->size = this->m_fileSize;
    this->m_asyncObject->userArg = this;
    this->m_asyncObject->userPostloadCallback = &CMapArea::LoadCallback;

    AsyncFileReadObject(this->m_asyncObject, 0);
}

// ref: FUN_007d7020
void CMapArea::LoadCallback(void* arg) {
    auto area = static_cast<CMapArea*>(arg);

    area->ParseChunks();

    AsyncFileReadDestroyObject(area->m_asyncObject);
    area->m_asyncObject = nullptr;
    area->m_file = nullptr;
}

// ref: FUN_007d6ef0
// Points the tile at each top-level chunk through the MHDR offsets. MFBO only when the header
// flags it, MH2O only when present and non-empty, MTXF only when present.
void CMapArea::ParseChunks() {
    uint8_t* data = this->m_fileBuffer;

    // Past MVER (8 + 4 bytes) and the MHDR chunk header
    auto header = reinterpret_cast<SMMapHeader*>(data + *reinterpret_cast<uint32_t*>(data + 4) + 0x10);
    this->m_header = header;
    auto body = reinterpret_cast<uint8_t*>(header);

    uint32_t ofsMTEX = header->ofsMTEX;

    this->m_chunkInfo = reinterpret_cast<SMChunkInfo*>(body + header->ofsMCIN + 8);
    this->m_doodadDefs = body + header->ofsMDDF + 8;
    this->m_mapObjDefs = body + header->ofsMODF + 8;
    this->m_doodadNames = reinterpret_cast<char*>(body + header->ofsMMDX + 8);
    this->m_mapObjNames = reinterpret_cast<char*>(body + header->ofsMWMO + 8);
    this->m_doodadNameOffsets = reinterpret_cast<uint32_t*>(body + header->ofsMMID + 8);
    this->m_mapObjNameOffsets = reinterpret_cast<uint32_t*>(body + header->ofsMWID + 8);

    if (header->flags & 0x1) {
        this->m_flightBounds = body + header->ofsMFBO + 8;
    }

    if (header->ofsMH2O && *reinterpret_cast<uint32_t*>(body + header->ofsMH2O + 4)) {
        // TODO FUN_008a3050 (the liquid instance, 8 bytes from SMemAlloc) and FUN_007d4f10
        // (its MH2O parse) are not ported yet
    }

    if (this->m_header->ofsMTXF) {
        this->m_textureFlags = reinterpret_cast<uint32_t*>(body + this->m_header->ofsMTXF + 8);
    }

    this->LoadTextures(reinterpret_cast<char*>(body + ofsMTEX + 8), *reinterpret_cast<uint32_t*>(body + this->m_header->ofsMTEX + 4));

    this->m_doodadDefCount = *reinterpret_cast<uint32_t*>(body + this->m_header->ofsMDDF + 4) / 0x24;
    this->m_mapObjDefCount = *reinterpret_cast<uint32_t*>(body + this->m_header->ofsMODF + 4) >> 6;
}

// ref: FUN_007c30b0
// The texture table grows in place: realloc, or alloc + copy + free when realloc cannot
void CMapArea::GrowTextures(uint32_t capacity) {
    auto old = this->m_textures;
    this->m_textureCapacity = capacity;

    this->m_textures = static_cast<CMapAreaTexture*>(SMemReAlloc(old, capacity * sizeof(CMapAreaTexture), AREA_TEXTURE_TAG, -2, 0x10));

    if (!this->m_textures) {
        this->m_textures = static_cast<CMapAreaTexture*>(SMemAlloc(capacity * sizeof(CMapAreaTexture), AREA_TEXTURE_TAG, -2, 0x0));

        if (old) {
            uint32_t count = this->m_textureCount < capacity ? this->m_textureCount : capacity;

            for (uint32_t i = 0; i < count; i++) {
                if (&this->m_textures[i]) {
                    this->m_textures[i] = old[i];
                }
            }

            SMemFree(old, AREA_TEXTURE_TAG, -2, 0x0);
        }
    }
}

// ref: FUN_007d6d20
// Walks the MTEX name block, one entry per name, growing the table by 16 (32 once past 32) as
// it goes, and creates each texture unless the archives are streaming.
void CMapArea::LoadTextures(const char* names, uint32_t size) {
    int32_t streaming = SFile::IsStreamingMode();

    this->m_textureGrowth = 0x10;

    for (uint32_t pos = 0; pos < size; ) {
        uint32_t needed = this->m_textureCount + 1;

        if (this->m_textureCapacity < needed) {
            uint32_t growth = this->m_textureGrowth;

            if (growth == 0) {
                if (needed < 0x20) {
                    growth = needed;
                    for (uint32_t bits = this->m_textureCount & needed; bits; bits = (bits - 1) & bits) {
                        growth = bits;
                    }
                    if (growth == 0) {
                        growth = 1;
                    }
                } else {
                    this->m_textureGrowth = 0x20;
                    growth = 0x20;
                }
            }

            if (needed % growth) {
                needed += growth - needed % growth;
            }

            this->GrowTextures(needed);
        }

        auto entry = &this->m_textures[this->m_textureCount];
        this->m_textureCount++;
        entry->name = names + pos;
        entry->texture = nullptr;

        int32_t index = this->m_textureCount - 1;

        if (!streaming) {
            this->LoadTexture(&this->m_textures[index], index);
        }

        while (names[pos]) {
            pos++;
        }
        pos++;
    }
}

// ref: FUN_007d6980
// A terrain layer texture: the "_s.blp" specular variant when the device can shade it and the
// layer is not flagged plain, a solid black when the layer is flagged and the device cannot,
// otherwise the texture itself.
void CMapArea::LoadTexture(CMapAreaTexture* entry, int32_t index) {
    // The reference asks the device caps three questions here (CGxCaps +0x5c, +0xb4, +0xc4)
    // and only a device answering yes to all three may shade a flagged layer. Frozen's CGxCaps
    // is a compacted layout, so those fields are not identified yet; every device this runs on
    // answers yes, which is what is assumed until they are.
    bool shaded = true;

    uint32_t flags = 0;
    if (this->m_textureFlags) {
        flags = this->m_textureFlags[index];
    }

    if (!(flags & 0x1) && CMap::s_terrainSpecular) {
        char path[260];
        SStrCopy(path, entry->name, sizeof(path));
        SStrCopy(SStrChrR(path, '.'), "_s.blp", sizeof(path));
        entry->texture = CMap::LoadTexture(path);
        return;
    }

    if ((flags & 0x1) && !shaded) {
        CImVector black = { 0x00, 0x00, 0x00, 0xFF };
        entry->texture = TextureCreateSolid(black);
        return;
    }

    entry->texture = CMap::LoadTexture(entry->name);
}

// ref: FUN_007d6b30
// One chunk of the tile from its MCIN entry: pooled, linked to the tile, given its indices, and
// loaded from the file buffer. The entry's flag records that its sizes have been fixed up.
void CMapArea::CreateChunk(int32_t x, int32_t y) {
    auto info = &this->m_chunkInfo[y * 16 + x];
    uint32_t oldFlags = info->flags;
    info->flags |= 0x1;

    auto chunk = CMap::AllocChunk();
    auto link = CMap::AllocBaseObjLink(chunk);
    link->ref = this;
    this->m_chunkLinkList.LinkToTail(link);

    chunk->m_areaChunkX = x;
    chunk->m_flags = 0;
    chunk->m_areaChunkY = y;
    chunk->m_indexY = this->m_chunkBaseX + x;
    chunk->m_indexX = this->m_chunkBaseY + y;
    chunk->m_cellY = (this->m_chunkBaseX + x) * 8;
    chunk->m_cellX = (this->m_chunkBaseY + y) * 8;

    chunk->Load(this->m_fileBuffer + info->offset, (oldFlags & 0x1) == 0);
}

// ref: FUN_007d6bf0
// For every chunk of the tile inside rect (map chunk coordinates, {minX, minY, maxX, maxY}):
// chunks outside the world's chunk window are destroyed, chunks inside it are created if
// missing. With update set, each live chunk of a rect the frustum reaches refreshes its sort
// distance (which puts it in its distance row) and its liquid visibility.
void CMapArea::CreateChunks(int32_t update, const int32_t* rect) {
    // rect = { minRow, minCol, maxRow, maxCol } in map chunk coordinates; the grid is [row][col]
    for (int32_t row = rect[0]; row <= rect[2]; row++) {
        int32_t y = row & 0xF;

        for (int32_t col = rect[1]; col <= rect[3]; col++) {
            int32_t x = col & 0xF;

            if (col < CMap::s_chunkWindowMinX || CMap::s_chunkWindowMaxX < col || row < CMap::s_chunkWindowMinY || CMap::s_chunkWindowMaxY < row) {
                auto chunk = this->m_chunks[y * 16 + x];

                if (chunk) {
                    this->m_chunks[chunk->m_areaChunkY * 16 + chunk->m_areaChunkX] = nullptr;
                    CMap::FreeBaseObjLink(chunk->m_parentLinkList.Head());
                    CMap::DestroyChunk(chunk);
                }
            } else {
                if (!this->m_chunks[y * 16 + x]) {
                    this->CreateChunk(x, y);
                }

                if (update && CWorldScene::ChunkRectInView(rect)) {
                    auto chunk = this->m_chunks[y * 16 + x];
                    chunk->UpdateSortDistance();
                    chunk->UpdateLiquidVisibility();
                }
            }
        }
    }
}

// ref: FUN_007d6a90
// Destroys every chunk of the tile inside rect ({minRow, minCol, maxRow, maxCol} in map chunk
// coordinates), clearing each grid slot first
void CMapArea::DestroyChunks(const int32_t* rect) {
    int32_t row0 = rect[0] - this->m_chunkBaseY;
    int32_t col0 = rect[1] - this->m_chunkBaseX;
    int32_t row1 = rect[2] - this->m_chunkBaseY;
    int32_t col1 = rect[3] - this->m_chunkBaseX;

    for (int32_t row = row0; row <= row1; row++) {
        for (int32_t col = col0; col <= col1; col++) {
            auto slot = &this->m_chunks[row * 16 + col];
            auto chunk = *slot;

            if (chunk) {
                *slot = nullptr;
                CMap::FreeBaseObjLink(chunk->m_parentLinkList.Head());
                CMap::DestroyChunk(chunk);
            }
        }
    }
}

// ref: FUN_007c35f0
// Tears the tile down: a read still in flight is cancelled (the cleanup then owns the buffer),
// every chunk is unlinked, cleared from the grid and its MCIN slot, and destroyed, the textures
// are closed, and the tile's own links are released.
void CMapArea::Destroy() {
    if (this->m_asyncObject) {
        if (!AsyncFileReadCancel(this->m_asyncObject, &CMap::AsyncLoadCleanup)) {
            this->m_fileBuffer = nullptr;
        }

        this->m_asyncObject = nullptr;
        this->m_file = nullptr;
    }

    for (auto link = this->m_chunkLinkList.Head(); link; ) {
        auto chunk = static_cast<CMapChunk*>(link->owner);
        auto next = this->m_chunkLinkList.Next(link);

        this->m_chunks[chunk->m_areaChunkY * 16 + chunk->m_areaChunkX] = nullptr;
        this->m_chunkInfo[chunk->m_areaChunkY * 16 + chunk->m_areaChunkX].flags = 0;

        CMap::FreeBaseObjLink(link);
        CMap::DestroyChunk(chunk);

        link = next;
    }

    for (uint32_t i = 0; i < this->m_textureCount; i++) {
        if (this->m_textures[i].texture) {
            HandleClose(this->m_textures[i].texture);
        }
    }
    this->m_textureCount = 0;

    for (auto link = this->m_parentLinkList.Head(); link; ) {
        auto next = this->m_parentLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }
}
