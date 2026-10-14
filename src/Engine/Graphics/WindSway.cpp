#include "WindSway.h"

#include <cmath>
#include <numbers>

static constexpr float WIND_GUST_LENGTH = 1500.0f; // World units between the crests of two gusts.

float decorationSwayRatio(DecorationId id) {
    if ((id >= DECORATION_TREE_56 && id <= DECORATION_TREE_97) || (id >= DECORATION_TREE_195 && id <= DECORATION_TREE_201) ||
        id == DECORATION_TREE_205 || (id >= DECORATION_TREE_224 && id <= DECORATION_TREE_227))
        return 0.035f;
    if (id >= DECORATION_BUSH_102 && id <= DECORATION_BUSH_109)
        return 0.05f; // The bushes from 133 on are cacti, which stay still.
    if (id >= DECORATION_FLOWER_141 && id <= DECORATION_PLANT_180)
        return 0.08f;
    return 0.0f;
}

float windSwayPhase(Vec3f pos) {
    float along = pos.x * WIND_DIRECTION.x + pos.y * WIND_DIRECTION.y;
    float jitter = std::sin(pos.x * 0.0173f + pos.y * 0.0291f) * 0.6f; // Neighbours don't move in lockstep.
    return -along / WIND_GUST_LENGTH * 2 * std::numbers::pi_v<float> + jitter;
}

float windSway(float seconds, float phase) {
    float omega = 2 * std::numbers::pi_v<float> / WIND_PERIOD;
    return 0.7f * std::sin(seconds * omega * 2 + phase) + 0.3f * std::sin(seconds * omega * 5 + 1.7f * phase);
}
