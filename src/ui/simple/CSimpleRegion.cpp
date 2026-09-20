#include "ui/simple/CSimpleRegion.hpp"
#include <cmath>
#include "ui/simple/CSimpleFrame.hpp"
#include <cstring>

CSimpleRegion::~CSimpleRegion() {
    this->SetFrame(nullptr, DRAWLAYER_BACKGROUND_BORDER, 0);
}

CSimpleRegion::CSimpleRegion(CSimpleFrame* frame, uint32_t drawlayer, int32_t show) : CScriptRegion() {
    memset(this->m_color, 0, sizeof(CImVector) * this->m_colorCount);
    this->m_colorCount = 0;

    memset(this->m_alpha, 0, sizeof(uint8_t) * this->m_alphaCount);
    this->m_alphaCount = 0;

    if (frame) {
        this->SetFrame(frame, drawlayer, show);
    }
}

void CSimpleRegion::GetVertexColor(CImVector& color) const {
    if (this->m_colorCount == 1) {
        color.a = this->m_alpha[0];
        color.r = this->m_color[0].r;
        color.g = this->m_color[0].g;
        color.b = this->m_color[0].b;
    } else {
        color = { 0xFF, 0xFF, 0xFF, 0xFF };
    }
}

// ref: FUN_0048b890
// Where a rotation or a scale pivots. The FRAMEPOINT picks a corner or an edge midpoint of the
// quad as it stands NOW -- already translated and rotated by anything earlier in the group -- and
// the origin offset is then added in the region's OWN frame rather than the screen's.
//
// That last part is what the tail of the reference does: it takes the quad's local up direction
// (bottom-right minus bottom-left), normalises it, and rotates the offset by it. A region turned
// on its side therefore has its origin offset turn with it, which is the behaviour you would want
// and not the one you would get by adding the offset directly.
static void AnimQuadPivot(const C3Vector position[4], FRAMEPOINT point, const C2Vector& origin,
                          C2Vector& pivot) {
    // 0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right.
    switch (point) {
        case FRAMEPOINT_TOPLEFT:     pivot.x = position[0].x; pivot.y = position[0].y; break;
        case FRAMEPOINT_TOPRIGHT:    pivot.x = position[2].x; pivot.y = position[2].y; break;
        case FRAMEPOINT_BOTTOMLEFT:  pivot.x = position[1].x; pivot.y = position[1].y; break;
        case FRAMEPOINT_BOTTOMRIGHT: pivot.x = position[3].x; pivot.y = position[3].y; break;

        case FRAMEPOINT_TOP:
            pivot.x = (position[0].x + position[2].x) * 0.5f;
            pivot.y = (position[0].y + position[2].y) * 0.5f;
            break;

        case FRAMEPOINT_LEFT:
            pivot.x = (position[0].x + position[1].x) * 0.5f;
            pivot.y = (position[0].y + position[1].y) * 0.5f;
            break;

        case FRAMEPOINT_CENTER:
            pivot.x = (position[0].x + position[3].x) * 0.5f;
            pivot.y = (position[0].y + position[3].y) * 0.5f;
            break;

        case FRAMEPOINT_RIGHT:
            pivot.x = (position[2].x + position[3].x) * 0.5f;
            pivot.y = (position[2].y + position[3].y) * 0.5f;
            break;

        case FRAMEPOINT_BOTTOM:
            pivot.x = (position[1].x + position[3].x) * 0.5f;
            pivot.y = (position[1].y + position[3].y) * 0.5f;
            break;

        default:
            pivot.x = 0.0f;
            pivot.y = 0.0f;
            break;
    }

    if (origin.x * origin.x + origin.y * origin.y <= 1.1920928955078125e-07f) {
        return;
    }

    float ux = position[3].x - position[1].x;
    float uy = position[3].y - position[1].y;

    float lengthSquared = ux * ux + uy * uy;

    if (lengthSquared > 2.384185791015625e-07f) {
        float inverse = 1.0f / sqrtf(lengthSquared);
        ux *= inverse;
        uy *= inverse;
    }

    pivot.x += origin.x * ux - origin.y * uy;
    pivot.y += origin.y * ux + origin.x * uy;
}

