#include "model/CM2Lighting.hpp"
#include "model/CM2Light.hpp"
#include "model/CM2Scene.hpp"
#include <cstring>

void CM2Lighting::AddAmbient(const C3Vector& ambColor) {
    this->m_sunAmbient = this->m_sunAmbient + ambColor;
}

void CM2Lighting::AddDiffuse(const C3Vector& dirColor, const C3Vector& dir) {
    C3Vector viewDir = dir;

    if (this->m_scene) {
        viewDir = {
            this->m_scene->m_view.a0 * dir.x + this->m_scene->m_view.b0 * dir.y + this->m_scene->m_view.c0 * dir.z,
            this->m_scene->m_view.a1 * dir.x + this->m_scene->m_view.b1 * dir.y + this->m_scene->m_view.c1 * dir.z,
            this->m_scene->m_view.a2 * dir.x + this->m_scene->m_view.b2 * dir.y + this->m_scene->m_view.c2 * dir.z
        };
    }

    this->vector18.x = viewDir.x * dirColor.x + this->vector18.x;
    this->vector18.y = viewDir.y * dirColor.x + this->vector18.y;
    this->vector18.z = viewDir.z * dirColor.x + this->vector18.z;

    this->vector24.x = viewDir.x * dirColor.y + this->vector24.x;
    this->vector24.y = viewDir.y * dirColor.y + this->vector24.y;
    this->vector24.z = viewDir.z * dirColor.y + this->vector24.z;

    this->vector30.x = viewDir.x * dirColor.z + this->vector30.x;
    this->vector30.y = viewDir.y * dirColor.z + this->vector30.y;
    this->vector30.z = viewDir.z * dirColor.z + this->vector30.z;

    float v7 = dirColor.y * 0.71516001f + dirColor.x * 0.212671f + dirColor.z * 0.072168998f;

    this->vector3C.x = viewDir.x * v7 + this->vector3C.x;
    this->vector3C.y = viewDir.y * v7 + this->vector3C.y;
    this->vector3C.z = viewDir.z * v7 + this->vector3C.z;

    this->vector48.x = dirColor.x + this->vector48.x;
    this->vector48.y = dirColor.y + this->vector48.y;
    this->vector48.z = dirColor.z + this->vector48.z;

    this->m_sunDir = dir;

    this->m_sunDiffuse = dirColor;
}

// The gate on the whole local-light path, and its point-light branch was empty -- so m_lightCount
// stayed 0, CameraSpace's loop never ran and ComputeLocalLights was never called. Everything built
// for local lights last cycle was dead until this.
//
// A point light is kept only if it is among the FOUR NEAREST to sphere4's centre, and the four are
// held sorted by squared distance, nearest first. When the set is full a candidate farther than the
// worst is dropped outright; otherwise the worst is evicted and the newcomer insertion-sorted into
// place.
//
// Two comparisons decide it and both are x87 compare-and-branch, so they are worth spelling out:
//
//   full-set test   `fcoms 0xa0; testb $0x5, %ah; jp drop` -- mask 0x5 is C0 (less) and C2
//                   (unordered), and jp is taken when both are clear, i.e. dist2 is NOT less than
//                   the worst kept. So a tie is dropped: the test is `>=`.
//   shift test      `fcoms; testb $0x41, %ah; je stop` -- mask 0x41 is C3 (equal) and C0 (less),
//                   and je is taken when both are clear, i.e. strictly greater. So the loop keeps
//                   shifting while dist2 <= the neighbour: the test is `<=`, not `<`.
//
// Getting either backwards would still compile and would still light models, just the wrong ones.
// ref: FUN_00834f60
void CM2Lighting::AddLight(CM2Light* light) {
    if (!light->m_visible) {
        return;
    }

    if (light->m_type == 1) {
        C3Vector d = {
            light->m_pos.x - this->sphere4.c.x,
            light->m_pos.y - this->sphere4.c.y,
            light->m_pos.z - this->sphere4.c.z
        };

        float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;

        uint32_t i = this->m_lightCount;

        if (i >= 4) {
            if (dist2 >= this->m_lightDistance[3]) {
                return;
            }

            i--;
        }

        while (i != 0 && dist2 <= this->m_lightDistance[i - 1]) {
            this->m_lights[i] = this->m_lights[i - 1];
            this->m_lightDistance[i] = this->m_lightDistance[i - 1];
            i--;
        }

        this->m_lightDistance[i] = dist2;
        this->m_lights[i] = light;

        if (this->m_lightCount < 4) {
            this->m_lightCount++;
        }
    } else {
        this->AddAmbient(light->m_ambColor);
        this->AddDiffuse(light->m_dirColor, light->m_dir);
        this->AddSpecular(light->m_specColor);
    }
}

void CM2Lighting::AddSpecular(const C3Vector& specColor) {
    this->m_sunSpecular = this->m_sunSpecular + specColor;
}

