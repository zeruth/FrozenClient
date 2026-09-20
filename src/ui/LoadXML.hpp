#ifndef UI_LOAD_XML_HPP
#define UI_LOAD_XML_HPP

#include "ui/Types.hpp"
#include <cstdint>

class CImVector;
class CSimpleFontString;
class CSimpleFrame;
class CSimpleTexture;
class CStatus;
class XMLNode;

// ref: FUN_00815eb0
// The <Origin> element a Rotation or Scale animation anchors around: a "point" attribute naming a
// FRAMEPOINT, and an optional <Offset> child carrying the displacement.
//
// Defaults before anything is read are CENTER and (0, 0) -- the reference writes literal 4 for the
// point, which is FRAMEPOINT_CENTER in frozen's enum too. It returns success even when the point
// name is bad: the bad name is reported and the default kept, rather than the element failing.
int32_t LoadXML_AnimOrigin(const XMLNode* node, FRAMEPOINT& point, float& offsetX, float& offsetY,
                           CStatus* status);

int32_t LoadXML_Color(const XMLNode* node, CImVector& color);

int32_t LoadXML_Dimensions(const XMLNode* node, float& x, float& y, CStatus* status);

int32_t LoadXML_Gradient(const XMLNode* node, ORIENTATION& orientation, CImVector& minColor, CImVector& maxColor, CStatus* status);

int32_t LoadXML_Insets(const XMLNode* node, float& left, float& right, float& top, float& bottom, CStatus* status);

CSimpleFontString* LoadXML_String(const XMLNode* node, CSimpleFrame* frame, CStatus* status);

CSimpleTexture* LoadXML_Texture(const XMLNode* node, CSimpleFrame* frame, CStatus* status);

int32_t LoadXML_Value(const XMLNode* node, float& value, CStatus* status);

#endif
