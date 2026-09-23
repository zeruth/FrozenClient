#include "gx/font/CGxFont.hpp"
#include "gx/font/FontFace.hpp"
#include "gx/font/GxuFontInternal.hpp"
#include "gx/Texture.hpp"
#include <cmath>
#include <cstring>
#include <storm/Error.hpp>
#include <storm/String.hpp>
#include <storm/Unicode.hpp>

uint16_t TEXTURECACHE::s_textureData[256 * 256];

GLYPHBITMAPDATA::~GLYPHBITMAPDATA() {
    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0x0);
    }

    this->m_data = nullptr;
}

void GLYPHBITMAPDATA::CopyFrom(GLYPHBITMAPDATA* data) {
    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0);
    }

    this->m_data = nullptr;
    *this = *data;
    data->m_data = nullptr;
}

uint32_t CHARCODEDESC::GapToNextTexture() {
    CHARCODEDESC* next = this->textureRowLink.Next();

    return next
        ? next->glyphStartPixel - this->glyphEndPixel - 1
        : 255 - this->glyphEndPixel;
}

uint32_t CHARCODEDESC::GapToPreviousTexture() {
    CHARCODEDESC* previous = this->textureRowLink.Prev();

    return previous
        ? this->glyphStartPixel - previous->glyphEndPixel - 1
        : this->glyphStartPixel;
}

void CHARCODEDESC::GenerateTextureCoords(uint32_t rowNumber, uint32_t glyphSide) {
    this->bitmapData.m_textureCoords.minY = (glyphSide * rowNumber) / 256.0f;
    this->bitmapData.m_textureCoords.minX = this->glyphStartPixel / 256.0f;
    this->bitmapData.m_textureCoords.maxY = ((glyphSide * rowNumber) + glyphSide) / 256.0f;
    this->bitmapData.m_textureCoords.maxX = (this->glyphStartPixel + this->bitmapData.m_glyphCellWidth) / 256.0f;
}

KERNINGHASHKEY& KERNINGHASHKEY::operator=(const KERNINGHASHKEY& rhs) {
    if (this->code != rhs.code) {
        this->code = rhs.code;
    }

    return *this;
}

bool KERNINGHASHKEY::operator==(const KERNINGHASHKEY& rhs) {
    return this->code == rhs.code;
}

CHARCODEDESC* TEXTURECACHEROW::CreateNewDesc(GLYPHBITMAPDATA* data, uint32_t rowNumber, uint32_t glyphCellHeight) {
    uint32_t glyphWidth = data->m_glyphCellWidth;

    if (this->widestFreeSlot < glyphWidth) {
        return nullptr;
    }

    if (!this->glyphList.Head()) {
        CHARCODEDESC* newCode = this->glyphList.NewNode(2, 0, 0);

        newCode->glyphStartPixel = 0;
        newCode->glyphEndPixel = glyphWidth - 1;
        newCode->rowNumber = rowNumber;
        newCode->bitmapData.CopyFrom(data);
        newCode->GenerateTextureCoords(rowNumber, glyphCellHeight);

        this->widestFreeSlot -= glyphWidth;

        return newCode;
    }

    uint32_t gapToPrevious = this->glyphList.Head()->GapToPreviousTexture();
    this->widestFreeSlot = gapToPrevious;

    if (gapToPrevious >= glyphWidth) {
        auto m = SMemAlloc(sizeof(CHARCODEDESC), __FILE__, __LINE__, 0x0);
        auto newCode = new (m) CHARCODEDESC();

        this->glyphList.LinkNode(newCode, 2, this->glyphList.Head());

        newCode->glyphEndPixel = this->glyphList.Head()->glyphStartPixel - 1;
        newCode->glyphStartPixel = newCode->glyphEndPixel - glyphWidth + 1;
        newCode->rowNumber = rowNumber;
        newCode->bitmapData.CopyFrom(data);
        newCode->GenerateTextureCoords(rowNumber, glyphCellHeight);

        this->widestFreeSlot = this->glyphList.Head()
            ? this->glyphList.Head()->GapToPreviousTexture()
            : 0;

        for (auto code = this->glyphList.Head(); code; code = this->glyphList.Link(code)->Next()) {
            uint32_t gapToNext = code->GapToNextTexture();
            if (gapToNext > this->widestFreeSlot) {
                this->widestFreeSlot = gapToNext;
            }
        }

        return newCode;
    }

    int32_t inserted = 0;
    CHARCODEDESC* newCode = nullptr;

    for (auto code = this->glyphList.Head(); code; code = this->glyphList.Link(code)->Next()) {
        uint32_t gapToNext = code->GapToNextTexture();

        if (inserted) {
            if (gapToNext > this->widestFreeSlot) {
                this->widestFreeSlot = gapToNext;
            }

            continue;
        }

        if (gapToNext >= glyphWidth) {
            auto m = SMemAlloc(sizeof(CHARCODEDESC), __FILE__, __LINE__, 0x0);
            newCode = new (m) CHARCODEDESC();

            this->glyphList.LinkNode(newCode, 1, code);

            newCode->glyphStartPixel = code->glyphEndPixel + 1;
            newCode->glyphEndPixel = code->glyphEndPixel + glyphWidth;
            newCode->rowNumber = rowNumber;
            newCode->bitmapData.CopyFrom(data);
            newCode->GenerateTextureCoords(rowNumber, glyphCellHeight);

            inserted = 1;
        }
    }

    return newCode;
}