// ref: FUN_0048b7b0
void AnimQuadTranslate(C3Vector position[4], const C2Vector& offset) {
    for (int32_t i = 0; i < 4; i++) {
        position[i].x += offset.x;
        position[i].y += offset.y;
    }
}

// ref: FUN_0048ba80
void AnimQuadRotate(C3Vector position[4], FRAMEPOINT point, const C2Vector& origin, float angle) {
    C2Vector pivot;
    AnimQuadPivot(position, point, origin, pivot);

    float c = cosf(angle);
    float s = sinf(angle);

    for (int32_t i = 0; i < 4; i++) {
        float x = position[i].x - pivot.x;
        float y = position[i].y - pivot.y;

        position[i].x = x * c - y * s + pivot.x;
        position[i].y = y * c + x * s + pivot.y;
    }
}

// ref: FUN_0048bb80
// The incoming vector is the COMPLEMENT of the scale, already multiplied by the animation's
// progress -- see CSimpleScaleAnim::OnApply. That is what makes this a plain lerp toward the
// pivot: at a complement of 0 nothing moves, and at 1 every vertex lands on the pivot, which is a
// scale of zero. Storing the scale itself would need a different expression here.
void AnimQuadScale(C3Vector position[4], FRAMEPOINT point, const C2Vector& origin,
                   const C2Vector& scale) {
    C2Vector pivot;
    AnimQuadPivot(position, point, origin, pivot);

    for (int32_t i = 0; i < 4; i++) {
        position[i].x += (pivot.x - position[i].x) * scale.x;
        position[i].y += (pivot.y - position[i].y) * scale.y;
    }
}

// ref: FUN_00487ce0
// Add a delta to the region's alpha, clamped to a byte, and write the colour back.
//
// CUMULATIVE, deliberately. It reads the colour that is there now rather than recomputing one from
// the animation's progress, which is why an alpha animation has to take last frame's contribution
// off before putting this frame's on -- see CSimpleAnim::OnUnapply. Getting that pair wrong makes
// a region fade a little further every frame instead of breaking visibly.
//
// The source region is ignored, as it is in the reference: the contribution lands on whichever
// region the animation's group points at, which is already this one.
//
// GetVertexColor supplies opaque white when no colour is set, matching the reference's own
// fallback for a region whose colour count is not exactly one.
void CSimpleRegion::AddAnimAlpha(CScriptRegion* source, int16_t delta) {
    CImVector color;
    this->GetVertexColor(color);

    int32_t alpha = static_cast<int32_t>(color.a) + delta;

    if (alpha > 0xFF) {
        alpha = 0xFF;
    } else if (alpha < 0) {
        alpha = 0;
    }

    color.a = static_cast<uint8_t>(alpha);

    this->SetVertexColor(color);
}

void CSimpleRegion::Hide() {
    this->m_shown = 0;
    this->HideThis();
}

void CSimpleRegion::HideThis() {
    if (this->m_visible && this->m_parent) {
        if (!this->m_parent->m_loading) {
            this->SetDeferredResize(1);
        }

        this->m_parent->RemoveFrameRegion(this, this->m_drawlayer);
        this->m_visible = 0;
    }
}

bool CSimpleRegion::IsShown() {
    return this->m_shown == 1;
}

bool CSimpleRegion::IsVisible() {
    return this->m_visible == 1;
}

void CSimpleRegion::OnColorChanged(bool a2) {
    if (this->m_parent) {
        uint8_t effectiveAlpha = this->m_parent->m_alpha * this->m_parent->alphaBD / 255;

        if (effectiveAlpha < 254) {
            if (this->m_colorCount == 0) {
                this->m_alphaCount = 1;
                this->m_colorCount = 1;

                this->m_alpha[0] = 255;

                this->m_color[0].r = 255;
                this->m_color[0].g = 255;
                this->m_color[0].b = 255;

                a2 = true;
            }
        } else {
            if (this->m_colorCount) {
                bool clearColors = true;

                for (uint32_t i = 0; i < this->m_colorCount; i++) {
                    auto alpha = this->m_alpha[i];
                    auto& color = this->m_color[i];

                    // If any color is set to a non-default value, do not clear colors
                    if (alpha < 254 || color.r != 255 || color.g != 255 || color.b != 255) {
                        clearColors = false;
                        break;
                    }
                }

                // If all colors were set to default values, clear colors
                if (clearColors) {
                    this->m_colorCount = 0;
                    this->m_alphaCount = 0;

                    a2 = true;
                }
            }
        }

        for (uint32_t i = 0; i < this->m_colorCount; i++) {
            this->m_color[i].a = this->m_alpha[i] * effectiveAlpha / 255;
        }
    }

    if (a2) {
        this->OnRegionChanged();
    }
}

