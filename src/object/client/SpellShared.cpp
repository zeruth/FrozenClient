#include "object/client/SpellShared.hpp"
#include <cmath>

// Spell effect ids: 6 apply aura; 35, 65, 119, 128, 129 and 143 the party, raid, pet, friend,
// enemy and owner area auras.
// ref: FUN_0076be10
bool SpellEffectAppliesUnitAura(uint32_t effect) {
    switch (effect) {
        case 6:
        case 35:
        case 65:
        case 119:
        case 128:
        case 129:
        case 143:
            return true;

        default:
            return false;
    }
}

// The area auras above, with 27 (persistent area aura) in place of the plain aura.
// ref: FUN_0076bed0
bool SpellEffectIsAreaAura(uint32_t effect) {
    switch (effect) {
        case 27:
        case 35:
        case 65:
        case 119:
        case 128:
        case 129:
        case 143:
            return true;

        default:
            return false;
    }
}

// Launch speed and flight time for a projectile leaving at `pitch` to cover `delta` under
// `gravity`. Returns 1 when the setup is degenerate, 0 when there is no solution, 2 on success.
// ref: FUN_0076c070
int32_t SpellTrajectorySolve(float pitch, const C3Vector* delta, float gravity, float* speed, float* time, C3Vector* velocity) {
    float cosine = std::cos(pitch);
    float sine = std::sin(pitch);
    float horizontal = std::sqrt(delta->x * delta->x + delta->y * delta->y);

    if (std::fabs(cosine) < 0.0001f || std::fabs(horizontal) < 0.0001f || std::fabs(gravity) < 0.0001f) {
        return 1;
    }

    float t = ((1.0f / cosine) * horizontal * sine - delta->z) / gravity;
    t = t + t;

    if (t <= 0.01f) {
        return 0;
    }

    t = std::sqrt(t);
    *time = t;

    *speed = (horizontal / t) * (1.0f / cosine);

    if (velocity) {
        velocity->x = *speed * delta->x * (1.0f / horizontal) * cosine;
        velocity->y = *speed * (1.0f / horizontal) * cosine * delta->y;
        velocity->z = sine * *speed;
    }

    return 2;
}

// Flight time for a projectile launched at `pitch` and `speed` to cover `delta` under `gravity`,
// or -1 when it cannot arrive.
// ref: FUN_0076bf80
float SpellTrajectoryTime(float pitch, float speed, const C3Vector* delta, float gravity) {
    float horizontalSpeed = std::cos(pitch) * speed;

    if (0.0001f < horizontalSpeed) {
        return std::sqrt(delta->x * delta->x + delta->y * delta->y) / horizontalSpeed;
    }

    if (speed <= 0.0001f) {
        if (std::fabs(gravity) <= 0.0001f) {
            return -1.0f;
        }
    } else if (std::fabs(gravity) <= 0.0001f) {
        return std::fabs(delta->z) / speed;
    }

    float verticalSpeed = std::sin(pitch) * speed;
    float discriminant = verticalSpeed * verticalSpeed - (delta->z * gravity + delta->z * gravity);

    if (discriminant >= 0.0f) {
        float root = std::sqrt(discriminant);
        float t1 = (root - verticalSpeed) * (-1.0f / gravity);
        float t2 = (-verticalSpeed - root) * (-1.0f / gravity);

        if (t1 >= 0.0f && (t2 < 0.0f || t1 < t2)) {
            return t1;
        }

        return t2;
    }

    return -1.0f;
}