void TEXTURECACHEROW::EvictGlyph(CHARCODEDESC* desc) {
    // TODO
}

void TEXTURECACHE::TextureCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    TEXTURECACHE* cache = static_cast<TEXTURECACHE*>(userArg);

    switch (cmd) {
        case GxTex_Latch: {
            for (int32_t i = cache->m_textureRows.Count() - 1; i >= 0; i--) {
                auto& cacheRow = cache->m_textureRows[i];

                for (auto glyph = cacheRow.glyphList.Head(); glyph; glyph = cacheRow.glyphList.Next(glyph)) {
                    cache->WriteGlyphToTexture(glyph);
                }
            }

            texelStrideInBytes = 512;
            texels = TEXTURECACHE::s_textureData;

            break;
        };
    }
}

CHARCODEDESC* TEXTURECACHE::AllocateNewGlyph(GLYPHBITMAPDATA* data) {
    for (int32_t i = 0; i < this->m_textureRows.Count(); i++) {
        auto& cacheRow = this->m_textureRows[i];
        CHARCODEDESC* glyph = cacheRow.CreateNewDesc(data, i, this->m_theFace->m_cellHeight);

        if (glyph) {
            return glyph;
        }
    }

    return nullptr;
}

void TEXTURECACHE::CreateTexture(int32_t filter) {
    CGxTexFlags flags = CGxTexFlags(filter ? GxTex_Linear : GxTex_Nearest, 0, 0, 0, 0, 0, 1);

    HTEXTURE texture = TextureCreate(
        256,
        256,
        GxTex_Argb4444,
        GxTex_Argb4444,
        flags,
        this,
        TEXTURECACHE::TextureCallback,
        "GxuFont",
        0
    );

    this->m_texture = texture;
}

void TEXTURECACHE::Initialize(CGxFont* face, uint32_t pixelSize) {
    this->m_theFace = face;

    uint32_t rowCount = 256 / pixelSize;
    this->m_textureRows.SetCount(rowCount);

    for (int32_t i = 0; i < rowCount; i++) {
        this->m_textureRows[i].widestFreeSlot = 256;
    }
}

void TEXTURECACHE::PasteGlyph(const GLYPHBITMAPDATA& data, uint16_t* dst) {
    if (this->m_theFace->m_flags & FONT_OUTLINE) {
        if (this->m_theFace->m_flags & FONT_MONOCHROME) {
            this->PasteGlyphOutlinedMonochrome(data, dst);
        } else {
            this->PasteGlyphOutlinedAA(data, dst);
        }
    } else if (this->m_theFace->m_flags & FONT_MONOCHROME) {
        this->PasteGlyphNonOutlinedMonochrome(data, dst);
    } else {
        this->PasteGlyphNonOutlinedAA(data, dst);
    }
}

void TEXTURECACHE::PasteGlyphNonOutlinedAA(const GLYPHBITMAPDATA& glyphData, uint16_t* dst) {
    auto src = reinterpret_cast<uint8_t*>(glyphData.m_data);
    auto pitch = glyphData.m_glyphPitch;
    auto dstCellStride = glyphData.m_glyphCellWidth * 2;

    for (int32_t y = 0; y < glyphData.m_yStart; y++) {
        memset(dst, 0, dstCellStride);
        dst += 256;
    }

    for (int32_t y = 0; y < glyphData.m_glyphHeight; y++) {
        for (int32_t x = 0; x < glyphData.m_glyphWidth; x++) {
            dst[x] = ((src[x] & 0xF0) << 8) | 0xFFF;
        }

        src += pitch;
        dst += 256;
    }

    auto glyphHeight = glyphData.m_glyphHeight;
    auto yStart = glyphData.m_yStart;
    if (this->m_theFace->m_cellHeight - glyphHeight - yStart > 0 && this->m_theFace->m_cellHeight - glyphHeight != yStart) {
        for (int32_t y = 0; y < this->m_theFace->m_cellHeight - glyphHeight - yStart; y++) {
            memset(dst, 0, dstCellStride);
            dst += 256;
        }
    }
}

