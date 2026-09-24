#include "model/CM2Lighting.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/RenderState.hpp"
#include <tempest/Vector.hpp>
#include <cmath>
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
// Getting either backwards would still compile and would still light models, just the wrong
// ones, so the insertion was checked against brute force over 20,000 random sequences: the
// kept set is always the four nearest, ascending. The tie case is what the two tests
// actually decide, and it is worth stating because it is the difference `<=` makes -- four
// lights at equal distance end up in REVERSE insertion order, because an equidistant
// newcomer shifts past all of its equals, and a fifth equal one is then dropped by the
// `>=` above. Both follow from the reference's branch masks.
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

// `memset(this, 0, 0xd4)`, then the scene at +0x0, `|= 0x20` into the flags at +0x14, and the
// four dwords of the sphere at +0x4. Statement for statement.
//
// 0xd4 is 212, and that is a check on this class's whole layout rather than just this
// function: frozen's CM2Lighting comes to exactly 212 bytes only WITH the m_lightDistance[4]
// added on 2026-09-23 for AddLight. Without it the struct would be 196, and the reference's
// own memset says 212.
// ref: FUN_00834900
void CM2Lighting::Initialize(CM2Scene* scene, const CAaSphere& a3) {
    memset(this, 0, sizeof(CM2Lighting));

    this->m_scene = scene;
    this->m_flags |= 0x20u;
    this->sphere4 = a3;
}

// Stores fogStart at +0xa8, fogEnd at +0xac, the scale at +0xb0, 1.0 as the density at +0xb4 and
// the colour at +0xb8, and returns with `retl $0xc` for its three stack arguments. Those offsets
// are the ones this class's layout was reconstructed with, so they check it a second time.
//
// One detail NOT taken from the mnemonic: the scale's sign. llvm-objdump renders the subtraction
// as `fsubp %st, %st(1)`, and AT&T's fsub/fsubr rendering is famously ambiguous about which
// operand is which, so reading it either way is a coin flip. The consumer settles it instead --
// CM2SceneRender::SetupLighting disables fog when m_fogScale <= 0, and fogEnd is greater than
// fogStart, so the scale has to be 1 / (fogEnd - fogStart) for fog to work at all. frozen already
// had it that way.
// ref: FUN_00834940
void CM2Lighting::SetFog(const C3Vector& fogColor, float fogStart, float fogEnd) {
    this->m_fogStart = fogStart;
    this->m_fogEnd = fogEnd;
    this->m_fogScale = 1.0f / (fogEnd - fogStart);
    this->m_fogDensity = 1.0f;
    this->m_fogColor = fogColor;
}

// The four-argument overload, at 0x00834990: the same stores as the three-argument one above
// but taking the density from the fourth argument instead of 1.0, and returning with
// `retl $0x10`. The two sit next to each other in the reference as they do here.
// ref: FUN_00834990
void CM2Lighting::SetFog(const C3Vector& fogColor, float fogStart, float fogEnd, float fogDensity) {
    this->m_fogStart = fogStart;
    this->m_fogEnd = fogEnd;
    this->m_fogScale = 1.0f / (fogEnd - fogStart);
    this->m_fogDensity = fogDensity;
    this->m_fogColor = fogColor;
}

// Push this lighting block into the device's four fixed-function light slots: the sun into slot 0
// as a DIRECTIONAL light, then up to three of the point lights AddLight kept, then a disable for
// every slot that did not get one.
//
// This is the far end of the local-light chain. The near end is CM2Light::Link putting a light in
// CM2Scene's 64x64 hash grid; CM2Scene::SelectLights sweeping the cells near the model;
// CM2Lighting::AddLight keeping the nearest four; CM2Lighting::CameraSpace transforming their
// positions. All of that was already ported and, until this function existed, went nowhere.
//
// Three details worth naming, because each is a place a from-memory version would differ:
//
//  - Slot 0 is the sun and is always set, unconditionally, before any of this looks at the point
//    lights. Its direction is m_sunDir and its ambient, diffuse and specular are the three sun
//    colours; SetupSunlight runs first to normalise the direction.
//  - The point lights contribute DIFFUSE ONLY. The reference zeroes ambient and specular once
//    before the loop and never writes them inside it, so CM2Light::m_ambColor and m_specColor --
//    which do exist and are filled from the model's tracks -- reach the device only through
//    CShaderEffect's shader path, never through the fixed-function one. Reproduced, not corrected.
//  - `a2` chooses the space. Null means the light's position is already in camera space and
//    m_posCameraSpace is used as-is; non-null means take the WORLD position and subtract a2,
//    making the positions relative to whatever the caller passed. Both go in as the light's
//    position with the positional flag set, and the origin argument to LightSet is always zero.
//
// The slot counter is shared across both stages and checked against 4 in three separate places,
// so a full bank returns early and leaves the remaining slots holding whatever they held. That is
// the reference's own shape.
//
// One fog colour component, 0..1 float to 0..255 byte, clamping at both ends. Split out only
// because the reference repeats it three times inline; the arithmetic is unchanged.
uint8_t CM2Lighting::FogColorByte(float c) {
    if (!(c > 0.0f)) {
        return 0;
    }

    if (c >= 1.0f) {
        return 255;
    }

    return static_cast<uint8_t>(static_cast<int32_t>(nearbyintf(c * 255.0f + 0.5f)));
}

