#include "precision.glsl"

in vec3 worldPos;
in vec3 normal;

uniform vec3 lightPosition;
uniform float lightRadius;

void main() {
    if (dot(normal, lightPosition - worldPos) < 0.0)
        discard; // Only faces turned towards the light cast, some lights sit just behind the faces of their own fitting.
    gl_FragDepth = length(worldPos - lightPosition) / lightRadius; // The receivers compare against this in any direction.
}
