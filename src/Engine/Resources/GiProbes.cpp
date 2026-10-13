#include "GiProbes.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include "Utility/Exception.h"

static constexpr char GI_PROBES_MAGIC[4] = {'O', 'E', 'G', 'I'};
static constexpr uint32_t GI_PROBES_VERSION = 1;
static constexpr uint32_t GI_PROBES_MAX_DIM = 1024; // Far more than any map needs, keeps a bad header from asking for gigabytes.
static constexpr uint32_t GI_PROBES_MAX_TIMES = 24;

/**
 * Reads little-endian values off the front of a blob, throwing when it runs out.
 */
class GiProbesReader {
 public:
    explicit GiProbesReader(const Blob &blob) : _data(static_cast<const char *>(blob.data())), _size(blob.size()) {}

    template<class T>
    T read() {
        T result;
        take(&result, sizeof(T));
        return result;
    }

    void take(void *dst, size_t size) {
        if (_size - _offset < size)
            throw Exception("Probe file is truncated at byte {}.", _offset);
        std::memcpy(dst, _data + _offset, size);
        _offset += size;
    }

    [[nodiscard]] bool atEnd() const {
        return _offset == _size;
    }

 private:
    const char *_data = nullptr;
    size_t _size = 0;
    size_t _offset = 0;
};

GiProbes readGiProbes(const Blob &blob) {
    GiProbesReader reader(blob);
    char magic[4];
    reader.take(magic, sizeof(magic));
    if (std::memcmp(magic, GI_PROBES_MAGIC, sizeof(magic)) != 0)
        throw Exception("Not a probe file.");
    uint32_t version = reader.read<uint32_t>();
    if (version != GI_PROBES_VERSION)
        throw Exception("Probe file has version {}, only version {} is supported.", version, GI_PROBES_VERSION);

    GiProbes result;
    uint32_t layout = reader.read<uint32_t>();
    if (layout != std::to_underlying(GI_PROBES_GRID) && layout != std::to_underlying(GI_PROBES_TERRAIN_LAYERS))
        throw Exception("Probe file has unknown layout {}.", layout);
    result.layout = static_cast<GiProbeLayout>(layout);
    for (float *value : {&result.origin.x, &result.origin.y, &result.origin.z, &result.spacing.x, &result.spacing.y, &result.spacing.z})
        *value = reader.read<float>();
    for (int *value : {&result.dims.x, &result.dims.y, &result.dims.z}) {
        uint32_t dim = reader.read<uint32_t>();
        if (dim == 0 || dim > GI_PROBES_MAX_DIM)
            throw Exception("Probe file has a grid dimension of {}.", dim);
        *value = static_cast<int>(dim);
    }

    uint32_t timeCount = reader.read<uint32_t>();
    if (timeCount == 0 || timeCount > GI_PROBES_MAX_TIMES)
        throw Exception("Probe file has {} time keys.", timeCount);
    result.timeHours.resize(timeCount);
    reader.take(result.timeHours.data(), timeCount * sizeof(float));
    result.averageIrradiance.resize(timeCount);
    for (Vec3f &average : result.averageIrradiance)
        for (float *value : {&average.x, &average.y, &average.z})
            *value = reader.read<float>();
    if (result.layout == GI_PROBES_TERRAIN_LAYERS) {
        result.layerHeights.resize(result.dims.z);
        reader.take(result.layerHeights.data(), result.dims.z * sizeof(float));
    }

    result.coefficients.resize(static_cast<size_t>(timeCount) * result.probeCount() * GiProbes::COEFFICIENTS);
    reader.take(result.coefficients.data(), result.coefficients.size() * sizeof(uint16_t));
    if (!reader.atEnd())
        throw Exception("Probe file has data past its probes.");
    return result;
}

Blob writeGiProbes(const GiProbes &probes) {
    std::string result;
    auto put = [&](const void *data, size_t size) {
        result.append(static_cast<const char *>(data), size);
    };
    auto putValue = [&](auto value) {
        put(&value, sizeof(value));
    };

    put(GI_PROBES_MAGIC, sizeof(GI_PROBES_MAGIC));
    putValue(GI_PROBES_VERSION);
    putValue(std::to_underlying(probes.layout));
    for (float value : {probes.origin.x, probes.origin.y, probes.origin.z, probes.spacing.x, probes.spacing.y, probes.spacing.z})
        putValue(value);
    for (int value : {probes.dims.x, probes.dims.y, probes.dims.z})
        putValue(static_cast<uint32_t>(value));
    putValue(static_cast<uint32_t>(probes.timeHours.size()));
    put(probes.timeHours.data(), probes.timeHours.size() * sizeof(float));
    for (const Vec3f &average : probes.averageIrradiance)
        for (float value : {average.x, average.y, average.z})
            putValue(value);
    if (probes.layout == GI_PROBES_TERRAIN_LAYERS)
        put(probes.layerHeights.data(), probes.layerHeights.size() * sizeof(float));
    put(probes.coefficients.data(), probes.coefficients.size() * sizeof(uint16_t));
    return Blob::fromString(std::move(result));
}

uint16_t floatToHalf(float value) {
    uint16_t sign = static_cast<uint16_t>((std::bit_cast<uint32_t>(value) >> 16) & 0x8000);
    float magnitude = std::fabs(value);
    if (std::isnan(value))
        return sign | 0x7E00;
    if (magnitude >= 65520.0f)
        return sign | 0x7BFF; // Largest finite half.
    if (magnitude < 6.103515625e-05f) // Below the smallest normal half, subnormals are steps of 2^-24.
        return sign | static_cast<uint16_t>(std::nearbyint(magnitude * 16777216.0f));

    int exponent = 0;
    float mantissa = std::frexp(magnitude, &exponent); // In [0.5, 1).
    uint32_t fraction = static_cast<uint32_t>(std::nearbyint((mantissa * 2.0f - 1.0f) * 1024.0f));
    uint32_t halfExponent = static_cast<uint32_t>(exponent - 1 + 15);
    if (fraction == 1024) { // Rounded up into the next power of two.
        fraction = 0;
        halfExponent++;
    }
    return sign | static_cast<uint16_t>((halfExponent << 10) | fraction);
}

float halfToFloat(uint16_t value) {
    float sign = (value & 0x8000) ? -1.0f : 1.0f;
    int exponent = (value >> 10) & 0x1F;
    int fraction = value & 0x3FF;
    if (exponent == 0)
        return sign * std::ldexp(static_cast<float>(fraction), -24);
    if (exponent == 31)
        return fraction ? std::nanf("") : sign * INFINITY;
    return sign * std::ldexp(1.0f + fraction / 1024.0f, exponent - 15);
}
