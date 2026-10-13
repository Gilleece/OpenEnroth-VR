#include "precision.glsl"

in vec3 shape;

out vec4 FragColour;

// Multiplied onto the world: 1 leaves it as it is, darker at the middle of the ellipse.
void main() {
    float darkening = shape.z * (1.0 - smoothstep(0.45, 1.0, length(shape.xy)));
    FragColour = vec4(vec3(1.0 - darkening), 1.0);
}
