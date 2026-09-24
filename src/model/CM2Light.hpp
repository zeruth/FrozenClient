#ifndef MODEL_C_M2_LIGHT_HPP
#define MODEL_C_M2_LIGHT_HPP

#include "model/M2Types.hpp"
#include <tempest/Vector.hpp>

class CM2Scene;

class CM2Light {
    public:
        // Member variables
        CM2Scene* m_scene = nullptr;
        // The frame this light was last updated on, compared against CM2Scene::uint14. The
        // reference keeps it at +0x4 -- the one dword of CM2Light whose purpose was unknown when
        // the layout was first worked out. CM2Model stamps it every frame; CM2Scene::SelectLights
        // uses it to notice a light nobody is driving any more and switch it off.
        uint32_t m_updateStamp = 0;
        int32_t m_type = 1;
        C3Vector m_pos;
        // The light's position in CAMERA space, written by CM2Lighting::CameraSpace once per model
        // per frame and read by CShaderEffect::ComputeLocalLights. This field was missing: the
        // reference's CM2Light carries six vectors between +0x0c and +0x53 and frozen had five, so
        // both of those functions had to stay stubs for want of somewhere to put this. The
        // reference keeps it at +0x18, immediately after m_pos, which is where it goes here too.
        C3Vector m_posCameraSpace;
        C3Vector m_dir;
        C3Vector m_ambColor;
        C3Vector m_dirColor;
        C3Vector m_specColor;
        float m_constantAttenuation = 0.0f;
        float m_linearAttenuation = 0.69999999f;
        float m_quadraticAttenuation = 0.029999999f;
        int32_t m_visible = 0;
        CM2Light** m_lightPrev = nullptr;
        CM2Light* m_lightNext = nullptr;

        // Member functions
        CM2Light();
        void Initialize(CM2Scene* scene);
        void Link();
        void SetDirection(const C3Vector& dir);
        void SetPosition(const C3Vector& pos);
        void SetLightType(M2LIGHTTYPE lightType);
        void SetVisible(int32_t visible);
        void Unlink();
};

#endif
