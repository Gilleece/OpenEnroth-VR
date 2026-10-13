// Ambient occlusion of the 3D world, sampled from the half resolution occlusion of the current view.

uniform sampler2D ambientOcclusionMap; // Occlusion in r and the distance along the view axis in g.
uniform vec4 ambientOcclusionViewport; // Origin and size of the world viewport, in pixels.
uniform vec2 ambientOcclusionDepth; // [2][2] and [3][2] of the projection matrix.

// Scale of the light at this fragment, from the occlusion of the four nearest occlusion texels weighted by how close
// their depth is to its own.
float AmbientOcclusion() {
    float depth = ambientOcclusionDepth.y / (gl_FragCoord.z * 2.0 - 1.0 + ambientOcclusionDepth.x);
    vec2 size = vec2(textureSize(ambientOcclusionMap, 0));
    vec2 pos = (gl_FragCoord.xy - ambientOcclusionViewport.xy) / ambientOcclusionViewport.zw * size - 0.5;
    vec2 base = floor(pos);
    vec2 f = pos - base;
    vec2 corner = (base + 1.0) / size; // The four texels around the corner they share, whatever the rounding.
    vec4 occlusion = textureGather(ambientOcclusionMap, corner, 0);
    vec4 depths = textureGather(ambientOcclusionMap, corner, 1);
    vec4 bilinear = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y)); // Gather order.
    vec4 weights = bilinear / (vec4(1.0) + 20.0 * abs(depths - vec4(depth)) / max(depth, 1.0)) + vec4(0.00001);
    float ao = dot(weights, occlusion) / dot(weights, vec4(1.0));
    return pow(ao, 1.0 / 2.2); // The world shaders sum their light before the sRGB decode, which would square it again.
}
