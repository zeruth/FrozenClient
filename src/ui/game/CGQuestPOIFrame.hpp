#ifndef UI_GAME_C_G_QUEST_POI_FRAME_HPP
#define UI_GAME_C_G_QUEST_POI_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include <tempest/Vector.hpp>

class CGQuestPOIFrame : public CSimpleFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();

        // Without this the frame answers only to its base's types, and every one of its own
        // script methods fails the This() check with "Wrong object type for member function" --
        // the methods are registered and reachable, the object just denies being what it is.
        virtual bool IsA(int32_t type);
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        // The tunables the script surface reaches, in the reference's own order so the offsets line
        // up: fill alpha +0x2a4, border alpha +0x2a5, border scalar +0x2a8, smoothing +0x2ac,
        // merging +0x2ad, merge threshold +0x2b0, spline points +0x2b4, tooltip count +0x2bc.
        //
        // Nothing draws points of interest yet, so these record what was asked for. The defaults
        // are frozen's own -- the reference sets them in a constructor that has not been
        // decompiled -- except the thirty spline points, which is the value its setter falls back
        // to and so is at least a number the original uses.
        uint8_t m_fillAlpha = 0;
        uint8_t m_borderAlpha = 0;
        float m_borderScalar = 0.0f;
        bool m_smoothing = false;
        bool m_merging = false;
        float m_mergeThreshold = 0.0f;
        int32_t m_numSplinePoints = 30;
        int32_t m_numTooltips = 0;
        // Four of them, immediately after the count at +0x2c0. GetTooltipIndex takes a 1-based
        // index and answers 0 for anything outside one to four rather than erroring.
        int32_t m_tooltipIndex[4] = { 0, 0, 0, 0 };

        // TODO the rest

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGQuestPOIFrame(CSimpleFrame* parent);
};

// ref: FUN_0058e5c0
// Where the infinite lines through a1-a2 and b1-b2 cross, in the xy plane with z = 0. False, and
// out untouched, when the lines are parallel to within 2^-22.
bool QuestPOILineIntersect(const C2Vector& a1, const C2Vector& a2, const C2Vector& b1, const C2Vector& b2, C3Vector& out);

#endif
