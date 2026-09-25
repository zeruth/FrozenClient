#ifndef WORLD_MAP_C_MAP_AREA_HPP
#define WORLD_MAP_C_MAP_AREA_HPP

#include "world/map/CMapBaseObj.hpp"
#include "gx/Texture.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CAsyncObject;
class CMapChunk;
class SFile;

// The MHDR body: offsets of the ADT's top-level chunks, relative to the MHDR body itself
struct SMMapHeader {
    uint32_t flags;         // +0x00: bit 0 = the file carries MFBO
    uint32_t ofsMCIN;       // +0x04
    uint32_t ofsMTEX;       // +0x08
    uint32_t ofsMMDX;       // +0x0c
    uint32_t ofsMMID;       // +0x10
    uint32_t ofsMWMO;       // +0x14
    uint32_t ofsMWID;       // +0x18
    uint32_t ofsMDDF;       // +0x1c
    uint32_t ofsMODF;       // +0x20
    uint32_t ofsMFBO;       // +0x24
    uint32_t ofsMH2O;       // +0x28
    uint32_t ofsMTXF;       // +0x2c
    uint32_t pad[4];        // +0x30
};

// One MCIN entry: where a chunk's MCNK sits in the file
struct SMChunkInfo {
    uint32_t offset;        // +0x0: from the start of the file
    uint32_t size;          // +0x4
    uint32_t flags;         // +0x8: bit 0 = the chunk's sub-chunk sizes were already fixed up
    uint32_t asyncId;       // +0xc
};

// One entry of an area's texture table: the MTEX name and the texture made from it
struct CMapAreaTexture {
    const char* name;
    HTEXTURE texture;
};

// One ADT tile ("WAREA" heap, 16 to a block): 16x16 chunks, loaded asynchronously from
// <map>_<x>_<y>.adt into m_fileBuffer, parsed in place. Reference offsets follow the base object.
class CMapArea : public CMapBaseObj {
    public:
        // Member functions
        CMapArea();
        ~CMapArea() override;

        // Member variables
        CAaBox m_bounds;                          // +0x24: b = corner less a tile, t = corner
        C3Vector m_corner;                        // +0x3c: the tile's largest-x, largest-y corner
        int32_t m_areaX = 0;                      // +0x48: tile column in the 64x64 grid
        int32_t m_areaY = 0;                      // +0x4c: tile row
        int32_t m_chunkBaseX = 0;                 // +0x50: m_areaX * 16
        int32_t m_chunkBaseY = 0;                 // +0x54: m_areaY * 16
        uint32_t m_textureCapacity = 0;           // +0x58
        uint32_t m_textureCount = 0;              // +0x5c
        CMapAreaTexture* m_textures = nullptr;    // +0x60
        uint32_t m_textureGrowth = 0;             // +0x64
        SMMapHeader* m_header = nullptr;          // +0x68: MHDR body inside m_fileBuffer
        SFile* m_file = nullptr;                  // +0x6c
        CAsyncObject* m_asyncObject = nullptr;    // +0x70
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_chunkLinkList;  // +0x74: links whose owners are this tile's chunks
        uint8_t* m_fileBuffer = nullptr;          // +0x80: the whole .adt, from CMap::MapMemAlloc
        uint32_t m_fileSize = 0;                  // +0x84
        SMChunkInfo* m_chunkInfo = nullptr;       // +0x88: MCIN
        uint32_t m_unk8c = 0;                     // +0x8c
        uint8_t* m_doodadDefs = nullptr;          // +0x90: MDDF
        uint8_t* m_mapObjDefs = nullptr;          // +0x94: MODF
        uint32_t m_doodadDefCount = 0;            // +0x98
        uint32_t m_mapObjDefCount = 0;            // +0x9c
        char* m_doodadNames = nullptr;            // +0xa0: MMDX
        char* m_mapObjNames = nullptr;            // +0xa4: MWMO
        uint32_t* m_doodadNameOffsets = nullptr;  // +0xa8: MMID
        uint32_t* m_mapObjNameOffsets = nullptr;  // +0xac: MWID
        uint8_t* m_flightBounds = nullptr;        // +0xb0: MFBO
        uint32_t* m_textureFlags = nullptr;       // +0xb4: MTXF
        void* m_liquid = nullptr;                 // +0xb8: the MH2O liquid instance
        CMapChunk* m_chunks[256] = {};            // +0xbc: [y * 16 + x]

        // Member functions
        void Load();
        void BeginLoad(const char* path);
        static void LoadCallback(void* area);
        void ParseChunks();
        void LoadTextures(const char* names, uint32_t size);
        void LoadTexture(CMapAreaTexture* entry, int32_t index);
        void GrowTextures(uint32_t capacity);
        void CreateChunk(int32_t x, int32_t y);
        void CreateChunks(int32_t update, const int32_t* rect);
        void Destroy();
};

#endif
