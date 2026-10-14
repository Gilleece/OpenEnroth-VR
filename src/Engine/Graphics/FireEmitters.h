#pragma once

#include "Engine/Data/DecorationEnums.h"

/**
 * Particles a burning decoration emits on top of its own sprite, with `graphics.gpu_particles`.
 */
struct FireEmitterProfile {
    float height = 0; // Height of the emitter above the decoration's base, as a part of its sprite's height.
    float spread = 0; // Radius of the patch the flames rise from, in world units.
    float flameSize = 0; // Diameter of a new flame, in world units.
    float glow = 1; // Brightness of the flames.
    int flames = 0;
    int sparks = 0;
    int smoke = 0;
};

/**
 * @param id                            Decoration type.
 * @return                              Its particles, or `nullptr` for decorations that keep only their sprite.
 */
const FireEmitterProfile *fireEmitterProfile(DecorationId id);
