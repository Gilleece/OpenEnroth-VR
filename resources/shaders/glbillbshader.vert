layout (location = 0) in vec3 vaPos;
layout (location = 1) in vec2 vaTexUV;
layout (location = 2) in vec4 vaCol;
layout (location = 3) in float vaScreenSpace;

layout (location = 5) in float palid; 

#ifdef SPRITE_CROSSFADE
layout (location = 8) in vec2 vaCrossfadeTexUV;
layout (location = 9) in float vaCrossfade;
layout (location = 10) in float vaCrossfadePalette;

out vec2 crossfadeTexuv;
out float crossfade;
flat out int crossfadePaletteid;
#endif

#ifdef SPRITE_DEPTH
layout (location = 11) in vec3 vaShapeRight;
layout (location = 12) in vec2 vaShapeSize;
layout (location = 13) in float vaShapeThickness;

flat out vec3 shapeRight;
flat out vec2 shapeSize;
flat out float shapeThickness;
#endif

out vec4 colour;
out vec2 texuv;
out float screenspace;
flat out int paletteid;
#ifdef LINEAR_PIPELINE
out vec3 worldPos; // Only a world position when the billboards are drawn in world space.
#endif
#ifdef SUN_SHADOWS
#include "lighting.glsl"
#include "shadow.glsl"

layout (location = 6) in vec3 vaShadowAnchor;
layout (location = 7) in float vaShadowed;

uniform vec3 sunShadowDirection; // Towards the sun.
uniform float sunShadowBillboardRatio; // Brightness of a sprite in shadow relative to one in the sun, before strength.

out float sunlight;
#endif

uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * vec4(vaPos, 1.0);
    // float opacity = smoothstep(1.0 , 0.9999, vaPos.z);
    colour = vec4(vaCol.r, vaCol.g, vaCol.b, 1.0);
    texuv = vaTexUV;
    screenspace = vaScreenSpace;
    paletteid = int(palid);
#ifdef SPRITE_DEPTH
    shapeRight = vaShapeRight;
    shapeSize = vaShapeSize;
    shapeThickness = vaShapeThickness;
#endif
#ifdef SPRITE_CROSSFADE
    crossfadeTexuv = vaCrossfadeTexUV;
    crossfade = vaCrossfade;
    crossfadePaletteid = int(vaCrossfadePalette);
#endif
#ifdef LINEAR_PIPELINE
    worldPos = vaPos;
#endif
#ifdef SUN_SHADOWS
    // A sprite's own shadow quad passes through its anchor, so the anchor is looked up a step towards the sun.
    sunlight = 1.0;
    if (vaShadowed > 0.0)
        sunlight = mix(sunShadowBillboardRatio * SunShadowAmbient(0.0), 1.0, SunShadow(vaShadowAnchor + sunShadowDirection * 64.0, sunShadowDirection));
#endif
}
