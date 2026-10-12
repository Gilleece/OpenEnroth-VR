#include "precision.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;
uniform vec2 texelSize; // Size of a source texel in texture coordinates.

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

#ifdef BLOOM_UPSAMPLE

// 3x3 tent filter over the smaller level, blended onto the larger one.
void main() {
    vec3 colour = texture(source, texuv).rgb * 4.0;
    colour += (texture(source, texuv + vec2(-texelSize.x, 0.0)).rgb + texture(source, texuv + vec2(texelSize.x, 0.0)).rgb +
               texture(source, texuv + vec2(0.0, -texelSize.y)).rgb + texture(source, texuv + vec2(0.0, texelSize.y)).rgb) * 2.0;
    colour += texture(source, texuv - texelSize).rgb + texture(source, texuv + texelSize).rgb +
              texture(source, texuv + vec2(-texelSize.x, texelSize.y)).rgb + texture(source, texuv + vec2(texelSize.x, -texelSize.y)).rgb;
    FragColour = vec4(colour / 16.0, 1.0);
}

#else

uniform int brightPass;
uniform float threshold;

// Keeps the part of the colour above the threshold, with its hue.
vec3 BrightPass(vec3 colour) {
    float luma = dot(colour, LUMA);
    return colour * (max(luma - threshold, 0.0) / max(luma, 0.0001));
}

vec3 Tap(vec2 offset) {
    vec3 colour = texture(source, texuv + offset * texelSize).rgb;
    return brightPass != 0 ? BrightPass(colour) : colour;
}

// 13 bilinear taps as five overlapping 2x2 blocks, which downsamples without the aliasing of a plain 2x2 box.
void main() {
    vec3 a = Tap(vec2(-2.0, -2.0));
    vec3 b = Tap(vec2(0.0, -2.0));
    vec3 c = Tap(vec2(2.0, -2.0));
    vec3 d = Tap(vec2(-1.0, -1.0));
    vec3 e = Tap(vec2(1.0, -1.0));
    vec3 f = Tap(vec2(-2.0, 0.0));
    vec3 g = Tap(vec2(0.0, 0.0));
    vec3 h = Tap(vec2(2.0, 0.0));
    vec3 i = Tap(vec2(-1.0, 1.0));
    vec3 j = Tap(vec2(1.0, 1.0));
    vec3 k = Tap(vec2(-2.0, 2.0));
    vec3 l = Tap(vec2(0.0, 2.0));
    vec3 m = Tap(vec2(2.0, 2.0));

    vec3 blocks[5] = vec3[5]((d + e + i + j) * 0.25, (a + b + f + g) * 0.25, (b + c + g + h) * 0.25,
                             (f + g + k + l) * 0.25, (g + h + l + m) * 0.25);
    float weights[5] = float[5](0.5, 0.125, 0.125, 0.125, 0.125);

    vec3 colour = vec3(0.0);
    float total = 0.0;
    for (int n = 0; n < 5; n++) {
        // On the first level each block is also weighted down by its brightness, so that a single very bright
        // texel can't make the whole bloom flicker as it moves across texels.
        float weight = weights[n] * (brightPass != 0 ? 1.0 / (1.0 + dot(blocks[n], LUMA)) : 1.0);
        colour += blocks[n] * weight;
        total += weight;
    }
    FragColour = vec4(colour / total, 1.0);
}

#endif
