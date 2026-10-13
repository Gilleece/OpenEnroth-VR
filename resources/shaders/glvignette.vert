layout (location = 0) in vec2 vaPos;

out vec2 position;

void main() {
    position = vaPos;
    gl_Position = vec4(vaPos, 0.0, 1.0);
}
