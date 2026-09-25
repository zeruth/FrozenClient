// The GPU side of a map object group: the three buffers it draws from, how they are bound,
// and how the parsed chunk arrays reach them. Reference module MapChunk.cpp, which serves
// terrain chunks and map object groups from the same helpers.

#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/VBBList.hpp"

#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/shader/CShaderEffect.hpp"

#include <cstring>

EGxVertexBufferFormat CMapObjGroup::VertexFormat() const {
    return (this->m_state & 0x8) ? GxVBF_PNC2T2 : GxVBF_PNCT;
}

// ref: FUN_007cbcb0
void CMapObjGroup::CreateBuffers() {
    if (!this->m_vertexBuf) {
        uint32_t stride = (this->m_state & 0x8) ? 0x30 : 0x24;

        VBBList::s_vertexList.Alloc(&this->m_vertexBuf, stride, this->m_vertexCount);
    }

    // The second colour set is only built for an outdoor group drawn without shaders: the
    // fixed-function path has nowhere else to put the ambient term.
    if (!CShaderEffect::s_enableShaders
        && (this->m_mapObj->m_mohd->flags & 0x2)
        && this->m_colors
        && this->m_batchCountA
        && !this->m_colorBuf) {
        uint32_t count = this->m_batches[this->m_batchCountA - 1].maxVertex + 1;

        VBBList::s_vertexList.Alloc(&this->m_colorBuf, GxVertexBufferFormatSize(GxVBF_PNCT), count);
    }

    if (!this->m_indexBuf) {
        VBBList::s_indexList.Alloc(&this->m_indexBuf, 2, this->m_indexCount);
    }
}

// ref: FUN_007c9cb0
void CMapObjGroup::BindVertexStream() {
    EGxVertexBufferFormat format = this->VertexFormat();
    uint32_t stride = (this->m_state & 0x8) ? 0x30 : 0x24;

    CGxBuf* buf = this->m_vertexBuf
        ? this->m_vertexBuf->buf
        : g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, stride, this->m_vertexCount);

    // unk1C is "the data is in", unk1D "the item count still matches"; a buffer the pool
    // recycled comes back with one of them clear.
    if (!buf->unk1C || !buf->unk1D) {
        this->FillVertexBuffer(buf, format);
    }

    GxPrimVertexPtr(buf, format);
}

// ref: FUN_007c9d80
void CMapObjGroup::BindIndexStream() {
    CGxBuf* buf = this->m_indexBuf
        ? this->m_indexBuf->buf
        : g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, this->m_indexCount);

    if (!buf->unk1C || !buf->unk1D) {
        this->FillIndexBuffer(buf);
    }

    g_theGxDevicePtr->PrimIndexPtr(buf);
}

namespace {

// The device reports whether its colour channels come out reversed; when they do, every
// colour written into a stream is byte-swapped on the way in.
CImVector StreamColor(const CImVector& color) {
    if (g_theGxDevicePtr->Caps().m_colorFormat != GxCF_rgba) {
        return color;
    }

    CImVector out;
    out.b = color.r;
    out.g = color.g;
    out.r = color.b;
    out.a = color.a;

    return out;
}

}

// ref: FUN_007c8560
// Position, normal, colour and texture coordinates, interleaved, one vertex at a time. A
// group with no MOCV of its own draws mid grey, or black when its root asks for the
// outdoor ambient to do the work instead.
void CMapObjGroup::FillVertexBuffer(CGxBuf* buf, EGxVertexBufferFormat format) {
    if (format != GxVBF_PNCT && format != GxVBF_PNC2T2) {
        return;
    }

    auto out = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(buf));

    CImVector baseColor;
    baseColor.b = (this->m_mapObj->m_mohd->flags & 0x2) ? 0x00 : 0x7f;
    baseColor.g = baseColor.b;
    baseColor.r = baseColor.b;
    baseColor.a = 0xff;

    C2Vector zeroUv = { 0.0f, 0.0f };
    CImVector zeroColor;
    zeroColor.b = 0;
    zeroColor.g = 0;
    zeroColor.r = 0;
    zeroColor.a = 0;

    for (uint32_t i = 0; i < this->m_vertexCount; i++) {
        const C3Vector& position = this->m_vertices[i];
        const C3Vector& normal = this->m_normals[i];

        out[0] = *reinterpret_cast<const uint32_t*>(&position.x);
        out[1] = *reinterpret_cast<const uint32_t*>(&position.y);
        out[2] = *reinterpret_cast<const uint32_t*>(&position.z);
        out[3] = *reinterpret_cast<const uint32_t*>(&normal.x);
        out[4] = *reinterpret_cast<const uint32_t*>(&normal.y);
        out[5] = *reinterpret_cast<const uint32_t*>(&normal.z);

        CImVector color = this->m_colors ? StreamColor(this->m_colors[i]) : StreamColor(baseColor);
        out[6] = *reinterpret_cast<const uint32_t*>(&color);

        if (format == GxVBF_PNCT) {
            const C2Vector& uv = this->m_texCoords[i];
            out[7] = *reinterpret_cast<const uint32_t*>(&uv.x);
            out[8] = *reinterpret_cast<const uint32_t*>(&uv.y);

            out += 9;

            continue;
        }

        CImVector color2 = this->m_colors2 ? StreamColor(this->m_colors2[i]) : StreamColor(zeroColor);
        out[7] = *reinterpret_cast<const uint32_t*>(&color2);

        const C2Vector& uv = this->m_texCoords[i];
        out[8] = *reinterpret_cast<const uint32_t*>(&uv.x);
        out[9] = *reinterpret_cast<const uint32_t*>(&uv.y);

        const C2Vector& uv2 = this->m_texCoords2 ? this->m_texCoords2[i] : zeroUv;
        out[10] = *reinterpret_cast<const uint32_t*>(&uv2.x);
        out[11] = *reinterpret_cast<const uint32_t*>(&uv2.y);

        out += 12;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);

    buf->unk1C = 1;
}

// ref: FUN_007c8b90
void CMapObjGroup::FillIndexBuffer(CGxBuf* buf) {
    auto out = g_theGxDevicePtr->BufLock(buf);

    memcpy(out, this->m_indices, this->m_indexCount * sizeof(uint16_t));

    g_theGxDevicePtr->BufUnlock(buf, 0);

    buf->unk1C = 1;
}
