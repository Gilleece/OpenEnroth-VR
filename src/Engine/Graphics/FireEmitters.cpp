#include "FireEmitters.h"

static constexpr FireEmitterProfile CAMPFIRE = {0.2f, 32.0f, 24.0f, {0.6f, 0.2f, 0.04f}, 24, 8, 12};
static constexpr FireEmitterProfile TORCH = {0.8f, 4.0f, 14.0f, {1.08f, 0.36f, 0.07f}, 14, 3, 4};
static constexpr FireEmitterProfile BEACON = {0.7f, 22.0f, 22.0f, {0.08f, 0.25f, 0.7f}, 24, 6, 10}; // The beacons burn blue.

const FireEmitterProfile *fireEmitterProfile(DecorationId id) {
    switch (id) {
    case DECORATION_CAMPFIRE_5:
        return &CAMPFIRE;
    case DECORATION_TORCH_31:
    case DECORATION_TORCH_32:
    case DECORATION_TORCH_33:
    case DECORATION_TORCH_130:
    case DECORATION_TORCH_131:
    case DECORATION_TORCH_132:
        return &TORCH;
    case DECORATION_BEACON_FIRE_206:
    case DECORATION_BEACON_FIRE_207:
    case DECORATION_BEACON_FIRE_208:
    case DECORATION_BEACON_FIRE_209:
        return &BEACON;
    default:
        return nullptr;
    }
}
