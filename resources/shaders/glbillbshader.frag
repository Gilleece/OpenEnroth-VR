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

void main() {
    vec4 fragcol = texture(texture0, texuv);
    int index = int(fragcol.r * 255.0);
    vec4 newcol = vec4(texelFetch(paltex2D, ivec2(index, paletteid), 0));

    if (paletteid > 0)
        if (index > 0)
            fragcol = vec4(newcol.r, newcol.g, newcol.b, 1.0);

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
