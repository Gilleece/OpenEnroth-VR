// Additive billboards that fade out where they meet the level behind them, for the linear pipeline.

uniform sampler2D sceneDepth; // Depth buffer of the level, copied before the billboards.
uniform vec4 sceneViewport; // Origin and size of the world viewport, in pixels.
uniform vec2 sceneProjection; // [2][2] and [3][2] of the projection matrix.
uniform float softDistance; // Distance in front of the level over which a billboard fades out, 0 for a hard edge.

// Distance along the view axis from a window depth.
float SceneDistance(float depth) {
    return sceneProjection.y / (depth * 2.0 - 1.0 + sceneProjection.x);
}

// 1 where the fragment is softDistance or more in front of the level, falling to 0 where it meets it.
float SoftFade() {
    if (softDistance <= 0.0)
        return 1.0;
    vec2 uv = (gl_FragCoord.xy - sceneViewport.xy) / sceneViewport.zw;
    float scene = SceneDistance(texture(sceneDepth, uv).r);
    return clamp((scene - SceneDistance(gl_FragCoord.z)) / softDistance, 0.0, 1.0);
}