// Monochrome glyphs come out of FreeType one BIT per pixel, most significant bit leftmost, so the
// only difference from the anti-aliased path above is how a source pixel becomes a texel: a set bit
// is opaque white and a clear bit is nothing. The reference spells that with negb/sbb to turn the
// bit into a 0 or -1 mask and writes 0xFFFF or 0x0000; this writes the same two values.
//
// It was an empty body, and it is reachable: CSimpleFont and CSimpleFontString both set
// FONT_MONOCHROME from the MONOCHROME flag a FrameXML font can declare, so any font that did
// declared it drew blank glyphs. The padding rows above and below the glyph, the 256-texel row
// stride and the odd second half of the bottom-padding guard are all the same as the AA version,
// because in the reference they are the same code.
// ref: FUN_006c9330
void TEXTURECACHE::PasteGlyphNonOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint16_t* dst) {
    auto src = reinterpret_cast<uint8_t*>(data.m_data);
    auto pitch = data.m_glyphPitch;
    auto dstCellStride = data.m_glyphCellWidth * 2;

    for (int32_t y = 0; y < data.m_yStart; y++) {
        memset(dst, 0, dstCellStride);
        dst += 256;
    }

    for (int32_t y = 0; y < data.m_glyphHeight; y++) {
        for (int32_t x = 0; x < data.m_glyphWidth; x++) {
            dst[x] = (src[x >> 3] >> (7 - (x & 7))) & 1 ? 0xFFFF : 0x0000;
        }

        src += pitch;
        dst += 256;
    }

    auto glyphHeight = data.m_glyphHeight;
    auto yStart = data.m_yStart;

    if (this->m_theFace->m_cellHeight - glyphHeight - yStart > 0 && this->m_theFace->m_cellHeight - glyphHeight != yStart) {
        for (int32_t y = 0; y < this->m_theFace->m_cellHeight - glyphHeight - yStart; y++) {
            memset(dst, 0, dstCellStride);
            dst += 256;
        }
    }
}

