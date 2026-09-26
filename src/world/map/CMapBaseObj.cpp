#include "world/map/CMapBaseObj.hpp"

CMapBaseObj::CMapBaseObj() = default;

uint32_t CMapBaseObj::GetType() {
    return this->m_type;
}
