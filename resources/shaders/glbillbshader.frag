#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "colour.glsl"
#endif
#include "fog.glsl"
#ifdef LINEAR_PIPELINE
#include "heightfog.glsl"
#include "softparticles.glsl"
#endif

in vec4 colour;
in vec2 texuv;
in float screenspace;
flat in int paletteid;
#ifdef LINEAR_PIPELINE
in vec3 worldPos;
#endif
#ifdef SUN_SHADOWS
in float sunlight;
#endif

out vec4 FragColour;

uniform sampler2D texture0;
uniform sampler2D paltex2D;
uniform FogParam fog;
uniform float gamma;
#ifdef LINEAR_PIPELINE
uniform float additivegain;
#endif

#ifdef SPRITE_CROSSFADE
in vec2 crossfadeTexuv;
in float crossfade;
flat in int crossfadePaletteid;

uniform sampler2D texture1; // The neighbouring side, blended in by `crossfade`.

// A side's colour, transparent outside its image, with its palette applied.
vec4 SpriteSide(sampler2D image, vec2 uv, int palette) {
    vec4 result = texture(image, clamp(uv, vec2(0.005), vec2(0.995)));
    int index = int(result.r * 255.0);
    vec4 paletted = vec4(texelFetch(paltex2D, ivec2(index, palette), 0));
    if (palette > 0 && index > 0)
        result = vec4(paletted.rgb, 1.0);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        result = vec4(0.0);
    return result;
}
#endif

#ifdef SPRITE_DEPTH
flat in vec3 shapeRight;
flat in vec2 shapeSize;
flat in float shapeThickness; // 0 for billboards without a shape.

uniform sampler2D spriteShape; // RG normal, B depth with 0.5 on the sprite's plane, A coverage.
uniform vec3 eyePosition;
uniform vec3 parallaxOrigin; // Where the sprite is seen without parallax, the head between the eyes.
uniform int depthPrepass; // Only the depth of the opaque parts is drawn.
uniform mat4 view;
uniform mat4 projection;

const float SPRITE_FOOT_FADE = 0.1; // Part of the sprite's height, at its bottom, where the depth flattens to its plane.
#endif

void main() {
#ifdef SPRITE_DEPTH
    // The sprite's surface as a height field over its plane, with the parallax of this eye against the head in two
    // steps, so the eyes see it differently but the head sees the stock image.
    vec3 spriteNormal = normalize(cross(vec3(0.0, 0.0, 1.0), shapeRight));
    if (dot(spriteNormal, eyePosition - worldPos) < 0.0)
        spriteNormal = -spriteNormal;
    vec3 toEye = normalize(eyePosition - worldPos);
    vec3 toOrigin = normalize(parallaxOrigin - worldPos);
    vec2 slope = vec2(dot(toEye, shapeRight), -toEye.z) / max(dot(toEye, spriteNormal), 0.0001) -
                 vec2(dot(toOrigin, shapeRight), -toOrigin.z) / max(dot(toOrigin, spriteNormal), 0.0001);
    slope = clamp(slope, vec2(-1.0), vec2(1.0));
    vec2 parallax = shapeThickness > 0.0 ? slope * shapeThickness / shapeSize : vec2(0.0);
    vec2 spriteuv = texuv + parallax * (texture(spriteShape, texuv).b - 0.5);
    spriteuv = texuv + parallax * (texture(spriteShape, spriteuv).b - 0.5);
    float spriteDepth = (texture(spriteShape, spriteuv).b - 0.5) * shapeThickness * clamp((1.0 - spriteuv.y) / SPRITE_FOOT_FADE, 0.0, 1.0);
    vec4 spriteClip = projection * view * vec4(worldPos + spriteNormal * spriteDepth, 1.0);
    gl_FragDepth = shapeThickness > 0.0 ? spriteClip.z / spriteClip.w * 0.5 + 0.5 : gl_FragCoord.z;
#endif
#ifdef SPRITE_CROSSFADE
#ifdef SPRITE_DEPTH
    vec4 sideA = SpriteSide(texture0, spriteuv, paletteid);
#else
    vec4 sideA = SpriteSide(texture0, texuv, paletteid);
#endif
    vec4 sideB = SpriteSide(texture1, crossfadeTexuv, crossfadePaletteid);
    // Colour from the sides that cover the pixel, by weight. Coverage grows to both silhouettes at the boundary, so
    // where only one side covers a pixel it stays solid rather than turning see-through.
    float weightA = sideA.a * (1.0 - crossfade);
    float weightB = sideB.a * crossfade;
    vec3 sidesColour = weightA + weightB > 0.0 ? (sideA.rgb * weightA + sideB.rgb * weightB) / (weightA + weightB) : vec3(0.0);
    float coverage = mix(sideA.a, max(sideA.a, sideB.a), crossfade * 2.0);
    vec4 fragcol = vec4(sidesColour, coverage);
#else
#ifdef SPRITE_DEPTH
    vec4 fragcol = texture(texture0, spriteuv);
    if (any(lessThan(spriteuv, vec2(0.0))) || any(greaterThan(spriteuv, vec2(1.0))))
        fragcol = vec4(0.0);
#else
    vec4 fragcol = texture(texture0, texuv);
#endif
    int index = int(fragcol.r * 255.0);
    vec4 newcol = vec4(texelFetch(paltex2D, ivec2(index, paletteid), 0));

    if (paletteid > 0)
        if (index > 0)
            fragcol = vec4(newcol.r, newcol.g, newcol.b, 1.0);
#endif

#ifdef SPRITE_DEPTH
    if (depthPrepass != 0 && fragcol.a < 0.5)
        discard;
#endif

    fragcol *= colour;
#ifdef SUN_SHADOWS
    fragcol.rgb *= sunlight;
#endif
#ifdef LINEAR_PIPELINE
    fragcol.rgb = srgbToLinear(fragcol.rgb);
#endif

#ifdef LINEAR_PIPELINE
    float fograt = getHeightFogRatio(fog, screenspace, worldPos);
#else
    float fograt = getFogRatio(fog, screenspace);
#endif
    if (fragcol.a < 0.004) fograt = 0.0;

    float alpha = getFogAlpha(fog, screenspace);

#ifdef LINEAR_PIPELINE
    FragColour = mix(fragcol, vec4(getHeightFogColour(fog, worldPos), alpha), fograt);
#else
    FragColour = mix(fragcol, vec4(fog.color, alpha), fograt);
#endif
#ifdef LINEAR_PIPELINE
    FragColour.rgb *= additivegain * SoftFade();
#else
    FragColour.rgb = pow(FragColour.rgb, vec3(1.0/gamma));
#endif
}