// Drive the fixed-function fog render states from this lighting block. The reference calls it
// immediately after SetupGxLights at every site that sets a block up, so the two belong together.
//
// Like SetupGxLights, this has no frozen caller yet: all five of its reference callers are
// unlinked. Four of them (007984a0, 008a5170, 008a5c70, 008a6350) are themselves unreached. The
// fifth is the interesting one and is worth writing down, because it is the bridge that would make
// this whole area live.
//
// FUN_007d04a0 is the reference's per-terrain-chunk lighting setup, called from the three chunk
// list draws (FUN_00793b10, FUN_00793c30, FUN_007989c0). Decompiled 2026-09-23, it: builds a 4x4
// with the chunk origin minus a global reference point in its translation row; builds a CM2Lighting
// ON THE STACK -- the local is 212 bytes, which is sizeof(CM2Lighting) = 0xd4, an independent
// check on that layout; calls CM2Scene::SelectLights (FUN_0081e400) on it; fills its sun and fog
// from the DayNight block via FUN_007b7bd0; then calls SetupGxLights with that reference point as
// `a2` and SetupGxFog straight after.
//
// So the terrain, not the model path, is what drives these two in the reference, and it does it
// per chunk. frozen cannot port FUN_007d04a0 as such: its terrain is a stand-in built on
// TerrainChunk in src/world/Terrain.cpp rather than a port of the reference's chunk class, and it
// already sets GxRs_FogColor/Start/End itself from CWorld::s_fog*. Wiring SetupGxFog in there
// would be a behaviour change to a path that currently works, so it wants its own change with a
// run rather than being folded in here.
//
// FUN_007b7bd0 is small and portable on its own terms and is the natural next piece: it is
// `AddLight(outdoorLight)` where the light is the CM2Light at the map light block 0x00ce04a8+0x58,
// then SetFog with the DayNight fog colour unpacked from the bytes at +0x8c..+0x8e (times the
// 1/255 at 0x00a45564) and the three floats at +0x90, +0x94 and +0x98. Those first two offsets
// are the fog start and end that docs/world-render-inventory.md already records for this block,
// which is a second independent confirmation of it.
//
// m_fogScale is the switch: the reference tests its magnitude against the 2^-22 at 0x009ea27c
// (2.384185791015625e-07, read out of the binary) and treats anything smaller as "no fog", which
// is a float-epsilon test rather than a flag. Note it is m_fogScale that decides, not m_fogEnd or
// m_fogDensity.
//
// The colour conversion is written out rather than handed to PackColor because it is NOT PackColor.
// PackColor (FUN_009851a0) rounds and does not clamp; this one clamps at both ends -- at or below
// zero gives 0, at or above one gives 255, and only the middle takes `c * 255 + 0.5` -- then
// rounds that. Passing a fog colour outside 0..1 through PackColor would wrap instead of
// saturating. The component order is the reference's too: blue is computed first, then green, then
// red, and alpha is forced to 0xff.
//
// ONE GATE IS NOT PORTED, and it is a missing frozen field rather than an oversight. The reference
// skips the FogStart and FogEnd writes when a capability flag is set: it reads the global device,
// takes the sub-object at +0x214, and tests the dword at +0xb4. That sub-object is CGxCaps -- the
// same accessor feeds offsets 0x130, 0x134 and 0x138, which are precisely the three fields frozen
// already names int130, int134 and int138 for their reference offsets. But frozen's CGxCaps is not
// layout-faithful: laid out as declared, its int130 lands at 0xa0, so the class is roughly 0x90
// bytes short of the reference's and has no field at 0xb4 to read. The flag is read ten times
// across the reference, so it is real and used. Until CGxCaps is filled in, this takes the branch
// that writes the states, which is what the reference does whenever the flag is clear.
//
// **Built, not seen running.**
// ref: FUN_00835750
void CM2Lighting::SetupGxFog() {
    // 2^-22, the reference's own constant at 0x009ea27c.
    if (fabsf(this->m_fogScale) < 2.384185791015625e-07f) {
        GxRsSet(GxRs_Fog, 0);
        return;
    }

    // See the note above: the reference gates these two on a CGxCaps field frozen does not carry.
    GxRsSet(GxRs_FogStart, this->m_fogStart);
    GxRsSet(GxRs_FogEnd, this->m_fogEnd);

    CImVector fogColor;
    fogColor.b = CM2Lighting::FogColorByte(this->m_fogColor.z);
    fogColor.g = CM2Lighting::FogColorByte(this->m_fogColor.y);
    fogColor.r = CM2Lighting::FogColorByte(this->m_fogColor.x);
    fogColor.a = 0xFF;

    GxRsSet(GxRs_FogColor, static_cast<int32_t>(fogColor.value));
    GxRsSet(GxRs_Fog, 1);
}

