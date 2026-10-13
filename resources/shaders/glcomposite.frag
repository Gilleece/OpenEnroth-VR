#include "precision.glsl"
#include "colour.glsl"
#include "tonemap.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;
uniform float exposure;
uniform float gamma;
uniform sampler2D bloom;
uniform float bloomIntensity; // 0 when there is no bloom texture.

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
