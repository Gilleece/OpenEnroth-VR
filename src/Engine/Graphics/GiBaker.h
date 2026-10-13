#pragma once

#include "Engine/Resources/GiProbes.h"

/**
 * Bakes the indirect light of the loaded level into a grid of probes: the ambient light, the sun and the level's
 * stationary lights after one bounce off its faces, coloured by their textures. Indoors the grid covers the sectors at
 * one time key. Outdoors it is layers above the terrain at seven times of day. Doors are left out of the bake, as if
 * they were open. Each key's average irradiance is over the probes inside the level indoors, and outdoors over the
 * lowest layer where the terrain tile under the probe isn't water.
 *
 * @return                              The probes, ready for `writeGiProbes`.
 */
GiProbes bakeGiProbes();
