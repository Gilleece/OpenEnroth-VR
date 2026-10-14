#include "FireEmitters.h"

static constexpr FireEmitterProfile CAMPFIRE = {0.25f, 26.0f, 24.0f, 28, 8, 12};
static constexpr FireEmitterProfile TORCH = {1.0f, 3.0f, 9.0f, 10, 2, 3};
static constexpr FireEmitterProfile BEACON = {1.0f, 22.0f, 22.0f, 24, 6, 10};

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
