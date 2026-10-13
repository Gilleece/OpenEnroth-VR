// Sun shadows from the cascaded shadow map. Include it after lighting.glsl.

#define SUN_SHADOW_CASCADES 4

uniform highp sampler2DArrayShadow sunShadowMap;
uniform mat4 sunShadowMatrices[SUN_SHADOW_CASCADES]; // World to shadow map texture space, with depth in [0, 1].
uniform float sunShadowTexels[SUN_SHADOW_CASCADES]; // World size of a shadow map texel in each cascade.
uniform float sunShadowFade; // 0 when the sun casts no shadows, 1 when they're at full strength.
uniform float sunShadowStrength; // Part of the ambient light that a shadow takes away too.
uniform int sunShadowFilter; // 0 for 3x3 taps, 1 for a single tap.

const float SUN_SHADOW_BLEND = 0.1; // Part of each cascade, at its border, that blends into the next one.

// Weights of the four depth comparisons of a gather, whose components are the texels at (0, 1), (1, 1), (1, 0) and
// (0, 0) of its 2x2 block, from the weights of the block's columns and rows.
vec4 SunShadowGatherWeights(vec2 weightsX, vec2 weightsY) {
    return vec4(weightsX.x * weightsY.y, weightsX.y * weightsY.y, weightsX.y * weightsY.x, weightsX.x * weightsY.x);
}

// Bilinear depth comparisons, 3x3 of them or one, from gathers, which keep the lookup valid in non-uniform control
// flow. The 3x3 taps cover a 4x4 block of texels, weighted as the taps would weigh them.
float SunShadowSample(int cascade, vec3 pos, vec3 normal) {
    vec3 offsetPos = pos + normal * sunShadowTexels[cascade] * 1.5;
    vec4 coord = sunShadowMatrices[cascade] * vec4(offsetPos, 1.0);
    vec2 size = vec2(textureSize(sunShadowMap, 0).xy);
    vec2 texel = vec2(1.0) / size;
    vec2 base = floor(coord.xy * size - 0.5); // Lower left of the 2x2 texels a bilinear tap reads.
    vec2 f = coord.xy * size - 0.5 - base;
    vec2 corner = (base + 1.0) * texel; // Gathering at the corner the four texels share picks them whatever the rounding.
    if (sunShadowFilter != 0)
        return dot(textureGather(sunShadowMap, vec3(corner, float(cascade)), coord.z), SunShadowGatherWeights(vec2(1.0 - f.x, f.x), vec2(1.0 - f.y, f.y)));

    vec2 lowX = vec2(1.0 - f.x, 1.0), highX = vec2(1.0, f.x);
    vec2 lowY = vec2(1.0 - f.y, 1.0), highY = vec2(1.0, f.y);
    float lit = dot(textureGather(sunShadowMap, vec3(corner + vec2(-1.0, -1.0) * texel, float(cascade)), coord.z), SunShadowGatherWeights(lowX, lowY)) +
                dot(textureGather(sunShadowMap, vec3(corner + vec2(1.0, -1.0) * texel, float(cascade)), coord.z), SunShadowGatherWeights(highX, lowY)) +
                dot(textureGather(sunShadowMap, vec3(corner + vec2(-1.0, 1.0) * texel, float(cascade)), coord.z), SunShadowGatherWeights(lowX, highY)) +
                dot(textureGather(sunShadowMap, vec3(corner + vec2(1.0, 1.0) * texel, float(cascade)), coord.z), SunShadowGatherWeights(highX, highY));
    return lit / 9.0;
}

// How far inside a cascade a point is, 0 at its border and 0.5 at its centre, negative outside.
float SunShadowInside(int cascade, vec3 pos) {
    vec4 coord = sunShadowMatrices[cascade] * vec4(pos, 1.0);
    vec2 edge = min(coord.xy, vec2(1.0) - coord.xy);
    return min(min(edge.x, edge.y), min(coord.z, 1.0 - coord.z));
}

// 1 where the sun reaches the point, 0 where it's fully shadowed.
float SunShadow(vec3 pos, vec3 normal) {
    if (sunShadowFade <= 0.0)
        return 1.0;

    for (int cascade = 0; cascade < SUN_SHADOW_CASCADES; cascade++) {
        float inside = SunShadowInside(cascade, pos);
        if (inside < 0.0)
            continue;

        float shadow = SunShadowSample(cascade, pos, normal);
        float blend = clamp(inside / (SUN_SHADOW_BLEND * 0.5), 0.0, 1.0);
        if (blend < 1.0) {
            float next = cascade + 1 < SUN_SHADOW_CASCADES ? SunShadowSample(cascade + 1, pos, normal) : 1.0;
            shadow = mix(next, shadow, blend);
        }
        return mix(1.0, shadow, sunShadowFade);
    }
    return 1.0;
}

// Scale of the ambient light, 1 in full sun and 1 - sunShadowStrength in full shadow.
float SunShadowAmbient(float shadow) {
    return mix(1.0 - sunShadowStrength, 1.0, shadow);
}

// CalcSunLight with the direct light scaled by the shadow. The sun's diffuse and specular are what a shadow takes
// away, and sunShadowStrength of the ambient.
vec3 CalcSunLightShadowed(Sunlight light, vec3 normal, vec3 viewDir, float shadow) {
    vec3 lightDir = normalize(light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0);
    return clamp(light.ambient * SunShadowAmbient(shadow) + ((light.diffuse * diff) + (light.specular * spec)) * shadow, 0.0, 1.0);
}
