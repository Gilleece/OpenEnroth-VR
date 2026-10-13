#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "colour.glsl"
#endif
#include "fog.glsl"
#ifdef LINEAR_PIPELINE
#include "heightfog.glsl"
#endif
#include "lighting.glsl"

in vec4 vertexColour;
in vec2 texuv;
flat in float olayer;
in vec3 vsPos;
in vec3 vsNorm;
flat in int vsAttrib;
in vec4 viewspace;

out vec4 FragColour;

uniform int waterframe;
uniform Sunlight sun;
uniform vec3 CameraPos;
uniform float gamma;

#define num_point_lights 20
uniform PointLight fspointlights[num_point_lights];

uniform sampler2DArray textureArray0;
uniform sampler2DArray textureArray1;
uniform FogParam fog;

#ifdef SUN_SHADOWS
#include "shadow.glsl"
#endif

#ifdef LINEAR_PIPELINE
#include "sky.glsl"
#include "water.glsl"
#endif

#ifdef MATERIAL_MAPS
uniform sampler2DArray materialArray;
#include "material.glsl"
#endif

#ifdef AMBIENT_OCCLUSION
#include "ao.glsl"
#endif

void main() {
#ifdef AMBIENT_OCCLUSION
    ambientOcclusion = AmbientOcclusion();
#endif
    vec3 fragnorm = normalize(vsNorm);
    vec3 fragviewdir = normalize(CameraPos - vsPos);

    // get water textures at point
    vec4 watercol = texture(textureArray0, vec3(texuv.x,texuv.y,waterframe));

    vec4 fragcol = vec4(0);

    // get normal texture at point
    fragcol = texture(textureArray1, vec3(texuv.x,texuv.y,olayer));

#ifdef MATERIAL_MAPS
    vec4 material = texture(materialArray, vec3(texuv.x, texuv.y, olayer));
    if (fragcol.a == 0.0 || (vsAttrib & 0x1) > 0)
        material = vec4(0.0); // Water has no material.
#endif

#ifdef LINEAR_PIPELINE
    // Water tiles are all water, shore tiles where they're transparent. Filtered high resolution shore tiles are partly
    // transparent along the shoreline.
    float waterAmount = (vsAttrib & 0x1) > 0 ? 1.0 : 1.0 - fragcol.a;
    vec3 waternormal = WaterNormal(vsPos); // Outside the branch, texture lookups need uniform control flow.
    fragcol = vec4(mix(fragcol.rgb, watercol.rgb, waterAmount), mix(fragcol.a, watercol.a, waterAmount));
#else
    // replace texture with water if alpha or a water tile (bit 0x1 in attribs)
    if (fragcol.a == 0.0 || (vsAttrib & 0x1) > 0){
        fragcol = watercol;
    }
#endif

#ifdef MATERIAL_MAPS
    // Tile u runs east and v runs south, along grid x and y.
    vec3 tangent = normalize(vec3(1.0, 0.0, 0.0) - fragnorm * fragnorm.x);
    vec3 bitangent = cross(fragnorm, tangent);

    vec3 lit;
    if (material.a > 0.0) {
        lit = CalcMaterialLighting(material, fragcol.rgb, fragnorm, tangent, bitangent, vsPos, fragviewdir);
    } else {
#endif

    // apply sun
#ifdef SUN_SHADOWS
    vec3 result = CalcSunLightShadowed(sun, fragnorm, fragviewdir, SunShadow(vsPos, fragnorm));
#else
    vec3 result = CalcSunLight(sun, fragnorm, fragviewdir, vec3(1));
#endif
    result = clamp(result, 0.0, 0.85);

    // stack torchlight if any
    result += CalcPointLight(fspointlights[0], fragnorm, vsPos, fragviewdir);

    // stack stationary lights
    for(int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 1.0)
            result += CalcPointLight(fspointlights[i], fragnorm, vsPos, fragviewdir);
    }

#ifdef LINEAR_PIPELINE
    result *= linearToSrgb(fragcol.rgb); // Lit in sRGB like the stock shaders, the sRGB curve doesn't commute with the product.
#else
    result *= fragcol.rgb;
#endif

    // stack mobile lights
    for(int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 2.0)
            result += CalcPointLight(fspointlights[i], fragnorm, vsPos, fragviewdir);
    }

#ifdef MATERIAL_MAPS
        lit = result;
    }
    vec3 result = lit;
#endif

#ifdef LINEAR_PIPELINE
    result = srgbToLinear(result);
    if (waterAmount > 0.0)
        result = mix(result, WaterSurface(result, waternormal, vsPos, fragviewdir), waterAmount);
#endif

    vec3 clamps = result;

    float dist = length(viewspace);
    float alpha = getFogAlpha(fog, dist);

#ifdef LINEAR_PIPELINE
    float fograt = getHeightFogRatio(fog, dist, vsPos);
    FragColour = mix(vec4(clamps, vertexColour.a), vec4(getHeightFogColour(fog, vsPos), alpha), fograt);
#else
    float fograt = getFogRatio(fog, dist);
    FragColour = mix(vec4(clamps, vertexColour.a), vec4(fog.color, alpha), fograt);
#endif
#ifndef LINEAR_PIPELINE
    FragColour.rgb = pow(FragColour.rgb, vec3(1.0/gamma));
#endif
}
