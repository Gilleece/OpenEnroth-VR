#include "precision.glsl"

in float viewDepth;

out vec4 FragColour;

void main() {
    FragColour = vec4(viewDepth, 0.0, 0.0, 1.0);
}