void TEXTURECACHE::PasteGlyphOutlinedAA(const GLYPHBITMAPDATA& glyphData, uint16_t* dst) {
    uint32_t v6;
    uint32_t v7;
    uint16_t* v8;
    int32_t v9;
    int32_t v15;
    int32_t v16;
    uint32_t v17;
    uint32_t v18;
    uint32_t v19;
    uint32_t v20;
    int32_t v21;
    int32_t v22;
    int32_t v23;
    int32_t v24;
    int32_t v25;
    uint32_t v26;
    uint32_t v27;
    uint32_t v28;
    bool v29;
    bool v30;
    int32_t v31;
    int32_t v32;
    int32_t v33;
    int32_t v34;
    int32_t v35;
    int32_t v36;
    bool v37;
    int32_t v38;
    int32_t v39;
    int32_t v40;
    uint32_t v41;
    uint8_t v42;
    bool v43;
    uint16_t v44[9216];
    uint16_t v45[9216];
    uint16_t v46[9216];
    uint32_t v49;
    uint16_t* v52;
    uint32_t v52_2;
    uint32_t v52_3;
    int32_t v53;

    static uint8_t pixelsLitLevels[] = {
        0, 1, 1, 3, 5, 7, 9, 0xB, 0xD, 0xF, 0, 0
    };

    const char* src = reinterpret_cast<char*>(glyphData.m_data);
    uint32_t thick = this->m_theFace->m_flags & 0x8;

    memset(v45, 0, sizeof(v45));
    memset(v46, 0, sizeof(v46));

    uint32_t ofs = thick
        ? 256 * glyphData.m_yStart + 258
        : 256 * glyphData.m_yStart + 257;

    for (int32_t y = 0; y < glyphData.m_glyphHeight; y++) {
        for (int32_t x = 0; x < glyphData.m_glyphWidth; x++) {
            uint8_t v10 = src[(y * glyphData.m_glyphPitch) + x];

            if (v10) {
                v45[ofs + (y * 256) + x] = v10;
                v46[ofs + (y * 256) + x] = 1;
            }
        }
    }

    v52_2 = 0;

    v15 = glyphData.m_yStart - 1;

    for (int32_t i = 0; i < (thick != 0) + 1; i++) {
        v49 = 2 * (i != 0) + 2;
        v16 = 2 * (i != 0) + 1;
        v17 = v15 < 0 ? 0 : v15;
        v53 = v15 < 0 ? 0 : v15;

        if (v17 >= this->m_theFace->m_cellHeight) {
            continue;
        }

        do {
            v18 = glyphData.m_glyphCellWidth;
            v19 = v17 << 8;
            v20 = 0;

            if (!v18) {
                goto LABEL_68;
            }

            do {
                v21 = v19 + v20;

                if (v46[v19 + v20] & v16) {
                    goto LABEL_66;
                }

                if (v53) {
                    if (v53 == this->m_theFace->m_cellHeight - 1) {
                        if (v20) {
                            if (v20 == v18 - 1) {
                                if (
                                    v46[v21 - 1] & v16
                                    || v46[v19 + v20 - 257] & v16
                                ) {
                                    goto LABEL_65;
                                }

                                v22 = v46[v19 + v20 - 256];
                            } else {
                                if (
                                    v46[v19 + v20 - 257] & v16
                                    || v46[v19 + v20 - 256] & v16
                                    || v46[v19 + v20 - 255] & v16
                                    || v46[v21 - 1] & v16
                                ) {
                                    goto LABEL_65;
                                }

                                v22 = v46[v21 + 1];
                            }
                        } else {
                            if (
                                v46[v19 - 256] & v16
                                || v46[v19 - 255] & v16
                            ) {
                                goto LABEL_65;
                            }

                            v22 = v46[v19 + 1];
                        }
                    } else if (v20) {
                        v24 = v19 + v20;

                        if (v20 == v18 - 1) {
                            if (
                                v46[v24 - 256] & v16
                                || v46[v19 + v20 - 257] & v16
                                || v46[v21 - 1] & v16
                                || v46[v21 + 255] & v16
                            ) {
                                goto LABEL_65;
                            }

                            v22 = v46[v21 + 256];
                        } else {
                            if (
                                v46[v24 - 257] & v16
                                || v46[v19 + v20 - 256] & v16
                                || v46[v19 + v20 - 255] & v16
                                || v46[v21 - 1] & v16
                                || v46[v21 + 1] & v16
                                || v46[v21 + 255] & v16
                                || v46[v21 + 256] & v16
                            ) {
                                goto LABEL_65;
                            }

                            v22 = v46[v21 + 257];
                        }
                    } else {
                        if (
                            v46[v19 - 256] & v16
                            || v46[v19 - 255] & v16
                            || v46[v19 + 1] & v16
                            || v46[v19 + 257] & v16
                        ) {
                            goto LABEL_65;
                        }

                        v22 = v46[v19 + 256];
                    }
LABEL_64:
                    if (!(v22 & v16)) {
                        goto LABEL_66;
                    }

                    goto LABEL_65;
                }

                if (v20) {
                    v23 = v46[v20 - 1];

                    if (v20 == v18 - 1) {
                        if (!(v23 & v16) && !(v46[v20 + 255] & v16)) {
                            v22 = v46[v20 + 256];
                            goto LABEL_64;
                        }
                    } else if (!(v23 & v16) && !(v46[v20 + 1] & v16) && !(v46[v20 + 255] & v16) && !(v46[v20 + 256] & v16)) {
                        v22 = v46[v20 + 257];
                        goto LABEL_64;
                    }
                } else if (!(v46[1] & v16) && !(v46[257] & v16)) {
                    v22 = v46[256];
                    goto LABEL_64;
                }
LABEL_65:
                v46[v21] = v49;
LABEL_66:
                ++v20;
            } while (v20 < v18);

            v17 = v53;
LABEL_68:
            v53 = ++v17;
        } while (v17 < this->m_theFace->m_cellHeight);
    }

    memset(v44, 0, sizeof(v44));

    v26 = 0;
    v53 = 0;

    if (!this->m_theFace->m_cellHeight) {
        goto LABEL_95;
    }

    while (2) {
        v27 = glyphData.m_glyphCellWidth;
        v28 = v26 << 8;
        v25 = 0;

        if (!v27) {
            goto LABEL_94;
        }

        while (2) {
            v37 = v46[v28 + v25] == 0;
            v52 = &v46[v28 + v25];
            v29 = !v37;
            v30 = !v37;

            if (v26) {
                if (v53 == this->m_theFace->m_cellHeight - 1) {
                    if (!v25) {
                        v31 = v30 + (v46[v28 + 1] != 0) + (v46[v28 - 255] != 0) + (v46[v28 - 256] != 0);
                        goto LABEL_92;
                    }

                    v37 = v25 == v27 - 1;
                    v34 = v28 + v25;

                    if (v37) {
                        v31 = (v46[v28 + v25 - 256] != 0) + (v46[v34 - 257] != 0) + v30 + (*(v52 - 1) != 0);
                        goto LABEL_92;
                    }

                    v35 = (v46[v34 - 256] != 0) + (v46[v28 + v25 - 257] != 0);
                    v36 = 0;
                    v37 = v46[v28 + v25 - 255] == 0;
                    v38 = v28 + v25;
                } else {
                    if (!v25) {
                        v31 = v30
                            + (v46[v28 + 256] != 0)
                            + (v46[v28 + 1] != 0)
                            + (v46[v28 + 257] != 0)
                            + (v46[v28 - 255] != 0)
                            + (v46[v28 - 256] != 0);

                        goto LABEL_92;
                    }

                    v37 = v25 == v27 - 1;
                    v39 = v28 + v25;

                    if (v37) {
                        v31 = (v46[v28 + v25 + 256] != 0)
                            + (v46[v28 + v25 + 255] != 0)
                            + (v46[v28 + v25 - 256] != 0)
                            + (v46[v39 - 257] != 0)
                            + v30
                            + (*(v52 - 1) != 0);

                        goto LABEL_92;
                    }

                    v37 = v46[v39 - 256] == 0;
                    v38 = v28 + v25;
                    v35 = (v46[v28 + v25 + 256] != 0)
                        + (v46[v28 + v25 + 255] != 0)
                        + (v46[v28 + v25 - 255] != 0)
                        + !v37
                        + (v46[v28 + v25 - 257] != 0);
                    v36 = 0;
                    v37 = v46[v28 + v25 + 257] == 0;
                }

                v36 = !v37;
                v31 = (v46[v38 + 1] != 0) + v36 + v35 + v30 + (*(v52 - 1) != 0);
            } else if (v25) {
                if (v25 == v27 - 1) {
                    v32 = (v46[v25 + 255] != 0) + (v46[v25 - 1] != 0);
                    v33 = v29 + (v46[v25 + 256] != 0);
                } else {
                    v32 = (v46[v25 + 257] != 0)
                        + (v46[v25 + 256] != 0)
                        + (v46[v25 + 255] != 0)
                        + (v46[v25 - 1] != 0);
                    v33 = v29 + (v46[v25 + 1] != 0);
                }

                v31 = v33 + v32;
            } else {
                v31 = v29 + (v46[1] != 0) + (v46[257] != 0) + (v46[256] != 0);
            }
LABEL_92:
            v26 = v53;

            v44[v28 + v25] = pixelsLitLevels[v31];

            v27 = glyphData.m_glyphCellWidth;

            if (++v25 < v27) {
                continue;
            }

            break;
        }

LABEL_94:
        v53 = ++v26;

        if (v26 < this->m_theFace->m_cellHeight) {
            continue;
        }

        break;
    }

    // The write-out below was checked against the reference on 2026-09-23 and is faithful. Its
    // three planes map one to one onto the reference's three scratch buffers -- v46 is the mask at
    // -0x481c, v45 the coverage at -0x901c, v44 the lit level at -0xd81c -- and it branches in the
    // same order. The coverage scale is `imull $0xff` then `sarl $0xc` there, which is this
    // expression exactly, and the nested shift-or assembles the same ARGB4444 word:
    // (lit << 12) | (a << 8) | (a << 4) | a. The reference reads the coverage plane a byte at a
    // time where this reads the uint16, which is equivalent because the unpack never writes above
    // 255.
LABEL_95:
    v40 = 0;

    for (int32_t y = 0; y < this->m_theFace->m_cellHeight; y++) {
        for (int32_t x = 0; x < glyphData.m_glyphCellWidth; x++) {
            if (v46[v40 + x]) {
                if (v45[v40 + x]) {
                    v42 = (255 * v45[v40 + x]) >> 12;
                    dst[v40 + x] = v42 | (16 * (v42 | (16 * (v42 | (16 * v44[v40 + x])))));
                } else {
                    dst[v40 + x] = v44[v40 + x] << 12;
                }
            } else {
                dst[v40 + x] = 0;
            }
        }

        v40 += 256;
    }
}

