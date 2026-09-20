#ifndef UI_SIMPLE_C_SIMPLE_TEXTURE_HPP
#define UI_SIMPLE_C_SIMPLE_TEXTURE_HPP

#include "gx/Texture.hpp"
#include "ui/Types.hpp"
#include "ui/simple/CSimpleRegion.hpp"
#include <tempest/Vector.hpp>

class CGxShader;
class CRect;
class CRenderBatch;
class CSimpleFrame;

class CSimpleTexture : public CSimpleRegion {
    public:
        // Static variables
        static CGxShader* s_imageModePixelShaders[];
        static uint16_t s_indices[];
        static int32_t s_metatable;
        static int32_t s_objectType;
        static EGxTexFilter s_textureFilterMode;

        // Static functions
        static void CreateScriptMetaTable();
        static CGxShader* GetImageModePixelShader(TextureImageMode mode);
        static int32_t GetObjectType();
        static void Init();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        HTEXTURE m_texture = nullptr;

        // The path the texture was set from. HTEXTURE gives no way back to a file name, and
        // GetTexture() has to answer with one -- FrameXML compares it to decide whether a button's
        // art needs changing, so answering nothing makes it re-set the texture every frame.
        char m_texturePath[260] = {};
        EGxBlend m_alphaMode = GxBlend_Alpha;
        CGxShader* m_shader = s_imageModePixelShaders[0];
        // Also the animation accumulator. An animation transforms these four vertices in place
        // and marks the region dirty; nothing recomputes them from the rect until the next layout.
        C3Vector m_position[4];
        C2Vector m_texCoord[4];
        uint32_t m_nonBlocking : 1;
        uint32_t m_updateTexCoord : 1;
        uint32_t m_horizTile : 1;
        uint32_t m_vertTile : 1;

        // Virtual member functions
        virtual ~CSimpleTexture();
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual float GetWidth();
        virtual float GetHeight();
        virtual void Draw(CRenderBatch* batch);

        // An animation's per-frame contribution, applied straight to m_position. A texture
        // overrides all three in the reference and inherits only AddAnimAlpha, which belongs to
        // CSimpleRegion because that is where the colour lives.
        //
        // ref: FUN_00481740
        virtual void AddAnimTranslation(CScriptRegion* source, const C2Vector& offset);

        // ref: FUN_00481770
        virtual void AddAnimRotation(CScriptRegion* source, FRAMEPOINT point,
                                     const C2Vector& origin, float angle);

        // ref: FUN_004817a0
        virtual void AddAnimScale(CScriptRegion* source, FRAMEPOINT point, const C2Vector& origin,
                                  const C2Vector& scale);
        virtual void OnFrameSizeChanged(const CRect& rect);

        // Member functions
        CSimpleTexture(CSimpleFrame* frame, uint32_t drawlayer, int32_t show);
        void GetTexCoord(C2Vector* texCoord);
        void PostLoadXML(const XMLNode* node, CStatus* status);
        void SetAlpha(float alpha);
        void SetBlendMode(EGxBlend blend);
        void SetPosition(const CRect& rect, C3Vector* position);
        void SetShader(CGxShader* shader);
        void SetTexCoord(const CRect& texRect);
        void SetTexCoord(const C2Vector* texCoord);
        int32_t SetTexture(const char* fileName, bool wrapU, bool wrapV, EGxTexFilter filter, TextureImageMode mode);
        int32_t SetTexture(const CImVector& color);

        // Take an already-created texture. Used for pixels the client generates rather than loads
        // -- the cinematic surface is one. Ownership passes to the region: the previous handle is
        // closed, and this one is closed when the next is set.
        int32_t SetTextureHandle(HTEXTURE texture);
};

#endif
