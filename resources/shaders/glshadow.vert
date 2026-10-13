layout (location = 0) in vec3 vaPos;

uniform mat4 lightMatrix;

void main() {
    gl_Position = lightMatrix * vec4(vaPos, 1.0);
}
