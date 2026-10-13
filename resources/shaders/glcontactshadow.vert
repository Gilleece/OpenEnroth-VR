layout (location = 0) in vec3 vaPos;
layout (location = 1) in vec3 vaShape; // Position in the ellipse, -1 to 1 on both axes, and the shadow's strength.

out vec3 shape;

uniform mat4 projection;
uniform mat4 view;

void main() {
    gl_Position = projection * view * vec4(vaPos, 1.0);
    shape = vaShape;
}
