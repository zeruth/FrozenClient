#ifndef UI_GAME_C_G_CAMERA_HPP
#define UI_GAME_C_G_CAMERA_HPP

#include "ui/simple/CSimpleCamera.hpp"
#include "util/GUID.hpp"

class CGObject_C;
class CM2Model;

class CGCamera : public CSimpleCamera {
    public:
        // Public structs
        struct CameraViewData {
            const char* m_distance;
            const char* m_pitch;
            const char* m_yaw;
        };

        // Public static variables
        static CameraViewData s_cameraViewDataDefault[];

        // Public static functions
        static int32_t UpdateCallback(const void*, void* param);

        // Virtual public member functions
        virtual ~CGCamera() = default;
        virtual float FOV() const;
        virtual C3Vector Forward() const;
        virtual C3Vector Right() const;
        virtual C3Vector Up() const;

        // Public member functions
        CGCamera();
        void CalcModelCamera(uint32_t timestamp);
        void CalcTargetCamera(CGObject_C* target, uint32_t timestamp);
        void CheckUnderwater();
        bool IsUnderwater() const { return this->m_underwater; }
        const WOWGUID& GetTarget() const;
        void SetTarget(const WOWGUID& target);
        void Rotate(float deltaYaw, float deltaPitch);
        void Zoom(float deltaDistance);
        int32_t HasModel() const;
        C33Matrix ParentToWorld() const;
        void SetupWorldProjection(const CRect& projRect);
        C3Vector Target() const;

        // Setters driven from 0x0074c0e0 and from the camera itself (0x005ff950, 0x005ffa60).
        // Their fields are named by reference offset until the readers are ported.

        // ref: FUN_005fe580
        // Unless flag 0x40 is up: the first call for an index since its bit was cleared records
        // the start time, every call records the value, and the end time is start + duration (0
        // for no duration).
        void SetTimedValue(int32_t index, int32_t startTime, int32_t duration, uint32_t value);

        // ref: FUN_005fe890
        // With start == end the value is taken at once; otherwise the old value becomes the blend
        // source and the new one its target.
        void SetBlendA(uint32_t value, int32_t start, int32_t end);

        // ref: FUN_005fe8d0
        // The same for the second blend.
        void SetBlendB(uint32_t value, int32_t start, int32_t end);

        // ref: FUN_005fe910
        void SetUnk2FC(uint32_t value);

        // ref: FUN_005fe920
        // Raises flag 0x20 and stores a value that, while the flag is up, FUN_005ff320 saves in
        // place of the one at +0x1e8.
        void SetOverride2D0(float value);

        // ref: FUN_005fe940
        void ClearOverride2D0();

    private:
        // Private member variables
        CM2Model* m_model;
        // TODO
        WOWGUID m_target;
        WOWGUID guid90;
        // TODO
        WOWGUID m_relativeTo;
        // TODO
        int32_t m_view;
        // TODO
        float m_distance;
        float m_yaw;
        float m_pitch;
        float m_roll;
        // TODO
        float m_fovOffset;
        uint32_t m_unk9C = 0;           // +0x9c flags: 0x20 override at +0x2d0, 0x40 timed values locked
        uint32_t m_unk160 = 0;          // +0x160 one bit per timed value (2 * index) with a start
        int32_t m_unk164[6] = {};       // +0x164 timed value start
        int32_t m_unk194[6] = {};       // +0x194 timed value end, 0 for none
        uint32_t m_unk1AC[6] = {};      // +0x1ac timed value
        float m_unk2D0 = 0.0f;          // +0x2d0
        int32_t m_unk2D4 = 0;           // +0x2d4 blend A start
        int32_t m_unk2D8 = 0;           // +0x2d8 blend A end
        uint32_t m_unk2DC = 0;          // +0x2dc blend A source
        uint32_t m_unk2E0 = 0;          // +0x2e0 blend A target
        uint32_t m_unk2E4 = 0;          // +0x2e4 blend A value
        int32_t m_unk2E8 = 0;           // +0x2e8 blend B start
        int32_t m_unk2EC = 0;           // +0x2ec blend B end
        uint32_t m_unk2F0 = 0;          // +0x2f0 blend B source
        uint32_t m_unk2F4 = 0;          // +0x2f4 blend B target
        uint32_t m_unk2F8 = 0;          // +0x2f8 blend B value
        uint32_t m_unk2FC = 0;          // +0x2fc
        bool m_underwater = false; // set by CheckUnderwater from the world's liquid query
};

void CameraRegisterCVars();

// ref: FUN_005fe3c0
// One turn added to a negative angle or taken off one past a full turn; a single step, not a
// modulo.
float CameraWrapAngleOnce(float angle);

#endif
