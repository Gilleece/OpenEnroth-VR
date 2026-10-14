// Shadows of the nearest indoor lights from cube shadow maps. Include it after declaring fspointlights.

uniform highp samplerCubeArrayShadow torchShadowMaps;
uniform int torchShadowLayers[num_point_lights]; // Cube map layer of each light's shadow map, -1 for lights without one.
uniform float torchShadowStrengths[num_point_lights]; // How far each shadow has faded in since its map was first drawn.

// 1 where the light reaches the point, 0 where something stands between them.
float TorchShadow(int light, vec3 pos, vec3 normal) {
    int layer = torchShadowLayers[light];
    if (layer < 0)
        return 1.0;

    vec3 toPoint = pos + normal * 2.0 - fspointlights[light].position; // A little off the surface against acne.
    float depth = length(toPoint) / fspointlights[light].radius - 0.002;
    return mix(1.0, texture(torchShadowMaps, vec4(toPoint, float(layer)), depth), torchShadowStrengths[light]);
}
