// Shadows of the nearest wall torches from cube shadow maps. Include it after declaring fspointlights.

uniform highp samplerCubeArrayShadow torchShadowMaps;
uniform int torchShadowLayers[num_point_lights]; // Cube map layer of each light's shadow map, -1 for lights without one.

// 1 where the light reaches the point, 0 where something stands between them.
float TorchShadow(int light, vec3 pos, vec3 normal) {
    int layer = torchShadowLayers[light];
    if (layer < 0)
        return 1.0;

    vec3 toPoint = pos + normal * 2.0 - fspointlights[light].position; // A little off the surface against acne.
    float depth = length(toPoint) / fspointlights[light].radius - 0.002;
    return texture(torchShadowMaps, vec4(toPoint, float(layer)), depth);
}
