#include "precision.glsl"
#include "colour.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;
uniform float exposure;
uniform float gamma;
uniform sampler2D bloom;
uniform float bloomIntensity; // 0 when there is no bloom texture.

const float KNEE = 0.6038; // Linear value of 0.8 sRGB encoded.

// Identity up to the knee, then a curve that keeps the slope of 1 at the knee and approaches white.
vec3 Shoulder(vec3 colour) {
    vec3 over = max(colour - KNEE, vec3(0.0));
    return min(colour, vec3(KNEE)) + (1.0 - KNEE) * (1.0 - exp(-over / (1.0 - KNEE)));
}

#ifdef FILMIC_TONEMAP
const float FILMIC_SCALE = 0.85; // Exposure into the curve.

// Krzysztof Narkowicz's fit of the ACES filmic curve, CC0 or MIT licensed:
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 Filmic(vec3 colour) {
    colour *= FILMIC_SCALE;
    return clamp((colour * (2.51 * colour + 0.03)) / (colour * (2.43 * colour + 0.59) + 0.14), 0.0, 1.0);
}
#endif

#ifdef EYE_ADAPTATION
uniform sampler2D adaptation; // 1x1, the exposure scale the eye has adapted to.
#endif

void main() {
    vec4 colour = texture(source, texuv);
    if (bloomIntensity > 0.0)
        colour.rgb += texture(bloom, texuv).rgb * bloomIntensity;
    float scale = exposure;
#ifdef EYE_ADAPTATION
    scale *= texture(adaptation, vec2(0.5)).r;
#endif
#ifdef FILMIC_TONEMAP
    vec3 encoded = linearToSrgb(Filmic(colour.rgb * scale));
#else
    vec3 encoded = linearToSrgb(Shoulder(colour.rgb * scale));
#endif
    FragColour = vec4(pow(encoded, vec3(1.0 / gamma)), colour.a);
}
