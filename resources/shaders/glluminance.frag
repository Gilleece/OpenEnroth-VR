#include "precision.glsl"
#include "colour.glsl"
#include "tonemap.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;
uniform sampler2D adaptation; // 1x1, the exposure scale the eye has adapted to.

#ifdef EYE_ADAPT
uniform float sourceLevel; // Mip level at which the source is averaged down to one texel.
uniform float rate; // Part of the way to the new exposure scale that this frame covers.

const float ADAPT_MIN = 0.25;
const float ADAPT_MAX = 8.0;

// Moves the scale towards the one at which the filmic curve gives the view the average brightness the shoulder curve
// gives it. Display brightness goes roughly with linear brightness to the power of 1 / 2.2, which sets the step.
void main() {
    vec2 average = textureLod(source, vec2(0.5), sourceLevel).rg;
    float scale = texture(adaptation, vec2(0.5)).r;
    float target = average.g > 0.001 ? clamp(scale * pow(average.r / average.g, 2.2), ADAPT_MIN, ADAPT_MAX) : scale;
    FragColour = vec4(mix(scale, target, rate));
}
#else
uniform float exposure;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// Display brightness of the world with the shoulder curve, and with the filmic curve at the adapted scale.
void main() {
    vec3 colour = texture(source, texuv).rgb * exposure;
    float scale = texture(adaptation, vec2(0.5)).r;
    FragColour = vec4(dot(linearToSrgb(Shoulder(colour)), LUMA), dot(linearToSrgb(Filmic(colour * scale)), LUMA), 0.0, 1.0);
}
#endif
