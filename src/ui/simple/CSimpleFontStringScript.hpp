#ifndef UI_SIMPLE_C_SIMPLE_FONT_STRING_SCRIPT_HPP
#define UI_SIMPLE_C_SIMPLE_FONT_STRING_SCRIPT_HPP

#include "ui/FrameScript.hpp"

#define NUM_SIMPLE_FONT_STRING_SCRIPT_METHODS 41

extern FrameScript_Method SimpleFontStringMethods[NUM_SIMPLE_FONT_STRING_SCRIPT_METHODS];

class CSimpleFontString;
struct lua_State;

// The reference implements every font, colour, spacing and justification method of a font string
// once, as a helper taking the owner's display name, the font string and the Lua state, and has
// both CSimpleFontString and CSimpleEditBox forward to it -- the edit box applies them to the font
// string it draws its text with. These are those helpers.
int32_t FontString_SetFontObject(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetFontObject(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetFont(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetFont(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetTextColor(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetTextColor(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetShadowColor(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetShadowColor(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetShadowOffset(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetShadowOffset(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetSpacing(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetSpacing(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetJustifyH(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetJustifyH(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetJustifyV(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetJustifyV(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_SetIndentedWordWrap(const char* displayName, CSimpleFontString* string, lua_State* L);
int32_t FontString_GetIndentedWordWrap(const char* displayName, CSimpleFontString* string, lua_State* L);


#endif
