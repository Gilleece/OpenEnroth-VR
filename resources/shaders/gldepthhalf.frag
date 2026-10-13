#include "precision.glsl"

out vec4 FragColour;

uniform highp sampler2D depthMap; // Depth buffer of the world target.
uniform ivec2 sourceOrigin; // Corner of the world viewport in the depth buffer, in pixels.

// The farthest depth of the 2x2 pixels under this one, so that a billboard doesn't fade against the edge of something
// nearer that it's drawn next to.
void main() {
    ivec2 pos = sourceOrigin + ivec2(gl_FragCoord.xy) * 2;
    float depth = max(max(texelFetch(depthMap, pos, 0).r, texelFetch(depthMap, pos + ivec2(1, 0), 0).r),
                      max(texelFetch(depthMap, pos + ivec2(0, 1), 0).r, texelFetch(depthMap, pos + ivec2(1, 1), 0).r));
    FragColour = vec4(depth);
}
