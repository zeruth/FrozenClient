#include "model/CM2ParticleEmitter.hpp"
#include "catch.hpp"
#include <cmath>

// CParticleEmitter2's setters and integrator (0x00978c40..0x00981cd0). An emitter is built bare,
// as the base class; nothing here needs a model, a scene or a device.

namespace {

CM2ParticleEmitter::Particle Still() {
    CM2ParticleEmitter::Particle p;
    p.m_age = 1.0f;
    p.m_position = { 0.0f, 0.0f, 0.0f };
    p.m_velocity = { 0.0f, 0.0f, 0.0f };
    return p;
}

} // namespace

TEST_CASE("CM2ParticleEmitter::SetTextureGrid and CellUvBase", "[model][particles]") {
    CM2ParticleEmitter emitter;

    SECTION("a power-of-two grid sets the cell size and the column shift") {
        emitter.SetTextureGrid(4, 8);
        CHECK(emitter.m_textureRows == 4);
        CHECK(emitter.m_textureCols == 8);
        CHECK(emitter.m_cellShift == 3);
        CHECK(emitter.m_cellWidth == Approx(0.125f));
        CHECK(emitter.m_cellHeight == Approx(0.25f));
    }

    SECTION("cells run along a row, then wrap to the next") {
        emitter.SetTextureGrid(4, 8);
        float u;
        float v;

        emitter.CellUvBase(0, u, v);
        CHECK(u == 0.0f);
        CHECK(v == 0.0f);

        emitter.CellUvBase(5, u, v);
        CHECK(u == Approx(5.0f / 8.0f));
        CHECK(v == 0.0f);

        emitter.CellUvBase(8 + 3, u, v);
        CHECK(u == Approx(3.0f / 8.0f));
        CHECK(v == Approx(0.25f));

        emitter.CellUvBase(31, u, v);
        CHECK(u == Approx(7.0f / 8.0f));
        CHECK(v == Approx(0.75f));
    }

    SECTION("a grid that is not a pair of powers of two is refused") {
        emitter.SetTextureGrid(2, 2);
        emitter.SetTextureGrid(3, 4);
        CHECK(emitter.m_textureRows == 2);
        CHECK(emitter.m_textureCols == 2);

        emitter.SetTextureGrid(0, 4);
        CHECK(emitter.m_textureCols == 2);
    }

    SECTION("animation needs more than one cell") {
        emitter.SetTextureGrid(1, 1);
        emitter.SetTextureAnimated(1);
        CHECK_FALSE(emitter.m_flags & 0x100000);

        emitter.SetTextureGrid(1, 4);
        emitter.SetTextureAnimated(1);
        CHECK(emitter.m_flags & 0x100000);

        emitter.SetTextureAnimated(0);
        CHECK_FALSE(emitter.m_flags & 0x100000);
    }
}

TEST_CASE("CM2ParticleEmitter::SetColors and GetColors", "[model][particles]") {
    CM2ParticleEmitter emitter;

    // ParticleColor.dbc's colours arrive b, g, r.
    const uint8_t start[3] = { 10, 20, 30 };
    const uint8_t mid[3] = { 40, 50, 60 };
    const uint8_t end[3] = { 70, 80, 90 };
    emitter.SetColors(start, mid, end);

    SECTION("stored r, g, b as floats, and the override flag raised") {
        CHECK(emitter.m_colorOverride[0].x == 30.0f);
        CHECK(emitter.m_colorOverride[0].y == 20.0f);
        CHECK(emitter.m_colorOverride[0].z == 10.0f);
        CHECK(emitter.m_colorOverride[2].x == 90.0f);
        CHECK(emitter.m_flags & 0x10);
    }

    SECTION("read back unchanged") {
        uint8_t a[3];
        uint8_t b[3];
        uint8_t c[3];
        emitter.GetColors(a, b, c);

        for (int32_t i = 0; i < 3; i++) {
            CHECK(a[i] == start[i]);
            CHECK(b[i] == mid[i]);
            CHECK(c[i] == end[i]);
        }
    }
}

TEST_CASE("CM2ParticleEmitter::SetHeadTail", "[model][particles]") {
    CM2ParticleEmitter emitter;

    emitter.SetHeadTail(1, 0, 0.5f, 0);
    CHECK(emitter.m_verticesPerParticle == 4);
    CHECK(emitter.m_indicesPerParticle == 6);
    CHECK(emitter.m_tailLength == 0.5f);

    emitter.SetHeadTail(1, 1, 1.0f, 0);
    CHECK(emitter.m_verticesPerParticle == 8);
    CHECK(emitter.m_indicesPerParticle == 12);

    emitter.SetHeadTail(0, 1, 1.0f, 1);
    CHECK(emitter.m_verticesPerParticle == 4);
    CHECK(emitter.m_flags & 0x20000);
    CHECK_FALSE(emitter.m_flags & 0x4);
}