void CSimpleRegion::OnRegionChanged() {
    if (this->m_visible && this->m_parent) {
        this->m_parent->NotifyDrawLayerChanged(this->m_drawlayer);
    }
}

void CSimpleRegion::SetVertexColor(const CImVector& color) {
    if (
        this->m_colorCount == 0
        && color.a >= 0xFE
        && color.r == 0xFF
        && color.g == 0xFF
        && color.b == 0xFF
    ) {
        return;
    }

    if (
        this->m_colorCount == 1
        && this->m_alpha[0] == color.a
        && this->m_color[0].r == color.r
        && this->m_color[0].g == color.g
        && this->m_color[0].b == color.b
    ) {
        return;
    }

    bool b1 = false;

    if (this->m_colorCount != 1) {
        this->m_colorCount = 1;
        this->m_alphaCount = 1;

        b1 = true;
    }

    this->m_color[0] = color;
    this->m_alpha[0] = color.a;

    this->OnColorChanged(b1);
}

void CSimpleRegion::SetVertexGradient(ORIENTATION orientation, const CImVector& minColor, const CImVector& maxColor) {
    bool b1 = false;

    if (this->m_colorCount != 4) {
        this->m_alphaCount = 4;
        this->m_colorCount = 4;
        b1 = true;
    }

    if (orientation == ORIENTATION_VERTICAL) {
        this->m_alpha[1] = minColor.a;
        this->m_color[1] = minColor;
        this->m_alpha[3] = minColor.a;
        this->m_color[3] = minColor;

        this->m_alpha[0] = maxColor.a;
        this->m_color[0] = maxColor;
        this->m_alpha[2] = maxColor.a;
        this->m_color[2] = maxColor;
    } else {
        this->m_alpha[0] = minColor.a;
        this->m_color[0] = minColor;
        this->m_alpha[1] = minColor.a;
        this->m_color[1] = minColor;

        this->m_alpha[2] = maxColor.a;
        this->m_color[2] = maxColor;
        this->m_alpha[3] = maxColor.a;
        this->m_color[3] = maxColor;
    }

    this->OnColorChanged(b1);
}

void CSimpleRegion::SetFrame(CSimpleFrame* frame, uint32_t drawlayer, int32_t show) {
    if (this->m_parent == frame) {
        if (this->m_drawlayer == drawlayer) {
            if (show != this->m_shown) {
                if (show) {
                    this->Show();
                } else {
                    this->Hide();
                }
            }
        } else {
            if (this->m_shown) {
                this->Hide();
            }

            this->m_drawlayer = drawlayer;

            if (show) {
                this->Show();
            }
        }
    } else {
        if (this->m_parent) {
            this->HideThis();
            this->m_parent->UnregisterRegion(this);
        }

        this->m_parent = frame;
        this->m_drawlayer = drawlayer;

        if (frame) {
            frame->RegisterRegion(this);
            this->SetDeferredResize(static_cast<CLayoutFrame*>(this->m_parent)->m_flags & 0x2);
            this->OnColorChanged(0);

            if (show) {
                this->Show();
            } else {
                this->Hide();
            }
        }
    }
}

void CSimpleRegion::Show() {
    this->m_shown = 1;
    this->ShowThis();
}

void CSimpleRegion::ShowThis() {
    if (this->m_shown && this->m_parent && this->m_parent->m_visible && !this->m_visible) {
        if (!this->m_parent->m_loading) {
            this->SetDeferredResize(0);
        }

        this->m_parent->AddFrameRegion(this, this->m_drawlayer);
        this->m_visible = 1;
    }
}
