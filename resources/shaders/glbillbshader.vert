layout (location = 0) in vec3 vaPos;
layout (location = 1) in vec2 vaTexUV;
layout (location = 2) in vec4 vaCol;
layout (location = 3) in float vaScreenSpace;

layout (location = 5) in float palid; 

out vec4 colour;
out vec2 texuv;
out float screenspace;
flat out int paletteid;
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
#ifdef SUN_SHADOWS
    // A sprite's own shadow quad passes through its anchor, so the anchor is looked up a step towards the sun.
    sunlight = 1.0;
    if (vaShadowed > 0.0)
        sunlight = mix(sunShadowBillboardRatio * SunShadowAmbient(0.0), 1.0, SunShadow(vaShadowAnchor + sunShadowDirection * 64.0, sunShadowDirection));
#endif
}
