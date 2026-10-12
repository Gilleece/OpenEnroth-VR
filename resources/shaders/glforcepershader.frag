#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "colour.glsl"
#endif
#include "fog.glsl"

in vec4 colour;
in vec4 texuv;
in float screenspace;

out vec4 FragColour;

uniform sampler2D texture0;
uniform FogParam fog;

void main() {
    vec4 fragcol = textureProj(texture0, texuv) * colour;
#ifdef LINEAR_PIPELINE
    fragcol.rgb = srgbToLinear(fragcol.rgb);
#endif

    float fograt = getFogRatio(fog, screenspace);
    if (fragcol.a < 0.004) fograt = 0.0;

    float alpha = 1.0; // Not getFogAlpha(fog, screenspace) b/c we need foggy sky.

    FragColour = mix(fragcol, vec4(fog.color, alpha), fograt);
}
