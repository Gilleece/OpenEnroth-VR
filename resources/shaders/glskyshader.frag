#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "colour.glsl"
#include "sky.glsl"
#endif
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
#ifdef LINEAR_PIPELINE
    sky = srgbToLinear(sky); // The fade to the horizon tint stays in sRGB, as the stock sky blends it.

    vec3 direction = normalize(ray);
    sky *= SkyGradient(direction);
    float towardsSun = max(dot(direction, sunDirection), 0.0);
    float low = 1.0 - smoothstep(0.0, 0.5, sunDirection.z);
    sky += sky * SunColour() * pow(towardsSun, 4.0) * low * sunVisibility * 0.8; // Dawn and dusk warm the sky around the sun.
    float disc = smoothstep(0.99974, 0.99985, towardsSun); // About a degree across.
    sky += SunColour() * sunVisibility * (disc * 12.0 + pow(towardsSun, 600.0) * 0.8);
#endif

    float fograt = getFogRatio(fog, fogDistance);
    FragColour = vec4(mix(sky, fog.color, fograt), 1.0);
}
