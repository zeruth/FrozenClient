#include "object/client/UnitMissileTrajectory_C.hpp"
#include <tempest/Math.hpp>

// ref: FUN_006fc360
float TrajectorySphereListHit(const C3Vector* origin, const C3Vector* delta, const CAaSphere* spheres) {
    float length = CMath::sqrt(delta->x * delta->x + delta->y * delta->y + delta->z * delta->z);

    // 1e-5 (0x009ea558)
    if (length < 0.00001f) {
        return -1.0f;
    }

    float invLength = 1.0f / length;

    for (const CAaSphere* sphere = spheres; ; sphere++) {
        if (sphere->r <= 0.0f) {
            return -1.0f;
        }

        float ox = sphere->c.x - origin->x;
        float oy = sphere->c.y - origin->y;
        float oz = sphere->c.z - origin->z;

        float along = oy * (delta->y * invLength) + ox * (delta->x * invLength) + oz * (invLength * delta->z);

        if (0.0f <= along
            && (oz * oz + ox * ox + oy * oy) - along * along <= sphere->r * sphere->r
            && along <= length
        ) {
            return along * invLength;
        }
    }
}
