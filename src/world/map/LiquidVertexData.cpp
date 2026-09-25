#include "world/map/LiquidVertexData.hpp"

namespace Liquid {

const uint8_t s_noCoord[4] = { 0, 0, 0, 0 };

// ref: FUN_008a31e0
CVertexDataMCLQ::CVertexDataMCLQ(const uint8_t* verts, uint32_t format) {
    this->m_verts = verts;
    this->m_format = format;
}

// ref: FUN_008a3200
// An MCLQ vertex is eight bytes: the layout-specific first four, then the height. Layouts 2 and
// 3 keep no height, so the surface sits flat at the layer's own.
float CVertexDataMCLQ::GetHeight(uint32_t index) const {
    if (this->m_format >= 2) {
        return 0.0f;
    }

    return *reinterpret_cast<const float*>(this->m_verts + index * 8 + 4);
}

// ref: FUN_008a3220
// The depth is the first byte, in the two layouts that carry one.
uint8_t CVertexDataMCLQ::GetDepth(uint32_t index) const {
    if (this->m_format != 0 && this->m_format != 2) {
        return 0;
    }

    return this->m_verts[index * 8];
}

// ref: FUN_008a3250
// Magma and slime replace the depth byte with a coordinate pair, in the same four bytes.
const uint8_t* CVertexDataMCLQ::GetCoord(uint32_t index) const {
    if (this->m_format != 1) {
        return Liquid::s_noCoord;
    }

    return this->m_verts + index * 8;
}

// ref: FUN_008a3280
CVertexDataMH2O::CVertexDataMH2O(const CMapLiquidData* data, const SMLiquidInstance* layer) {
    this->m_body = data->m_body;
    this->m_layer = layer;

    // The layer covers a rectangle of tiles, so it has one more vertex than tiles each way.
    this->m_vertexCount = (layer->tileHeight + 1) * (layer->tileWidth + 1);
}

// ref: FUN_008a3130
// MH2O keeps each array whole rather than interleaving them, and the heights come first.
float CVertexDataMH2O::GetHeight(uint32_t index) const {
    if (this->m_layer->material >= 2) {
        return 0.0f;
    }

    return *reinterpret_cast<const float*>(
        this->m_body + this->m_layer->vertexOffset + index * 4);
}

// ref: FUN_008a3160
// The depths follow the heights when there are any, and stand alone when there are not. An
// ocean layer with no vertex data at all is fully deep rather than dry.
uint8_t CVertexDataMH2O::GetDepth(uint32_t index) const {
    if (this->m_layer->material == 0) {
        return *(this->m_body + this->m_layer->vertexOffset + this->m_vertexCount * 4 + index);
    }

    if (this->m_layer->material != 2) {
        return 0;
    }

    if (!this->m_layer->vertexOffset) {
        return 0xff;
    }

    return *(this->m_body + this->m_layer->vertexOffset + index);
}

// ref: FUN_008a31b0
// The coordinates follow the heights, a pair of signed bytes each but stepped as four.
const uint8_t* CVertexDataMH2O::GetCoord(uint32_t index) const {
    if (this->m_layer->material != 1) {
        return Liquid::s_noCoord;
    }

    return this->m_body + this->m_layer->vertexOffset + (this->m_vertexCount + index) * 4;
}

}
