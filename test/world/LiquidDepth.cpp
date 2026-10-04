#include "gx/Device.hpp"
#include "gx/Transform.hpp"
#include "gx/d3d/CGxDeviceD3d.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "catch.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

// Water must land in the depth buffer on the same scale as everything else, or it z-fights the
// scene: the liquid shaders (vsLiquidWater and friends, out of patch.MPQ) take their projection as
// constants c0..c3, and the reference copies those from the device's NATIVE projection (+0xfc8,
// 0x008a33df) -- the matrix with depth remapped to D3D's [0, 1] -- not the application projection at
// +0xf88. Frozen read the application one until 2026-10-04, which put water at depth 1 - 2n/z
// against the scene's 1 - n/z: drawn as if half as far away, bleeding through whatever stood in
// front of it.
//
// A CGxDeviceD3d is built here without opening a device; the transform code is all CPU-side.

namespace {

const float NEAR_Z = 0.4f;
const float FAR_Z = 4000.0f;

// The depth a row-vector projection gives a view-space point straight ahead at distance z.
float DepthAt(const C44Matrix& p, float z) {
    float clipZ = z * p.c2 + p.d2;
    float clipW = z * p.c3 + p.d3;
    return clipZ / clipW;
}

// The device object is far too large for the stack, so it lives on the heap.
struct DeviceScope {
    CGxDeviceD3d* owned;
    CGxDeviceD3d& device;
    CGxDevice* saved;

    DeviceScope() : owned(new CGxDeviceD3d()), device(*owned), saved(g_theGxDevicePtr) {
        g_theGxDevicePtr = &this->device;

        C44Matrix projection;
        GxuXformCreateProjection_Exact(1.5707963705062866f, 4.0f / 3.0f, NEAR_Z, FAR_Z, projection);
        this->device.XformSetProjection(projection);
        this->device.XformSetView(C44Matrix());
    }

    ~DeviceScope() {
        g_theGxDevicePtr = this->saved;
        delete this->owned;
    }
};

} // namespace

TEST_CASE("The D3D device's native projection maps depth onto [0, 1]", "[world][liquid][depth]") {
    DeviceScope scope;

    C44Matrix application;
    scope.device.XformProjection(application);
    C44Matrix native;
    scope.device.XformProjNative(native);

    SECTION("the application projection is OpenGL-style: -1 at the near plane, 1 at the far") {
        CHECK(DepthAt(application, NEAR_Z) == Approx(-1.0f).margin(1e-4));
        CHECK(DepthAt(application, FAR_Z) == Approx(1.0f).margin(1e-4));
    }

    SECTION("the native one is D3D-style: 0 at the near plane, 1 at the far") {
        CHECK(DepthAt(native, NEAR_Z) == Approx(0.0f).margin(1e-4));
        CHECK(DepthAt(native, FAR_Z) == Approx(1.0f).margin(1e-4));
    }

    SECTION("both put the same thing at the same x and y") {
        CHECK(native.a0 == Approx(application.a0));
        CHECK(native.b1 == Approx(application.b1));
    }
}

TEST_CASE("Liquid::SetupTransforms hands the shader the native projection", "[world][liquid][depth]") {
    DeviceScope scope;

    C44Matrix placement;
    Liquid::SetupTransforms({ 0.0f, 0.0f, 0.0f }, placement);

    const C4Vector* vs = Liquid::VertexConstants();
    const C44Matrix& shaderProjection = *reinterpret_cast<const C44Matrix*>(&vs[0]);

    C44Matrix native;
    scope.device.XformProjNative(native);

    SECTION("c0..c3 are the native projection, unchanged on D3D") {
        CHECK(shaderProjection.c2 == native.c2);
        CHECK(shaderProjection.d2 == native.d2);
        CHECK(shaderProjection.c3 == native.c3);
        CHECK(shaderProjection.a0 == native.a0);
    }

    SECTION("water at any distance lands at the scene's depth for that distance") {
        for (float z : { 1.0f, 5.0f, 20.0f, 100.0f, 500.0f, 2000.0f }) {
            CHECK(DepthAt(shaderProjection, z) == Approx(DepthAt(native, z)).epsilon(1e-6));
        }
    }

    SECTION("a surface behind an object never gets a nearer depth than the object") {
        // The bug's signature: under the application projection, water at 20 yards came out
        // nearer than an object at 15 standing in front of it.
        float object = DepthAt(native, 15.0f);
        float water = DepthAt(shaderProjection, 20.0f);
        CHECK(water > object);
    }
}
