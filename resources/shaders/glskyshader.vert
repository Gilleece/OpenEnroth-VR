layout (location = 0) in vec3 vaPos;
layout (location = 1) in vec4 vaCol;
layout (location = 2) in vec2 vaTexSize;
layout (location = 3) in vec2 vaTimeOffset;

out vec3 worldPos;
out vec4 colour;
out vec2 texsize;
out vec2 timeoffset;

uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * vec4(vaPos, 1.0);
    worldPos = vaPos;
    colour = vaCol;
    texsize = vaTexSize;
    timeoffset = vaTimeOffset;
}
