layout (location = 0) in vec2 vaPos;

out vec2 texuv;

void main() {
    texuv = vaPos * 0.5 + 0.5;
    gl_Position = vec4(vaPos, 0.0, 1.0);
}
