#include "precision.glsl"
#include "colour.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source;
uniform float exposure;
uniform float gamma;

const float KNEE = 0.6038; // Linear value of 0.8 sRGB encoded.

// Identity up to the knee, then a curve that keeps the slope of 1 at the knee and approaches white.
vec3 Shoulder(vec3 colour) {
    vec3 over = max(colour - KNEE, vec3(0.0));
    return min(colour, vec3(KNEE)) + (1.0 - KNEE) * (1.0 - exp(-over / (1.0 - KNEE)));
}

void main() {
    vec4 colour = texture(source, texuv);
    vec3 encoded = linearToSrgb(Shoulder(colour.rgb * exposure));
    FragColour = vec4(pow(encoded, vec3(1.0 / gamma)), colour.a);
}
