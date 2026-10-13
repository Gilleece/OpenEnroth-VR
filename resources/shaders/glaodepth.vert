layout (location = 0) in vec3 vaPos;

out float viewDepth;

uniform mat4 projection;
uniform mat4 view;

void main() {
    vec4 viewPos = view * vec4(vaPos, 1.0);
    viewDepth = -viewPos.z;
    gl_Position = projection * viewPos;
}
