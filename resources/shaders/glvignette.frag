#include "precision.glsl"

in vec2 position;

out vec4 FragColour;

uniform float strength; // Darkness at the edge of the falloff, 0 to 1.
uniform vec2 center; // Where the eye looks straight ahead, in normalized device coordinates.

const float FALLOFF_START = 0.65; // Distance from the center where the darkening starts, the image edges are at 1.
const float FALLOFF_END = 1.0;

void main() {
    float darkness = strength * smoothstep(FALLOFF_START, FALLOFF_END, length(position - center));
    FragColour = vec4(0.0, 0.0, 0.0, darkness);
}
