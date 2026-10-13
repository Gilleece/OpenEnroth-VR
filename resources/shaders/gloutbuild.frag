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
#ifdef MATERIAL_MAPS
in vec3 vsCameraOffset;
#endif
in vec4 viewspace;

out vec4 FragColour;

uniform int waterframe;
uniform Sunlight sun;
uniform vec3 CameraPos;
uniform int flowtimer;
uniform int flowtimerms; // TODO(Nik-RE-dev): use a single timer for everything
uniform int watertiles;
uniform float gamma;

#define num_point_lights 20
uniform PointLight fspointlights[num_point_lights];

uniform sampler2DArray textureArray0;
uniform ivec2 logicaltexsize;
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
#ifdef LINEAR_PIPELINE
    bool waterTile = watertiles == 1 && olayer == 0.0 && (vsAttrib & 0x3C00) == 0;
    bool water = ((vsAttrib & 0x2) > 0 || waterTile) && fragnorm.z > 0.7; // Fountain basins and wells, not falling water.
    // Looked up outside the branch, texture lookups need uniform control flow.
    vec3 waternormal = WaterNormal(vsPos * 4.0); // Finer ripples than the outdoor waves, basins are small.
#endif

    vec4 fragcol = vec4(1.0);
    vec2 texcoords = vec2(1.0);
    vec2 texuvmod = vec2(0.0);
    vec2 deltas = vec2(0.0);
    ivec2 texsize = logicaltexsize;

    // texture flow mods
    if (abs(vsNorm.z) >= 0.9) {
        if ((vsAttrib & 0x400) > 0) texuvmod.y = 1.0;
        if ((vsAttrib & 0x800) > 0) texuvmod.y = -1.0;
    } else {
        if ((vsAttrib & 0x400) > 0) texuvmod.y = -1.0;
        if ((vsAttrib & 0x800) > 0) texuvmod.y = 1.0;
    }

    if ((vsAttrib & 0x1000) > 0) {
        texuvmod.x = -1.0;
    } else if ((vsAttrib & 0x2000) > 0) {
        texuvmod.x = 1.0;
    }

    // lava movement
    if ((vsAttrib & 0x4000) > 0) {
        // Texture makes full circle in 8 seconds
        float lavaperiod = mod(float(flowtimerms), 8000.0);
        float lavaradians = lavaperiod * radians(360.0) / 8000.0;
        float curpos = sin(lavaradians);
        deltas.x = 0.0;
        deltas.y = float(texsize.y) * curpos;
    } else {
        deltas.x = texuvmod.x * mod(float(flowtimer), float(texsize.x));
        deltas.y = texuvmod.y * mod(float(flowtimer), float(texsize.y));
    }

    texcoords.x = (deltas.x + texuv.x) / float(texsize.x);
    texcoords.y = (deltas.y + texuv.y) / float(texsize.y);
    fragcol = texture(textureArray0, vec3(texcoords.x,texcoords.y,olayer));

    vec4 toplayer = texture(textureArray0, vec3(texcoords.x,texcoords.y,0));
    vec4 watercol = texture(textureArray0, vec3(texcoords.x,texcoords.y,waterframe));

    if ((watertiles == 1) && (olayer == 0.0)){
        if ((vsAttrib & 0x3C00) != 0){ // water anim disabled
            fragcol = toplayer;
        } else {
            fragcol = watercol;
        }
    }

#ifdef MATERIAL_MAPS
    float materiallayer = olayer;
    if ((watertiles == 1) && (olayer == 0.0))
        materiallayer = ((vsAttrib & 0x3C00) != 0) ? 0.0 : float(waterframe);
    vec4 material = texture(materialArray, vec3(texcoords.x, texcoords.y, materiallayer));
    vec3 tangent;
    vec3 bitangent;
    bool hasframe = FaceTangentFrame(vsCameraOffset, texuv, fragnorm, tangent, bitangent);

    vec3 lit;
    if (material.a > 0.0 && hasframe) {
        lit = CalcMaterialLighting(material, fragcol.rgb, fragnorm, tangent, bitangent, vsPos, fragviewdir);
    } else {
#endif

    // sunlight
#ifdef SUN_SHADOWS
    vec3 result = CalcSunLightShadowed(sun, fragnorm, fragviewdir, SunShadow(vsPos, fragnorm));
#else
    vec3 result = CalcSunLight(sun, fragnorm, fragviewdir, vec3(1)); //fragcol.rgb);
#endif
    result = clamp(result, 0.0, 0.85);

    result += CalcPointLight(fspointlights[0], fragnorm, vsPos, fragviewdir);

    // stack stationary
    for(int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 1.0)
            result += CalcPointLight(fspointlights[i], fragnorm, vsPos, fragviewdir);
    }

#ifdef LINEAR_PIPELINE
    result *= linearToSrgb(fragcol.rgb); // Lit in sRGB like the stock shaders, the sRGB curve doesn't commute with the product.
#else
    result *= fragcol.rgb;
#endif

    // stack mobile

    for(int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 2.0)
            result += CalcPointLight(fspointlights[i], fragnorm, vsPos, fragviewdir);
    }

#ifdef MATERIAL_MAPS
        lit = result;
    }
    vec3 result = lit;
#endif

    vec3 clamps = result; // fragcol.rgb *  // clamp(result,0,1) * ;

    vec3 dull;

    // percpetion red fade
    if ((vsAttrib & 0x10000) > 0) {
        float ss = (sin(float(flowtimer) / 30.0) + 1.0) / 2.0;
        dull = vec3(1, ss, ss);
    } else {
        dull = vec3(1,1,1);
    }

    float dist = length(viewspace);
    float alpha = getFogAlpha(fog, dist);

    vec4 inter = vec4(clamps, vertexColour.a) * vec4(dull,1); // result, 1.0);
#ifdef LINEAR_PIPELINE
    inter.rgb = srgbToLinear(inter.rgb);
    if (water)
        inter.rgb = WaterSurface(inter.rgb, waternormal, vsPos, fragviewdir);
#endif
#ifdef LINEAR_PIPELINE
    float fograt = getHeightFogRatio(fog, dist, vsPos);
    FragColour = mix(inter, vec4(getHeightFogColour(fog, vsPos), alpha), fograt);
#else
    float fograt = getFogRatio(fog, dist);
    FragColour = mix(inter, vec4(fog.color, alpha), fograt);
#endif
#ifndef LINEAR_PIPELINE
    FragColour.rgb = pow(FragColour.rgb, vec3(1.0/gamma));
#endif
}