// The outlined monochrome glyph paste, FUN_006c8e70, named by the dispatcher inlined at
// 0x006c9fce which tests FONT_OUTLINE then FONT_MONOCHROME to pick one of the four. It was an
// empty body, so any FrameXML font declaring both flags drew nothing.
//
// It is NOT PasteGlyphOutlinedAA with a plane removed. That one keeps three scratch planes -- mask,
// coverage and lit level -- and anti-aliases its outline from neighbour counts. This keeps ONE, and
// the plane holds a CLASS CODE: 1 for the glyph body, 4 for the hard inner ring, 2 for the soft
// outer ring. There is no anti-aliasing anywhere in it.
//
// Three things about the reference are worth stating, because they are what make the loop below a
// faithful rewrite rather than a lookalike:
//
// * The dilation is IN PLACE, and that is safe rather than lucky. Each pass searches for one class
//   and writes a different one -- pass 0 looks for 1 and writes 4, pass 1 looks for 4 and writes 2
//   -- so a value written during a pass can never satisfy that same pass's test. In-place and
//   double-buffered give identical results here.
// * The reference unrolls the neighbourhood into fifteen boundary cases (first row, last row, first
//   column, last column and the interior). Counting the tests in each shows they are the full
//   eight-neighbourhood minus whatever falls outside, so the bounds-checked loop below computes the
//   same thing with none of the unrolling.
// * A body pixel is never overwritten: the reference guards its store with a `!= 1` test.
//
// **Built, not seen running.** The failure mode is confined to fonts carrying both flags, and it is
// visual only.
// ref: FUN_006c8e70
void TEXTURECACHE::PasteGlyphOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint16_t* dst) {
    // One plane, 256 texels per row, matching the reference's single 0x4800-byte scratch buffer.
    uint16_t plane[9216];
    memset(plane, 0, sizeof(plane));

    auto cellHeight = static_cast<int32_t>(this->m_theFace->m_cellHeight);
    auto cellWidth = static_cast<int32_t>(data.m_glyphCellWidth);
    auto yStart = static_cast<int32_t>(data.m_yStart);

    // FONT_OUTLINE_THICK. The reference reads face->flags & 0x8, runs a second dilation pass when
    // it is set, and shifts the glyph one more texel right to leave room for the wider ring.
    bool thick = (this->m_theFace->m_flags & 0x8) != 0;

    // The body lands one row down and one column right of the cell origin, so the ring has
    // somewhere to go; thick moves it one further right again.
    int32_t originRow = yStart + 1;
    int32_t originCol = thick ? 2 : 1;

    auto src = reinterpret_cast<uint8_t*>(data.m_data);

    for (int32_t y = 0; y < data.m_glyphHeight; y++) {
        for (int32_t x = 0; x < data.m_glyphWidth; x++) {
            if ((src[x >> 3] >> (7 - (x & 7))) & 1) {
                int32_t row = originRow + y;
                int32_t col = originCol + x;

                if (row >= 0 && row < cellHeight && col >= 0 && col < cellWidth) {
                    plane[row * 256 + col] = 1;
                }
            }
        }

        src += data.m_glyphPitch;
    }

    for (int32_t pass = 0; pass < (thick ? 2 : 1); pass++) {
        uint16_t seek = pass == 0 ? 1 : 4;
        uint16_t mark = pass == 0 ? 4 : 2;

        // The second pass starts one row higher, which is how the outer ring reaches above the
        // inner one. The reference decrements only when yStart is non-zero.
        int32_t y0 = yStart;

        if (pass == 1 && y0 != 0) {
            y0--;
        }

        for (int32_t y = y0; y < cellHeight; y++) {
            for (int32_t x = 0; x < cellWidth; x++) {
                int32_t idx = y * 256 + x;

                if (pass == 1 && plane[idx]) {
                    continue;
                }

                bool adjacent = false;

                for (int32_t dy = -1; dy <= 1 && !adjacent; dy++) {
                    for (int32_t dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) {
                            continue;
                        }

                        int32_t ny = y + dy;
                        int32_t nx = x + dx;

                        if (ny < 0 || ny >= cellHeight || nx < 0 || nx >= cellWidth) {
                            continue;
                        }

                        if (plane[ny * 256 + nx] & seek) {
                            adjacent = true;
                            break;
                        }
                    }
                }

                if (adjacent && plane[idx] != 1) {
                    plane[idx] = mark;
                }
            }
        }
    }

    // Class to texel, exactly as the reference writes it from 0x006c92c0.
    for (int32_t y = 0; y < cellHeight; y++) {
        for (int32_t x = 0; x < cellWidth; x++) {
            uint16_t cls = plane[y * 256 + x];

            if (cls == 1) {
                dst[x] = 0xFFFF;
            } else if (cls == 2) {
                dst[x] = 0x7000;
            } else {
                dst[x] = cls == 4 ? 0xF000 : 0x0000;
            }
        }

        dst += 256;
    }
}

