#pragma once

#include <cstdint>
#include <vector>

#include "Library/Geometry/Vec.h"

#include "Utility/Memory/Blob.h"

/**
 * Layout of the probes in a `GiProbes` grid.
 */
enum class GiProbeLayout : uint32_t {
    GI_PROBES_GRID = 0, // A regular grid in world space, for indoor maps.
    GI_PROBES_TERRAIN_LAYERS = 1, // Layers at `layerHeights` above the terrain, for outdoor maps.
};
using enum GiProbeLayout;

/**
 * Baked indirect light of a map, a grid of probes that each hold L1 spherical harmonics of the irradiance arriving from
 * all directions, in linear colour. Stored in a texture pack as `gi/<map file>.bin`.
 */
struct GiProbes {
    static constexpr int COEFFICIENTS = 12; // c0, c1x, c1y, c1z for red, then green, then blue.

    GiProbeLayout layout = GI_PROBES_GRID;
    Vec3f origin; // World position of the first probe.
    Vec3f spacing; // World units between neighbouring probes.
    Vec3i dims; // Probes along x, y and z.
    std::vector<float> timeHours; // Time of day of each key, a single key at hour 0 indoors.
    std::vector<Vec3f> averageIrradiance; // Mean irradiance over the map for each key, linear RGB.
    std::vector<float> layerHeights; // Height above the terrain of each z layer, only for `GI_PROBES_TERRAIN_LAYERS`.
    std::vector<uint16_t> coefficients; // Half floats, for each key, each probe in z, y, x order, `COEFFICIENTS` each.

    [[nodiscard]] int probeCount() const {
        return dims.x * dims.y * dims.z;
    }
};

/**
 * @param blob                          Contents of a probe file.
 * @return                              The probes in it.
 * @throws Exception                    If the file isn't a probe file of a supported version, or its sizes don't add
 *                                      up.
 */
GiProbes readGiProbes(const Blob &blob);

/**
 * @param probes                        Probes to store, consistent in their sizes.
 * @return                              The contents of a probe file holding them.
 */
Blob writeGiProbes(const GiProbes &probes);

/**
 * @param value                         Value to convert.
 * @return                              `value` as an IEEE half float, rounded to nearest and saturated to the largest
 *                                      finite half.
 */
uint16_t floatToHalf(float value);

/**
 * @param value                         IEEE half float.
 * @return                              `value` as a float.
 */
float halfToFloat(uint16_t value);
