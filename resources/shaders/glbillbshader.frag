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

// Where an image is sampled at a quad coordinate: the stock billboards trim 1% off each edge of a sprite, so the quads
// that cover more than the image map their 0 to 1 range onto the same inset.
vec2 SpriteInset(vec2 uv) {
    return mix(vec2(0.01), vec2(0.99), uv);
}

#ifdef SPRITE_CROSSFADE
in vec2 crossfadeTexuv;
in float crossfade;
flat in int crossfadePaletteid;

uniform sampler2D texture1; // The neighbouring side, blended in by `crossfade`.

// A side's colour, transparent outside its image, with its palette applied.
vec4 SpriteSide(sampler2D image, vec2 uv, int palette) {
    vec4 result = texture(image, SpriteInset(clamp(uv, vec2(0.0), vec2(1.0))));
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
uniform int shapeColourPass; // The colour of the sprites the depth prepass drew, blended by alpha.
uniform int alphaToCoverage; // In the depth prepass, the alpha gives the samples covered instead of a cutoff.
uniform mat4 view;
uniform mat4 projection;

const float SPRITE_FOOT_FADE = 0.1; // Part of the sprite's height, at its bottom, where the depth flattens to its plane.
#endif

#ifdef SPRITE_LIGHTING
#include "lighting.glsl"
#ifdef GLOBAL_ILLUMINATION
#include "gi.glsl"
#endif

#define num_point_lights 20
uniform Sunlight sun;
uniform PointLight fspointlights[num_point_lights];

const float SPRITE_RELIGHT_MIN = 0.5; // Range of the relighting against the stock look.
const float SPRITE_RELIGHT_MAX = 1.5;

// The level's light reaching a surface facing `normal`, without its colour.
vec3 SpriteLightSum(vec3 normal, vec3 pos, vec3 viewDir) {
#ifdef GLOBAL_ILLUMINATION
    vec3 result = sun.ambient * GlobalIllumination(pos, normal);
#else
    vec3 result = sun.ambient;
#endif
#ifdef SKY_AMBIENT
    result *= HemisphereLight(normal);
#endif
    if (dot(sun.direction, sun.direction) > 0.0) // Indoors there is no sun.
        result += sun.diffuse * max(dot(normal, normalize(sun.direction)), 0.0);
    for (int i = 0; i < num_point_lights; i++)
        if (fspointlights[i].type > 0.0)
            result += CalcPointLight(fspointlights[i], normal, pos, viewDir);
    return result;
}
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
    vec2 spriteuv = texuv + parallax * (texture(spriteShape, SpriteInset(texuv)).b - 0.5);
    spriteuv = texuv + parallax * (texture(spriteShape, SpriteInset(spriteuv)).b - 0.5);
    float spriteDepth = (texture(spriteShape, SpriteInset(spriteuv)).b - 0.5) * shapeThickness * clamp((1.0 - spriteuv.y) / SPRITE_FOOT_FADE, 0.0, 1.0);
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
    vec4 fragcol = texture(texture0, SpriteInset(spriteuv));
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
    if (depthPrepass != 0) {
        if (alphaToCoverage != 0) {
            // The edge sharpened to about a pixel, so that it's antialiased rather than blurred when the sprite is close.
            FragColour = vec4(0.0, 0.0, 0.0, clamp((fragcol.a - 0.5) / max(fwidth(fragcol.a), 0.0001) + 0.5, 0.0, 1.0));
            return;
        }
        if (fragcol.a < 0.5)
            discard;
        FragColour = vec4(0.0); // Only the depth is written, the colour below would go nowhere.
        return;
    }
#endif

    fragcol *= colour;
#ifdef SPRITE_LIGHTING
    // Fetched while the whole quad of fragments still runs, so that the filtering sees the same derivatives.
    vec2 shapeNormal = texture(spriteShape, SpriteInset(spriteuv)).rg * 2.0 - 1.0;
#endif
#ifdef SPRITE_DEPTH
    if (shapeColourPass != 0 && fragcol.a == 0.0)
        discard; // Blended by alpha, a clear texel leaves the pixel as it is, so its lighting is skipped.
#endif
#ifdef SPRITE_LIGHTING
    // The stock tint stays the light of a flat sprite, the shape's normals move that light around.
    vec3 relight = vec3(1.0);
    if (shapeThickness > 0.0) {
        vec3 surfaceNormal = normalize(shapeRight * sign(shapeSize.x) * shapeNormal.x + vec3(0.0, 0.0, 1.0) * shapeNormal.y +
                                       spriteNormal * sqrt(max(1.0 - dot(shapeNormal, shapeNormal), 0.0)));
        relight = SpriteLightSum(surfaceNormal, worldPos, toEye) / max(SpriteLightSum(spriteNormal, worldPos, toEye), vec3(0.05));
        relight = clamp(relight, vec3(SPRITE_RELIGHT_MIN), vec3(SPRITE_RELIGHT_MAX));
    }
#endif
#ifdef SUN_SHADOWS
    fragcol.rgb *= sunlight;
#endif
#ifdef LINEAR_PIPELINE
    fragcol.rgb = srgbToLinear(fragcol.rgb);
#endif
#ifdef SPRITE_LIGHTING
    fragcol.rgb *= relight; // On linear light, so that the clamp bounds the brightness the eye sees.
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
