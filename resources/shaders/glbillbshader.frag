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

void main() {
#ifdef SPRITE_CROSSFADE
    vec4 sideA = SpriteSide(texture0, texuv, paletteid);
    vec4 sideB = SpriteSide(texture1, crossfadeTexuv, crossfadePaletteid);
    // Colour from the sides that cover the pixel, by weight. Coverage grows to both silhouettes at the boundary, so
    // where only one side covers a pixel it stays solid rather than turning see-through.
    float weightA = sideA.a * (1.0 - crossfade);
    float weightB = sideB.a * crossfade;
    vec3 sidesColour = weightA + weightB > 0.0 ? (sideA.rgb * weightA + sideB.rgb * weightB) / (weightA + weightB) : vec3(0.0);
    float coverage = mix(sideA.a, max(sideA.a, sideB.a), crossfade * 2.0);
    vec4 fragcol = vec4(sidesColour, coverage);
#else
    vec4 fragcol = texture(texture0, texuv);
    int index = int(fragcol.r * 255.0);
    vec4 newcol = vec4(texelFetch(paltex2D, ivec2(index, paletteid), 0));

    if (paletteid > 0)
        if (index > 0)
            fragcol = vec4(newcol.r, newcol.g, newcol.b, 1.0);
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
