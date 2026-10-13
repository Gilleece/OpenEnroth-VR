#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "Testing/Unit/UnitTest.h"

#include "Engine/Resources/GiProbes.h"

#include "Utility/Exception.h"

static GiProbes makeOutdoorProbes() {
    GiProbes result;
    result.layout = GI_PROBES_TERRAIN_LAYERS;
    result.origin = Vec3f(-512, -512, 0);
    result.spacing = Vec3f(512, 512, 0);
    result.dims = Vec3i(3, 2, 2);
    result.timeHours = {9.0f, 18.0f};
    result.averageIrradiance = {Vec3f(0.25f, 0.2f, 0.15f), Vec3f(0.1f, 0.05f, 0.02f)};
    result.layerHeights = {64.0f, 256.0f};
    for (int i = 0; i < 2 * result.probeCount() * GiProbes::COEFFICIENTS; i++)
        result.coefficients.push_back(floatToHalf(i * 0.125f));
    return result;
}

UNIT_TEST(GiProbes, RoundTrip) {
    // A written probe file reads back as it was, with the layer heights of an outdoor map.
    GiProbes probes = makeOutdoorProbes();
    GiProbes result = readGiProbes(writeGiProbes(probes));
    EXPECT_EQ(result.layout, GI_PROBES_TERRAIN_LAYERS);
    EXPECT_EQ(result.origin, probes.origin);
    EXPECT_EQ(result.spacing, probes.spacing);
    EXPECT_EQ(result.dims, probes.dims);
    EXPECT_EQ(result.timeHours, probes.timeHours);
    EXPECT_EQ(result.averageIrradiance, probes.averageIrradiance);
    EXPECT_EQ(result.layerHeights, probes.layerHeights);
    EXPECT_EQ(result.coefficients, probes.coefficients);
}

UNIT_TEST(GiProbes, Malformed) {
    // Other files, newer versions, impossible sizes, truncated data and trailing data are all refused.
    std::string good(writeGiProbes(makeOutdoorProbes()).str());
    auto bad = [&](size_t offset, char value) {
        std::string result = good;
        result[offset] = value;
        return Blob::fromString(std::move(result));
    };
    EXPECT_THROW((void) readGiProbes(bad(0, 'X')), Exception); // Magic.
    EXPECT_THROW((void) readGiProbes(bad(4, 2)), Exception); // Version.
    EXPECT_THROW((void) readGiProbes(bad(8, 7)), Exception); // Layout.
    EXPECT_THROW((void) readGiProbes(bad(36, 0)), Exception); // Zero probes along x.
    EXPECT_THROW((void) readGiProbes(Blob::fromString(good.substr(0, good.size() - 1))), Exception);
    EXPECT_THROW((void) readGiProbes(Blob::fromString(good + "x")), Exception);

    // A header that claims more probes than the file holds is refused before the probes are allocated.
    std::string huge = good;
    uint32_t dim = 1024;
    for (size_t offset : {36, 40, 44})
        std::memcpy(huge.data() + offset, &dim, sizeof(dim));
    EXPECT_THROW((void) readGiProbes(Blob::fromString(std::move(huge))), Exception);
}

UNIT_TEST(GiProbes, Halves) {
    // Half float conversion is exact on representable values, rounds to nearest, and saturates.
    for (float value : {0.0f, 1.0f, -2.5f, 0.125f, 65504.0f, 6.103515625e-05f, 5.9604645e-08f})
        EXPECT_EQ(halfToFloat(floatToHalf(value)), value);
    EXPECT_EQ(floatToHalf(1.0f), 0x3C00);
    EXPECT_EQ(halfToFloat(floatToHalf(1.0f + 1.0f / 4096)), 1.0f);
    EXPECT_EQ(halfToFloat(floatToHalf(1e6f)), 65504.0f);
    EXPECT_TRUE(std::isnan(halfToFloat(floatToHalf(NAN))));
}
