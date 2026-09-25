#include "world/map/CMapLiquidData.hpp"

const uint8_t CMapLiquidData::s_maskNone[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

const uint8_t CMapLiquidData::s_maskAll[8] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// ref: FUN_008a3050
CMapLiquidData::CMapLiquidData() {
    this->m_body = nullptr;
}

// ref: FUN_007d4f10
void CMapLiquidData::SetBody(const uint8_t* body) {
    this->m_body = body;
}

// ref: FUN_008a3060
bool CMapLiquidData::HasLiquid() const {
    return this->m_body != nullptr;
}

// ref: FUN_008a3070
const SMLiquidChunk* CMapLiquidData::Chunk(uint32_t x, uint32_t y) const {
    return reinterpret_cast<const SMLiquidChunk*>(this->m_body) + (y * 16 + x);
}

// ref: FUN_008a3090
const SMLiquidInstance* CMapLiquidData::Layer(const SMLiquidChunk* chunk, uint32_t layer) const {
    return reinterpret_cast<const SMLiquidInstance*>(this->m_body + chunk->instanceOffset) + layer;
}

// ref: FUN_008a30b0
const uint8_t* CMapLiquidData::FishableMask(const SMLiquidChunk* chunk) const {
    if (!chunk->layerCount) {
        return CMapLiquidData::s_maskNone;
    }

    if (!chunk->attributeOffset) {
        return CMapLiquidData::s_maskAll;
    }

    return this->m_body + chunk->attributeOffset;
}

// ref: FUN_008a30e0
const uint8_t* CMapLiquidData::DeepMask(const SMLiquidChunk* chunk) const {
    if (!chunk->layerCount) {
        return CMapLiquidData::s_maskNone;
    }

    if (!chunk->attributeOffset) {
        return CMapLiquidData::s_maskAll;
    }

    // The deep mask is the second half of the attribute block; the fishable one is the first.
    return this->m_body + chunk->attributeOffset + 8;
}

// ref: FUN_008a3110
const uint8_t* CMapLiquidData::ExistsMask(const SMLiquidInstance* layer) const {
    if (!layer->existsOffset) {
        return CMapLiquidData::s_maskAll;
    }

    return this->m_body + layer->existsOffset;
}