TEST_CASE("CM2ParticleEmitter small setters", "[model][particles]") {
    CM2ParticleEmitter emitter;

    SECTION("an emission rate must be positive to take") {
        emitter.SetEmissionRate(12.0f);
        CHECK(emitter.m_rate == 12.0f);
        emitter.SetEmissionRate(0.0f);
        emitter.SetEmissionRate(-3.0f);
        CHECK(emitter.m_rate == 12.0f);
    }

    SECTION("a z source under a thousandth is a hard zero") {
        emitter.SetZSource(0.5f);
        CHECK(emitter.m_zSource == 0.5f);
        emitter.SetZSource(0.0009f);
        CHECK(emitter.m_zSource == 0.0f);
        emitter.SetZSource(-0.0005f);
        CHECK(emitter.m_zSource == 0.0f);
    }
}

TEST_CASE("CM2ParticleEmitter::IntegrateParticle", "[model][particles]") {
    CM2ParticleEmitter emitter;
    emitter.m_wind = { 0.0f, 0.0f, 0.0f };
    emitter.m_windTime = 0.0f;

    SECTION("velocity moves the particle and gravity pulls it down exactly") {
        emitter.m_gravity = 10.0f;
        auto p = Still();
        p.m_velocity = { 1.0f, 2.0f, 5.0f };

        CHECK(emitter.IntegrateParticle(p, 0.5f));

        CHECK(p.m_position.x == Approx(0.5f));
        CHECK(p.m_position.y == Approx(1.0f));
        // z = v t - g t^2 / 2
        CHECK(p.m_position.z == Approx(5.0f * 0.5f - 10.0f * 0.25f * 0.5f));
        CHECK(p.m_velocity.z == Approx(5.0f - 10.0f * 0.5f));
    }

    SECTION("drag takes its fraction of the velocity, capped at all of it") {
        emitter.m_drag = 0.5f;
        auto p = Still();
        p.m_velocity = { 4.0f, 0.0f, 0.0f };

        emitter.IntegrateParticle(p, 0.5f);
        CHECK(p.m_velocity.x == Approx(3.0f));

        emitter.m_drag = 100.0f;
        emitter.IntegrateParticle(p, 0.5f);
        CHECK(p.m_velocity.x == 0.0f);
    }

    SECTION("wind blows only while the particle is younger than the wind time") {
        emitter.m_wind = { 2.0f, 0.0f, 0.0f };
        emitter.m_windTime = 1.0f;

        auto young = Still();
        young.m_age = 0.5f;
        emitter.IntegrateParticle(young, 0.25f);
        CHECK(young.m_velocity.x == Approx(0.5f));

        auto old = Still();
        old.m_age = 2.0f;
        emitter.IntegrateParticle(old, 0.25f);
        CHECK(old.m_velocity.x == 0.0f);
    }

    SECTION("under flag 0x1000 a particle dies once it stops approaching the emitter") {
        emitter.m_flags |= 0x1000 | 0x200;   // measured from the origin

        auto inbound = Still();
        inbound.m_position = { 10.0f, 0.0f, 0.0f };
        inbound.m_velocity = { -1.0f, 0.0f, 0.0f };
        CHECK(emitter.IntegrateParticle(inbound, 0.1f));

        auto outbound = Still();
        outbound.m_position = { 10.0f, 0.0f, 0.0f };
        outbound.m_velocity = { 1.0f, 0.0f, 0.0f };
        CHECK_FALSE(emitter.IntegrateParticle(outbound, 0.1f));
    }
}

TEST_CASE("CM2ParticleEmitterSpline span", "[model][particles]") {
    CM2ParticleEmitterSpline emitter;
    emitter.m_splineRate = 20.0f;

    SECTION("the start and end are curve parameters, clamped to [0, 1]") {
        emitter.SetWidth(-1.0f);
        CHECK(emitter.m_splineStart == 0.0f);
        emitter.SetWidth(0.25f);
        CHECK(emitter.m_splineStart == 0.25f);
        emitter.SetWidth(3.0f);
        CHECK(emitter.m_splineStart == 1.0f);
    }

    SECTION("a new end rescales the emission rate and asks for a particle at the end") {
        emitter.SetLength(0.5f);
        CHECK(emitter.m_splineEnd == 0.5f);
        CHECK(emitter.m_rate == Approx(10.0f));
        CHECK(emitter.m_emitAtEnd == 1);

        emitter.m_emitAtEnd = 0;
        emitter.SetLength(0.5000001f);
        CHECK(emitter.m_emitAtEnd == 0);
    }
}
