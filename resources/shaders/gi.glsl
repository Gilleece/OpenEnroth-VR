// Indirect light that a texture pack bakes for the map, as L1 spherical harmonics of irradiance in a grid of probes.

#define GI_MAX_LAYERS 8

uniform highp sampler3D giProbes; // Coefficients of red, then green, then blue, each block giDims.z texels deep.
uniform highp sampler2D giTerrainHeights; // Terrain height under each probe column outdoors in r, 1 in g where it's water.
uniform vec3 giOrigin; // World position of the first probe.
uniform vec3 giSpacing; // World units between probes, z is unused outdoors.
uniform ivec3 giDims; // Probes along x, y and z.
uniform int giTerrainLayers; // 1 when the z layers sit at giLayerHeights above the terrain, 0 for a regular grid.
uniform float giLayerHeights[GI_MAX_LAYERS];
uniform vec3 giAverage; // Mean irradiance where the party walks, at this time of day.

// Scale of the ambient light at `pos`, the baked irradiance on a face looking along `normal` against the map's mean.
// A zero normal gives the irradiance averaged over all directions.
vec3 GlobalIllumination(vec3 pos, vec3 normal) {
    vec2 cell = (pos.xy - giOrigin.xy) / giSpacing.xy;
    vec2 uv = (cell + 0.5) / vec2(giDims.xy);
    float layer = 0.0;
    float water = 0.0;
    if (giTerrainLayers != 0) {
        vec2 column = texture(giTerrainHeights, uv).rg;
        float height = pos.z - column.r;
        water = column.g;
        for (int i = 1; i < GI_MAX_LAYERS; i++)
            if (i < giDims.z && height > giLayerHeights[i - 1])
                layer = float(i - 1) + clamp((height - giLayerHeights[i - 1]) / (giLayerHeights[i] - giLayerHeights[i - 1]), 0.0, 1.0);
    } else {
        layer = (pos.z - giOrigin.z) / giSpacing.z;
    }

    // Kept half a texel inside its own block, so that filtering never reaches into the next channel.
    float depth = float(giDims.z);
    float w = clamp(layer + 0.5, 0.5, depth - 0.5) / (3.0 * depth);
    vec4 basis = vec4(0.282095, 0.488603 * normal);
    vec3 irradiance = vec3(dot(texture(giProbes, vec3(uv, w)), basis),
                           dot(texture(giProbes, vec3(uv, w + 1.0 / 3.0)), basis),
                           dot(texture(giProbes, vec3(uv, w + 2.0 / 3.0)), basis));

    vec3 ratio = clamp(max(irradiance, vec3(0.0)) / max(giAverage, vec3(0.0001)), 0.5, 2.0);
    ratio = mix(ratio, vec3(1.0), water); // Over water the probes see the dark blue sea, which the average leaves out.
    return mix(vec3(1.0), ratio, smoothstep(vec3(0.001), vec3(0.004), giAverage)); // A channel that is dark all over the map has nothing to compare against.
}