void TEXTURECACHE::UpdateDirty() {
    if (this->m_anyDirtyGlyphs && this->m_texture) {
        CGxTex* gxTex = TextureGetGxTex(this->m_texture, 1, nullptr);
        CiRect updateRect = { 0, 0, 256, 256 };
        GxTexUpdate(gxTex, updateRect, 1);

        this->m_anyDirtyGlyphs = 0;
    }
}

void TEXTURECACHE::WriteGlyphToTexture(CHARCODEDESC* glyph) {
    if (!this->m_texture || !this->m_theFace || !this->m_theFace->m_cellHeight) {
        return;
    }

    uint32_t ofs = glyph->glyphStartPixel + (glyph->rowNumber * this->m_theFace->m_cellHeight << 8);
    uint16_t* ptr = &TEXTURECACHE::s_textureData[ofs];

    this->PasteGlyph(glyph->bitmapData, ptr);
}

CGxFont::~CGxFont() {
    this->Clear();
}

int32_t CGxFont::CheckStringGlyphs(const char* string) {
    if (!string || !*string) {
        return 1;
    }

    while (*string) {
        int32_t advance;
        auto code = SUniSGetUTF8(reinterpret_cast<const uint8_t*>(string), &advance);

        HASHKEY_NONE key = {};

        if (code != '\r' && code != '\n' && !this->m_activeCharacters.Ptr(code, key)) {
            return 0;
        }

        string += advance;
    }

    return 1;
}

