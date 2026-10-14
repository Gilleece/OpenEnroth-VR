#include "precision.glsl"
#ifdef LINEAR_PIPELINE
#include "colour.glsl"
#include "sky.glsl"
#endif
#include "fog.glsl"
#ifdef LINEAR_PIPELINE
#include "heightfog.glsl"
#endif

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

#if defined(LINEAR_PIPELINE) && (defined(SKY_CLOUDS) || defined(NIGHT_SKY))
uniform int procedural; // 1 for the outdoor sky, 0 for the sky seen through indoor faces, which stays stock.

// Hash of an integer cell to 0 to 1.
float CellHash(uvec2 cell) {
    uint h = cell.x * 1597334677u ^ cell.y * 3812015801u;
    h = (h ^ (h >> 16)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    return float(h ^ (h >> 16)) / 4294967295.0;
}
#endif

#if defined(LINEAR_PIPELINE) && defined(SKY_CLOUDS)
uniform vec3 skyClear; // Mean sRGB colour of the open sky in the sky bitmap, under the procedural clouds.
uniform vec2 cloudOffset; // Drift of the clouds along the wind, in noise cells, wrapped to the noise period.
uniform float cloudCoverage; // 0 for a clear sky, 1 for an overcast one.

const float CLOUD_HEIGHT = 6000.0; // World height of the cloud layer.
const float CLOUD_CELL = 2500.0; // World units per noise cell.
const float CLOUD_FADE = 60000.0; // Distance over which the clouds fade into the haze towards the horizon.
const uint NOISE_PERIOD = 256u; // Cells after which the noise repeats.

// Tileable value noise, 0 to 1.
float ValueNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = p - cell;
    vec2 u = f * f * (3.0 - 2.0 * f);
    uvec2 c = uvec2(ivec2(cell) & ivec2(NOISE_PERIOD - 1u));
    uvec2 n = (c + 1u) & (NOISE_PERIOD - 1u);
    return mix(mix(CellHash(c), CellHash(uvec2(n.x, c.y)), u.x), mix(CellHash(uvec2(c.x, n.y)), CellHash(n), u.x), u.y);
}

float CloudNoise(vec2 p, int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; i++) {
        sum += ValueNoise(p) * amplitude;
        p = p * 2.0 + vec2(17.0, 31.0); // Whole cells and a factor of 2 keep every octave tileable.
        amplitude *= 0.5;
    }
    return sum;
}
#endif

#if defined(LINEAR_PIPELINE) && defined(NIGHT_SKY)
uniform float nightVisibility; // 0 by day and in foggy weather, 1 at night.
uniform float starRotation; // Turn of the sky about the pole, in radians.
uniform vec3 moonDirection; // Towards the moon.
uniform float moonPhase; // 0 at new moon, pi at full moon.

const vec3 SKY_POLE = vec3(0.0, 0.6, 0.8);
const float STAR_CELLS = 220.0; // Cells across each face of the star cube.
const float MOON_RADIUS = 0.018; // Tangent of the moon's angular radius.

vec3 Stars(vec3 direction) {
    // Turn the direction about the pole, then look up its cell on a cube around the eye.
    vec3 axis = normalize(SKY_POLE);
    float c = cos(starRotation);
    float s = sin(starRotation);
    vec3 d = direction * c + cross(axis, direction) * s + axis * dot(axis, direction) * (1.0 - c);
    vec3 a = abs(d);
    vec2 face = a.x > a.y && a.x > a.z ? d.yz / a.x : a.y > a.z ? d.xz / a.y : d.xy / a.z;
    float faceId = a.x > a.y && a.x > a.z ? (d.x > 0.0 ? 0.0 : 1.0) : a.y > a.z ? (d.y > 0.0 ? 2.0 : 3.0) : (d.z > 0.0 ? 4.0 : 5.0);
    vec2 grid = (face * 0.5 + 0.5) * STAR_CELLS;
    uvec2 cell = uvec2(floor(grid)) + uvec2(uint(faceId) * 1000u, 7u);
    float h = CellHash(cell);
    if (h < 0.985)
        return vec3(0.0);
    vec2 centre = vec2(CellHash(cell + 11u), CellHash(cell + 23u)) * 0.6 + 0.2;
    float dist = length(fract(grid) - centre);
    float brightness = pow((h - 0.985) / 0.015, 3.0) * 2.5 + 0.15;
    vec3 tint = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.9, 0.75), CellHash(cell + 41u));
    return tint * brightness * (1.0 - smoothstep(0.08, 0.2, dist));
}

