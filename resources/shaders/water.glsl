// Water surface for the linear pipeline: an animated normal map, the sky reflected by it and the sun's highlight.
// Include it after sky.glsl, and after shadow.glsl when SUN_SHADOWS is defined.

uniform sampler2D waterNormals; // Tiling normal map, green up.
uniform float waterTime; // Seconds.
uniform vec3 skyZenith; // Linear sky colour overhead.
uniform vec3 skyHorizon; // Linear sky colour at the horizon.

const float WATER_REFLECTIVITY = 0.3; // Of the Fresnel reflection, the rest of the water keeps its texture.
const float WATER_SPECULAR = 5.0; // Linear brightness of the sun's highlight, past white so that it blooms.

// Two layers of the normal map scrolling across each other.
vec3 WaterNormal(vec3 pos) {
    vec3 n1 = texture(waterNormals, pos.xy / 1024.0 + waterTime * vec2(0.012, 0.007)).xyz * 2.0 - 1.0;
    vec3 n2 = texture(waterNormals, pos.yx / 613.0 + waterTime * vec2(-0.008, 0.011)).xyz * 2.0 - 1.0;
    return normalize(vec3((n1.xy + n2.yx) * 0.35, n1.z * n2.z));
}

// Water lit as before, with the sky reflected over it by Fresnel and the sun's highlight on top.
vec3 WaterSurface(vec3 lit, vec3 normal, vec3 pos, vec3 viewDir) {
    vec3 reflected = reflect(-viewDir, normal);
    vec3 sky = mix(skyHorizon, skyZenith, clamp(reflected.z, 0.0, 1.0)) * SkyGradient(reflected);
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);
    vec3 result = mix(lit, sky, fresnel * WATER_REFLECTIVITY);

    float highlight = pow(max(dot(reflected, sunDirection), 0.0), 500.0) * sunVisibility;
#ifdef SUN_SHADOWS
    highlight *= SunShadow(pos, vec3(0.0, 0.0, 1.0));
#endif
    return result + SunColour() * highlight * WATER_SPECULAR;
}