void CGxFont::Clear() {
    if (this->m_faceHandle) {
        FontFaceCloseHandle(this->m_faceHandle);
    }

    this->m_faceHandle = nullptr;

    this->ClearGlyphs();
}

void CGxFont::ClearGlyphs() {
    for (int32_t i = 0; i < 8; i++) {
        auto& cache = this->m_textureCache[i];

        if (cache.m_texture) {
            HandleClose(this->m_textureCache[i].m_texture);
        }

        cache.m_texture = nullptr;

        // TODO
    }

    this->m_activeCharacters.Clear();

    this->m_activeCharacterCache.Clear();

    this->m_kernInfo.Clear();
}

float CGxFont::ComputeStep(uint32_t currentCode, uint32_t nextCode) {
    KERNINGHASHKEY kernKey = { nextCode | (currentCode << 16) };
    KERNNODE* kern = this->m_kernInfo.Ptr(currentCode, kernKey);

    if (kern && kern->flags & 0x02) {
        return kern->proporportionalSpacing;
    }

    auto face = FontFaceGetFace(this->m_faceHandle);
    auto currentIndex = FT_Get_Char_Index(face, currentCode);
    auto nextIndex = FT_Get_Char_Index(face, nextCode);

    FT_Vector vector;
    vector.x = 0;

    if (face->face_flags & FT_FACE_FLAG_KERNING) {
        FT_Get_Kerning(face, currentIndex, nextIndex, ft_kerning_unscaled, &vector);
        vector.x &= (vector.x >= 0) - 1;
    }

    HASHKEY_NONE charKey = {};
    auto activeChar = this->m_activeCharacters.Ptr(currentCode, charKey);

    float advance = 0.0f;

    if (activeChar) {
        advance = this->m_flags & 0x08
            ? activeChar->bitmapData.m_glyphAdvance + 1.0f
            : activeChar->bitmapData.m_glyphAdvance;
    }

    float spacing = (this->m_pixelsPerUnit * vector.x) + advance;

    if (!kern) {
        kern = this->m_kernInfo.New(currentCode, kernKey, 0, 0);
    }

    kern->flags |= 0x02;
    kern->proporportionalSpacing = ceil(spacing);

    return kern->proporportionalSpacing;
}

float CGxFont::ComputeStepFixedWidth(uint32_t currentCode, uint32_t nextCode) {
    // Three live call sites, but all three sit behind `flags & 0x10`, which only
    // ConvertStringFlags sets and only from its own 0x800 -- and nothing passes that today. So
    // returning 0 collapses no text in practice. It would the moment a caller asks for a
    // fixed-width string, which is why this is worth porting before that happens.
    // TODO
    return 0.0f;
}

float CGxFont::GetGlyphBearing(const CHARCODEDESC* glyph, bool billboarded, float height) {
    if (billboarded) {
        float v8 = ScreenToPixelHeight(1, height) / this->GetPixelSize();
        return glyph->bitmapData.m_glyphBearing * v8;
    }

    return ceil(glyph->bitmapData.m_glyphBearing);
}

int32_t CGxFont::GetGlyphData(GLYPHBITMAPDATA* glyphData, uint32_t code) {
    FT_Face face = FontFaceGetFace(this->m_faceHandle);
    FT_Set_Pixel_Sizes(face, this->m_pixelSize, 0);

    uint32_t v6 = 0;

    if (this->m_flags & 0x8) {
        v6 = 4;
    } else if (this->m_flags & FONT_OUTLINE) {
        v6 = 2;
    }

    return IGxuFontGlyphRenderGlyph(
        face,
        this->m_pixelSize,
        code,
        this->m_baseline,
        glyphData,
        this->m_flags & FONT_MONOCHROME,
        v6
    );
}

const char* CGxFont::GetName(void) const {
    STORM_ASSERT(this->m_faceHandle);

    return FontFaceGetFontName(this->m_faceHandle);
}

