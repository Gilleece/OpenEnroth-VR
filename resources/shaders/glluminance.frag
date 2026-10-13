#include "precision.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;

#ifdef EYE_ADAPT
uniform sampler2D previous; // 1x1, the exposure scale of the previous frame.
uniform float sourceLevel; // Mip level at which the source's log luminance is averaged down to one texel.
uniform float rate; // Part of the way to the new exposure scale that this frame covers.

const float ADAPT_KEY = 0.1; // Average linear luminance of a sunlit outdoor view, which keeps a scale of 1.
const float ADAPT_STRENGTH = 0.25; // How much of the difference to the key the eye makes up.
const float ADAPT_MIN = 0.8;
const float ADAPT_MAX = 1.6;

void main() {
    float average = exp(textureLod(source, vec2(0.5), sourceLevel).r);
    float target = clamp(pow(ADAPT_KEY / max(average, 0.0001), ADAPT_STRENGTH), ADAPT_MIN, ADAPT_MAX);
    FragColour = vec4(mix(texture(previous, vec2(0.5)).r, target, rate));
}
#else
// Log luminance of the world, averaged afterwards by the mip chain.
void main() {
    float luminance = dot(texture(source, texuv).rgb, vec3(0.2126, 0.7152, 0.0722));
    FragColour = vec4(log(max(luminance, 0.0001)));
}
#endif
