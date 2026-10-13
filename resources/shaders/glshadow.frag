#include "precision.glsl"

#ifdef SHADOW_SPRITE
in vec2 texuv;
flat in int palette;

uniform sampler2D texture0;

// Sprites cast through their opaque pixels: index 0 is transparent in palette sprites, alpha in true colour ones.
void main() {
    vec4 colour = texture(texture0, texuv);
    if (palette > 0 ? colour.r * 255.0 < 0.5 : colour.a < 0.5)
        discard;
}
#else
// Depth only, the shadow map has no colour attachment.
void main() {
}
#endif