// The moon's colour where the direction meets its disc, and its coverage there in alpha.
vec4 Moon(vec3 direction) {
    float facing = dot(direction, moonDirection);
    if (facing <= 0.0)
        return vec4(0.0);
    vec3 right = normalize(cross(moonDirection, vec3(0.0, 0.0, 1.0)));
    vec3 up = cross(right, moonDirection);
    vec2 local = vec2(dot(direction, right), dot(direction, up)) / facing / MOON_RADIUS;
    float r = length(local);
    float coverage = 1.0 - smoothstep(0.92, 1.0, r);
    if (coverage <= 0.0)
        return vec4(0.0);
    vec3 normal = vec3(local, sqrt(max(1.0 - r * r, 0.0)));
    vec3 light = vec3(sin(moonPhase), 0.0, -cos(moonPhase)); // From behind the moon at new moon, from the eye at full.
    float lit = smoothstep(-0.08, 0.08, dot(normal, light));
    float mottle = 0.85 + 0.15 * CellHash(uvec2(floor((local + 1.0) * 6.0)));
    return vec4(vec3(1.0, 0.97, 0.9) * (0.03 + 2.5 * lit * mottle), coverage);
}
#endif

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
#if defined(LINEAR_PIPELINE) && defined(SKY_CLOUDS)
    if (procedural != 0)
        sky = skyClear * colour.rgb;
#endif
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
#if defined(NIGHT_SKY)
    if (procedural != 0 && direction.z > 0.0) {
        sky += Stars(direction) * nightVisibility * smoothstep(0.0, 0.2, direction.z); // The haze at the horizon hides them.
        vec4 moon = Moon(direction);
        sky = mix(sky, moon.rgb, moon.a * nightVisibility);
    }
#endif
#if defined(SKY_CLOUDS)
    if (procedural != 0 && direction.z > 0.0) {
        // Where the ray meets the cloud layer, a plane fixed in the world that both eyes see in the same place.
        float cloudDistance = max(CLOUD_HEIGHT - eye.z, 500.0) / direction.z;
        vec2 cloudPos = (eye.xy + direction.xy * cloudDistance) / CLOUD_CELL + cloudOffset;
        float density = CloudNoise(cloudPos, 4);
        float threshold = mix(0.62, 0.3, cloudCoverage);
        float cover = smoothstep(threshold, threshold + 0.15, density);
        // A step towards the sun through the cloud: thinner there lights its sunward edges, thicker shades its core.
        float towardsSunDensity = CloudNoise(cloudPos + normalize(sunDirection.xy + vec2(0.0001)) * 0.12, 2);
        float light = clamp(0.6 - (towardsSunDensity - density * 0.75) * 4.0, 0.0, 1.0);
        // Clouds near a low sun glow with its colour, light passing through them towards the eye.
        float glow = pow(towardsSun, 8.0) * low * 2.0;
        vec3 cloud = sky * 0.75 + SunColour() * sunVisibility * (mix(0.25, 1.1, light) + glow);
        sky = mix(sky, cloud, cover * exp(-cloudDistance / CLOUD_FADE));
    }
#endif
    sky += SunColour() * sunVisibility * (disc * 12.0 + pow(towardsSun, 600.0) * 0.8);
#endif

#ifdef LINEAR_PIPELINE
    vec3 fogPos = eye + normalize(ray) * fogDistance;
    float fograt = getHeightFogRatio(fog, fogDistance, fogPos);
    FragColour = vec4(mix(sky, getHeightFogColour(fog, fogPos), fograt), 1.0);
#else
    float fograt = getFogRatio(fog, fogDistance);
    FragColour = vec4(mix(sky, fog.color, fograt), 1.0);
#endif
}
