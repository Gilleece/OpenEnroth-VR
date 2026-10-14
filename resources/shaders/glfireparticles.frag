#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "softparticles.glsl"
#endif

in vec2 texuv;
in vec4 colour;
flat in float seed;

out vec4 FragColour;

uniform int additive; // Flames and sparks add their light, smoke covers what's behind it.

float CellHash(vec2 cell) {
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = p - cell;
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(CellHash(cell), CellHash(cell + vec2(1.0, 0.0)), u.x), mix(CellHash(cell + vec2(0.0, 1.0)), CellHash(cell + vec2(1.0, 1.0)), u.x), u.y);
}

void main() {
    // A soft puff whose edge is broken up by noise, so that it reads as a wisp and not as a disc.
    vec2 p = texuv * 2.0 - 1.0;
    float noise = ValueNoise(texuv * 4.0 + seed * 37.0) * 0.6 + ValueNoise(texuv * 9.0 + seed * 91.0) * 0.4;
    float shape = 1.0 - smoothstep(0.25, 1.0, length(p) + (noise - 0.5) * 0.6);
    float alpha = colour.a * shape;
#ifdef LINEAR_PIPELINE
    alpha *= SoftFade();
#endif
    if (alpha <= 0.0)
        discard;
    FragColour = additive != 0 ? vec4(colour.rgb * alpha, 0.0) : vec4(colour.rgb, alpha);
}