// Transforms each selected light's position into camera space, once per model per frame, so that
// ComputeLocalLights can pack the results into shader constants without redoing the work per
// light per draw.
//
// The reference walks m_lights at +0x84 for m_lightCount at +0xa4, and for each one calls
// `operator*(out, &light->m_pos, scene + 0x84)` -- the C3Vector by C44Matrix transform at
// FUN_004c21b0 -- then stores the result at light + 0x18. That destination is the field this port
// had to add to CM2Light; without it there was nowhere to put the answer, which is why this and
// ComputeLocalLights were both stubs.
//
// The early-out on flag 0x1 and the null-scene test are the reference's own. Nothing in frozen
// sets 0x1, so the first never fires today; it is kept because dropping a guard is how a port
// starts diverging quietly.
//
// NOT PORTED: the reference follows the loop with a second block gated on
// `(m_flags & 0x60) == 0x60`, working on the fields at +0xc4..+0xd0 against the sun direction.
// Nothing in frozen ever sets 0x40, so that block is unreachable here -- Initialize sets 0x20 and
// SetupSunlight sets 0x2, and those are the only two writers. It is left out rather than guessed
// at, and this comment is the record that it exists.
// ref: FUN_008350a0
void CM2Lighting::CameraSpace() {
    if (this->m_flags & 0x1) {
        return;
    }

    if (!this->m_scene) {
        return;
    }

    for (uint32_t i = 0; i < this->m_lightCount; i++) {
        CM2Light* light = this->m_lights[i];

        light->m_posCameraSpace = light->m_pos * this->m_scene->m_view;
    }
}

void CM2Lighting::Initialize(CM2Scene* scene, const CAaSphere& a3) {
    memset(this, 0, sizeof(CM2Lighting));

    this->m_scene = scene;
    this->m_flags |= 0x20u;
    this->sphere4 = a3;
}

void CM2Lighting::SetFog(const C3Vector& fogColor, float fogStart, float fogEnd) {
    this->m_fogStart = fogStart;
    this->m_fogEnd = fogEnd;
    this->m_fogScale = 1.0f / (fogEnd - fogStart);
    this->m_fogDensity = 1.0f;
    this->m_fogColor = fogColor;
}

void CM2Lighting::SetFog(const C3Vector& fogColor, float fogStart, float fogEnd, float fogDensity) {
    this->m_fogStart = fogStart;
    this->m_fogEnd = fogEnd;
    this->m_fogScale = 1.0f / (fogEnd - fogStart);
    this->m_fogDensity = fogDensity;
    this->m_fogColor = fogColor;
}

// FUN_008353d0, identified while locating CameraSpace: it builds a 0x64-byte light structure on
// the stack through FUN_00683fb0, sets bit 0 of its first dword, calls SetupSunlight, copies the
// CM2Lighting fields from +0x54 to +0x80 into it and hands it to the device through the virtual at
// +0x118 with index 0 -- the fixed-function GxLightSet.
//
// Left a stub on purpose. This is the FIXED-FUNCTION sibling of the local-light path: SetLocalLighting
// calls it only in its `else`, when shaders are off, and frozen's world draws with shaders. Porting
// it needs the device-side light state that CGxDeviceD3d::IStateSyncLights also waits on, which is
// recorded against FUN_006a43d0 -- CGxDevice carries no light array at all. The two belong to one
// change, not this one.
// ref: FUN_008353d0
void CM2Lighting::SetupGxLights(const C3Vector* a2) {
    // TODO -- see above; needs the CGxDevice light state first
}

// Found while locating CameraSpace: the reference at 0x00835280 loads the three floats at
// CM2Lighting + 0x78, squares and sums them, compares against the 1e-5 at 0x009ea558, and on
// the small side writes {0, 0, -1.0} (the -1.0 being 0x009e2ef4) while on the other it calls
// the normalize at 0x004c3600. That is this function statement for statement, and it puts
// m_sunDir at +0x78.
// ref: FUN_00835280
void CM2Lighting::SetupSunlight() {
    if (this->m_flags & 0x2) {
        return;
    }

    this->m_sunDir.x = this->vector3C.x;
    this->m_sunDir.y = this->vector3C.y;
    this->m_sunDir.z = this->vector3C.z;

    if (this->m_sunDir.SquaredMag() <= 0.0000099999997) {
        this->m_sunDir = { 0.0f, 0.0f, -1.0f };
    } else {
        this->m_sunDir.Normalize();
    }

    float v6 = this->m_sunDir.z * this->vector18.z + this->m_sunDir.y * this->vector18.y + this->m_sunDir.x * this->vector18.x;
    float v7 = this->m_sunDir.z * this->vector24.z + this->m_sunDir.y * this->vector24.y + this->m_sunDir.x * this->vector24.x;
    float v8 = this->m_sunDir.z * this->vector30.z + this->m_sunDir.y * this->vector30.y + this->m_sunDir.x * this->vector30.x;

    this->m_sunDiffuse = {
        v6 * 1.25f - this->vector48.x * 0.25f,
        v7 * 1.25f - this->vector48.y * 0.25f,
        v8 * 1.25f - this->vector48.z * 0.25f
    };

    this->m_sunAmbient = {
        (this->vector48.x - v6) * 0.25f + this->m_sunAmbient.x,
        (this->vector48.y - v7) * 0.25f + this->m_sunAmbient.y,
        (this->vector48.z - v8) * 0.25f + this->m_sunAmbient.z
    };

    this->m_flags |= 0x2;
}
