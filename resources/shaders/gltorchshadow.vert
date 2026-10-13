layout (location = 0) in vec3 vaPos;
layout (location = 1) in vec3 vaNormal;

out vec3 worldPos;
out vec3 normal;

uniform mat4 lightMatrix;

void main() {
    gl_Position = lightMatrix * vec4(vaPos, 1.0);
    worldPos = vaPos;
    normal = vaNormal;
}