uint32_t CGxFont::GetPixelSize() {
    return this->m_pixelSize;
}

int32_t CGxFont::Initialize(const char* name, uint32_t newFlags, float fontHeight) {
    SStrPrintf(this->m_fontName, 260, "%s", name);

    this->m_requestedFontHeight = fontHeight;

    this->Clear();

    this->m_flags = newFlags;

    this->m_faceHandle = FontFaceGetHandle(name, GetFreeTypeLibrary());

    if (this->m_faceHandle) {
        return this->UpdateDimensions();
    } else {
        return 0;
    }
}

const CHARCODEDESC* CGxFont::NewCodeDesc(uint32_t code) {
    HASHKEY_NONE key = {};
    CHARCODEDESC* charDesc = this->m_activeCharacters.Ptr(code, key);

    if (charDesc) {
        this->m_activeCharacterCache.LinkToHead(charDesc);
        return charDesc;
    }

    GLYPHBITMAPDATA data;

    if (!CGxFont::GetGlyphData(&data, code)) {
        return nullptr;
    }

    // Attempt to allocate the character off of texture caches
    for (uint32_t textureNumber = 0; textureNumber < 8; textureNumber++) {
        TEXTURECACHE* textureCache = &this->m_textureCache[textureNumber];

        if (textureCache->m_texture && TextureGetGxTex(reinterpret_cast<CTexture*>(textureCache->m_texture), 1, nullptr)) {
            charDesc = textureCache->AllocateNewGlyph(&data);

            if (charDesc) {
                charDesc->textureNumber = textureNumber;
                break;
            }
        } else {
            textureCache->CreateTexture(this->m_flags & 0x4);
            textureCache->Initialize(this, this->m_cellHeight);

            charDesc = textureCache->AllocateNewGlyph(&data);

            if (charDesc) {
                charDesc->textureNumber = textureNumber;
            }

            break;
        }
    }

    // No character was allocated from the texture caches, so evict the oldest character and
    // attempt to allocate from that character's texture cache row
    if (!charDesc) {
        CHARCODEDESC* oldestDesc = this->m_activeCharacterCache.Tail();

        if (oldestDesc) {
            uint32_t textureNumber = oldestDesc->textureNumber;
            uint32_t rowNumber = oldestDesc->rowNumber;

            TEXTURECACHE* textureCache = &this->m_textureCache[textureNumber];
            TEXTURECACHEROW* cacheRow = &textureCache->m_textureRows[rowNumber];
            cacheRow->EvictGlyph(oldestDesc);

            this->RegisterEvictNotice(textureNumber);

            charDesc = cacheRow->CreateNewDesc(&data, rowNumber, this->m_cellHeight);

            if (charDesc) {
                charDesc->rowNumber = rowNumber;
                charDesc->textureNumber = textureNumber;
            }
        }
    }

    if (charDesc) {
        this->m_activeCharacters.Insert(charDesc, code, key);
        this->m_activeCharacterCache.LinkToHead(charDesc);
        this->m_textureCache[charDesc->textureNumber].m_anyDirtyGlyphs = 1;
    }

    return charDesc;
}

void CGxFont::RegisterEvictNotice(uint32_t a2) {
    // TODO
}

int32_t CGxFont::UpdateDimensions() {
    FT_Face theFace = FontFaceGetFace(this->m_faceHandle);

    float v11 = Sub6C2280(theFace, this->m_requestedFontHeight);
    float v3 = 2.0 / GetScreenPixelHeight();

    if (v11 > v3) {
        v3 = v11;
    }

    float height = v3;

    uint32_t pixelSize = ScreenToPixelHeight(0, height);

    if (pixelSize <= 32) {
        if (!pixelSize) {
            // TODO
            // nullsub_3();
        }
    } else {
        pixelSize = 32;
    }

    this->m_pixelSize = pixelSize;

    uint32_t v10 = theFace->ascender + abs(theFace->descender);

    if (!v10) {
        return 0;
    }

    this->m_cellHeight = pixelSize;
    float baseline = (double)(pixelSize * theFace->ascender) / (double)v10;

    this->m_baseline = (int64_t)(baseline + 0.5);

    uint32_t flags = this->m_flags;

    if (flags & 0x8) {
        this->m_cellHeight = pixelSize + 4;
    } else if (flags & 0x1) {
        this->m_cellHeight = pixelSize + 2;
    }

    int32_t result = FT_Set_Pixel_Sizes(theFace, pixelSize, 0) == FT_Err_Ok;

    this->m_pixelsPerUnit = (double)theFace->size->metrics.x_ppem / (double)theFace->units_per_EM;

    return result;
}
