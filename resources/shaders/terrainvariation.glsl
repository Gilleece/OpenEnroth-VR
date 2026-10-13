// Breaks up the repetition of terrain tiles for the linear pipeline. Plain tiles are mirrored per grid column and row,
// and the ground's brightness drifts slowly across the map.

const float TERRAIN_BRIGHTNESS_PERIOD = 4096.0; // World units between the lattice points of the brightness variation.
const float TERRAIN_BRIGHTNESS_RANGE = 0.16; // Brightest over darkest, minus one.

// 1 or -1 per texture axis, -1 where the square's texture is mirrored. The mirror is in bits 0x4 and 0x8 of the
// attributes, set only on plain tiles.
vec2 TerrainMirror(int attrib) {
    return vec2((attrib & 0x4) != 0 ? -1.0 : 1.0, (attrib & 0x8) != 0 ? -1.0 : 1.0);
}

vec2 TerrainMirroredUv(vec2 uv, vec2 mirror) {
    return mix(uv, vec2(1.0) - uv, lessThan(mirror, vec2(0.0)));
}

float TerrainLatticeValue(vec2 point) {
    return fract(sin(dot(point, vec2(127.1, 311.7))) * 43758.5453);
}

// Scale of the ground's albedo at a world position, smooth value noise around 1.
float TerrainBrightness(vec2 pos) {
    vec2 p = pos / TERRAIN_BRIGHTNESS_PERIOD;
    vec2 cell = floor(p);
    vec2 f = p - cell;
    f = f * f * (3.0 - 2.0 * f);
    float value = mix(mix(TerrainLatticeValue(cell), TerrainLatticeValue(cell + vec2(1.0, 0.0)), f.x),
                      mix(TerrainLatticeValue(cell + vec2(0.0, 1.0)), TerrainLatticeValue(cell + vec2(1.0, 1.0)), f.x), f.y);
    return 1.0 + (value - 0.5) * TERRAIN_BRIGHTNESS_RANGE;
}
