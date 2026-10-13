// Sun shadows from the cascaded shadow map. Include it after lighting.glsl.

#define SUN_SHADOW_CASCADES 4

uniform highp sampler2DArrayShadow sunShadowMap;
uniform mat4 sunShadowMatrices[SUN_SHADOW_CASCADES]; // World to shadow map texture space, with depth in [0, 1].
uniform float sunShadowTexels[SUN_SHADOW_CASCADES]; // World size of a shadow map texel in each cascade.
uniform float sunShadowFade; // 0 when the sun casts no shadows, 1 when they're at full strength.

const float SUN_SHADOW_BLEND = 0.1; // Part of each cascade, at its border, that blends into the next one.

// 3x3 taps of bilinear depth comparisons. textureGrad keeps the lookup valid in non-uniform control flow.
float SunShadowSample(int cascade, vec3 pos, vec3 normal) {
    vec3 offsetPos = pos + normal * sunShadowTexels[cascade] * 1.5;
    vec4 coord = sunShadowMatrices[cascade] * vec4(offsetPos, 1.0);
    vec2 texel = vec2(1.0) / vec2(textureSize(sunShadowMap, 0).xy);
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            lit += textureGrad(sunShadowMap, vec4(coord.xy + vec2(x, y) * texel, float(cascade), coord.z), vec2(0.0), vec2(0.0));
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

// CalcSunLight with the direct light scaled by the shadow. The ambient term is left alone, the sun's diffuse and
// specular are what a shadow takes away.
vec3 CalcSunLightShadowed(Sunlight light, vec3 normal, vec3 viewDir, float shadow) {
    vec3 lightDir = normalize(light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0);
    return clamp(light.ambient + ((light.diffuse * diff) + (light.specular * spec)) * shadow, 0.0, 1.0);
}
