#include "precision.glsl"

in vec2 texuv;

out vec4 FragColour;

#ifdef AO_BLUR

uniform sampler2D source; // Occlusion in r and the distance along the view axis in g.

// 4x4 blur that evens out the 4x4 rotation pattern of the samples, without blurring across depth edges.
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(source, 0);
    float depth = texelFetch(source, pixel, 0).g;
    float sum = 0.0;
    float total = 0.0;
    for (int y = -2; y < 2; y++) {
        for (int x = -2; x < 2; x++) {
            vec2 tap = texelFetch(source, clamp(pixel + ivec2(x, y), ivec2(0), size - 1), 0).rg;
            float weight = 1.0 / (1.0 + 20.0 * abs(tap.y - depth) / max(depth, 1.0));
            sum += tap.x * weight;
            total += weight;
        }
    }
    FragColour = vec4(sum / total, depth, 0.0, 1.0);
}

#else

uniform sampler2D depthMap; // Distance along the view axis, 0 where nothing was drawn.
uniform vec4 projection; // [0][0], [1][1], [2][0] and [2][1] of the projection matrix.

const int SAMPLES = 12;
const float RADIUS = 80.0; // World units.
const float BIAS = 4.0;
const float FADE_START = 3000.0; // Distance where the occlusion starts to fade out, it's noise further away.
const float FADE_END = 6000.0;

vec3 ViewPosition(vec2 uv, float depth) {
    return vec3((uv * 2.0 - 1.0 + projection.zw) / projection.xy * depth, -depth);
}

vec2 ScreenPosition(vec3 viewPos) {
    return ((projection.xy * viewPos.xy + projection.zw * viewPos.z) / -viewPos.z) * 0.5 + 0.5;
}

// Screen-space ambient occlusion: how much of the hemisphere above each pixel is free of nearby geometry.
void main() {
    float depth = texture(depthMap, texuv).r;
    vec3 pos = ViewPosition(texuv, depth);
    vec3 normal = normalize(cross(dFdx(pos), dFdy(pos)));
    if (dot(normal, pos) > 0.0)
        normal = -normal;
    if (depth <= 0.0 || depth > FADE_END) {
        FragColour = vec4(1.0, depth, 0.0, 1.0);
        return;
    }

    // Each pixel of a 4x4 tile turns the samples by a different angle.
    ivec2 tile = ivec2(gl_FragCoord.xy) % 4;
    float angle = float(tile.x * 4 + tile.y) * 2.39996;
    vec3 random = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = random - normal * dot(random, normal);
    tangent = length(tangent) > 0.001 ? normalize(tangent) : normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
    vec3 bitangent = cross(normal, tangent);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; i++) {
        // Spread over the hemisphere and closer to the centre for the first samples. None lie flat on the surface,
        // where the error of the normal would put half of them under it.
        float phi = float(i) * 2.39996;
        float cosTheta = mix(1.0, 0.25, (float(i) + 0.5) / float(SAMPLES));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float scale = float(i + 1) / float(SAMPLES);
        scale = mix(0.15, 1.0, scale * scale);
        vec3 offset = (tangent * cos(phi) * sinTheta + bitangent * sin(phi) * sinTheta + normal * cosTheta) * RADIUS * scale;
        vec3 samplePos = pos + offset;

        vec2 sampleUv = ScreenPosition(samplePos);
        float sampleDepth = textureLod(depthMap, sampleUv, 0.0).r;
        if (sampleDepth > 0.0 && sampleDepth < -samplePos.z - BIAS - depth * 0.002)
            occlusion += smoothstep(0.0, 1.0, RADIUS / abs(depth - sampleDepth));
    }
    float ao = 1.0 - occlusion / float(SAMPLES);
    ao = mix(ao, 1.0, smoothstep(FADE_START, FADE_END, depth));
    FragColour = vec4(ao, depth, 0.0, 1.0);
}

#endif