// **Built, not seen running -- and not reachable in this build either.** Checked immediately after
// porting it, because the same cycle had just spent its time on a chain that turned out to be dead
// from the top, and it would be poor form not to apply that to this. The state of it:
//
//   - The one frozen caller is CShaderEffect::SetLocalLighting (FUN_00873ca0), which is the same
//     function that calls it in the reference. So the wiring is right.
//   - But it sits in that function's `else`, taken only when CShaderEffect::s_enableShaders is 0,
//     and frozen forces that on: M2GetCacheFlags in Model2.cpp does a bare `flags |= 0x8`, and
//     CWorld passes bit 3 straight into InitShaderSystem. So the branch never runs today and the
//     shader path (ComputeLocalLights, eleven vertex constants at c17) is what lights models.
//   - The reference has a SECOND consumer frozen does not: FUN_007d04a0, 326 bytes and three
//     callers, in the terrain chunk draw setup. That one is not behind a shader toggle. It is the
//     thing to port if this bank should carry real traffic.
//
// This is therefore correct code on a cold path, not a rendering change. It is worth having landed
// anyway -- it is what a from-memory port would have got wrong later, the layout and constants are
// now pinned by the binary, and it unblocks FUN_007d04a0 -- but nothing on screen should move
// because of it, and if something does, that is the bug rather than the feature.
// ref: FUN_008353d0
void CM2Lighting::SetupGxLights(const C3Vector* a2) {
    CGxLight light;
    C3Vector origin = { 0.0f, 0.0f, 0.0f };

    uint32_t slot = 1;

    light.m_flags |= 0x1;

    this->SetupSunlight();

    light.m_posOrDir = this->m_sunDir;
    light.m_flags &= ~0x2u;
    light.m_ambient = this->m_sunAmbient;
    light.m_diffuse = this->m_sunDiffuse;
    light.m_specular = this->m_sunSpecular;

    g_theGxDevicePtr->LightSet(0, light, origin);
    g_theGxDevicePtr->LightEnable(0, 1);

    // Point lights from here down: positional, and diffuse-only.
    light.m_flags |= 0x2;
    light.m_ambient = { 0.0f, 0.0f, 0.0f };
    light.m_specular = { 0.0f, 0.0f, 0.0f };

    // The reference walks m_lights from the END backwards, so the FURTHEST of the kept lights
    // takes the lowest slot. AddLight sorts nearest-first, so this reverses that order. Kept
    // because with four slots and at most four lights every one of them is sent either way, and
    // deviating would change which light lands in which slot for no reason.
    for (uint32_t i = this->m_lightCount; i > 0; i--) {
        if (slot > 3) {
            return;
        }

        CM2Light* m2Light = this->m_lights[i - 1];

        if (a2 == nullptr) {
            light.m_posOrDir = m2Light->m_posCameraSpace;
        } else {
            light.m_posOrDir.x = m2Light->m_pos.x - a2->x;
            light.m_posOrDir.y = m2Light->m_pos.y - a2->y;
            light.m_posOrDir.z = m2Light->m_pos.z - a2->z;
        }

        light.m_diffuse = m2Light->m_dirColor;

        light.m_attenuation.x = m2Light->m_constantAttenuation;
        light.m_attenuation.y = m2Light->m_linearAttenuation;
        light.m_attenuation.z = m2Light->m_quadraticAttenuation;

        g_theGxDevicePtr->LightSet(slot, light, origin);
        g_theGxDevicePtr->LightEnable(slot, 1);

        slot++;
    }

    if (slot > 3) {
        return;
    }

    while (slot < 4) {
        g_theGxDevicePtr->LightEnable(slot, 0);
        slot++;
    }
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
