#include "precision.glsl"
#include "fog.glsl"

in vec3 worldPos;
in vec4 colour;
in vec2 texsize;
in vec2 timeoffset;

out vec4 FragColour;

uniform sampler2D texture0;
uniform vec3 eye;
uniform float planeHeight; // Height of the sky plane above the eye.
uniform float elevationBias; // Added to the tangent of the elevation, lowers the horizon of the sky plane.
uniform int fade; // Fade into the tint towards the horizon.
uniform float fadeTop; // Elevation tangent where the fade starts.
uniform float fadeBottom; // Elevation tangent where the fade ends, solid tint below.
uniform float horizon; // Elevation tangent below which nothing is drawn when not fading.
uniform FogParam fog;
uniform float fogDistance;

void main() {
    vec3 ray = worldPos - eye;
    float horizontal = max(length(ray.xy), 0.0001);
    float elevation = ray.z / horizontal;
    if (fade == 0 && elevation < horizon)
        discard;

    // Where the ray hits the sky plane, relative to the eye.
    float up = max(elevation + elevationBias, 0.0001);
    vec2 planePos = ray.xy / horizontal * (planeHeight / up);

    vec3 sky = texture(texture0, (timeoffset + planePos) / texsize).rgb * colour.rgb;
    if (fade != 0)
        sky = mix(sky, colour.rgb, clamp((fadeTop - elevation) / (fadeTop - fadeBottom), 0.0, 1.0));

    float fograt = getFogRatio(fog, fogDistance);
    FragColour = vec4(mix(sky, fog.color, fograt), 1.0);
}
