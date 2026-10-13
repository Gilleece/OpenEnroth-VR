layout (location = 0) in vec3 vaPos;
#ifdef SHADOW_SPRITE
layout (location = 1) in vec2 vaTexUV;
layout (location = 2) in float vaPalette;

out vec2 texuv;
flat out int palette;
#endif

uniform mat4 lightMatrix;

void main() {
    gl_Position = lightMatrix * vec4(vaPos, 1.0);
#ifdef SHADOW_SPRITE
    texuv = vaTexUV;
    palette = int(vaPalette);
#endif
}
