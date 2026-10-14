// Water surface for the linear pipeline: an animated normal map, the sky reflected by it and the sun's highlight.
// Include it after sky.glsl, and after shadow.glsl when SUN_SHADOWS is defined.

uniform sampler2D waterNormals; // Tiling normal map, green up.
uniform float waterTime; // Seconds.
uniform vec3 skyZenith; // Linear sky colour overhead.
uniform vec3 skyHorizon; // Linear sky colour at the horizon.

#ifdef WATER_SHADER
const float WATER_REFLECTIVITY = 0.5;
#else
const float WATER_REFLECTIVITY = 0.3; // Of the Fresnel reflection, the rest of the water keeps its texture.
#endif
const float WATER_SPECULAR = 5.0; // Linear brightness of the sun's highlight, past white so that it blooms.

// Two layers of the normal map scrolling across each other.
vec3 WaterNormal(vec3 pos) {
    vec3 n1 = texture(waterNormals, pos.xy / 1024.0 + waterTime * vec2(0.012, 0.007)).xyz * 2.0 - 1.0;
    vec3 n2 = texture(waterNormals, pos.yx / 613.0 + waterTime * vec2(-0.008, 0.011)).xyz * 2.0 - 1.0;
    return normalize(vec3((n1.xy + n2.yx) * 0.35, n1.z * n2.z));
}

#ifdef WATER_SHADER
uniform sampler2DArray waterAverages; // The texture array of the water's textures again, with its mip levels.

const float WATER_DETAIL = 0.15; // Part of the stock water texture's contrast kept over the sea's colour.
const float WATER_POOL_DETAIL = 0.7; // Same for pools, which show no sky to break up their colour.
const float WATER_DEPTH_FALLOFF = 350.0; // World units from the shore over which the water deepens by a factor of e.
const float WATER_GLINT = 2.0; // Linear brightness of a point light's glint on the water.
const float WATER_FOAM_REACH = 180.0; // World units from the shore that waves break in.
const float WATER_POOL_SHORE = 150.0; // Distance from the shore that pools and basins are coloured for.

// Average linear colour of a layer of the water's texture array.
vec3 WaterAverage(float layer) {
    return textureLod(waterAverages, vec3(0.5, 0.5, layer), 16.0).rgb;
}

// Three layers of the normal map scrolling across each other. The finest fades out with distance, where it would alias.
vec3 WaterNormalLayers(vec3 pos, float viewDistance) {
    vec3 n1 = texture(waterNormals, pos.xy / 1024.0 + waterTime * vec2(0.012, 0.007)).xyz * 2.0 - 1.0;
    vec3 n2 = texture(waterNormals, pos.yx / 613.0 + waterTime * vec2(-0.008, 0.011)).xyz * 2.0 - 1.0;
    vec3 n3 = texture(waterNormals, pos.xy / 211.0 + waterTime * vec2(0.021, -0.017)).xyz * 2.0 - 1.0;
    float fine = 1.0 - smoothstep(1500.0, 5000.0, viewDistance);
    return normalize(vec3((n1.xy + n2.yx + n3.yx * fine * 0.5) * 0.2, n1.z * n2.z));
}

// Linear colour of the water body: the average of its texture, lighter in the shallows and darker away from the shore,
// a little of the texture's own detail, and foam where waves break on the shore.
vec3 WaterBody(vec3 average, vec3 detail, float detailAmount, float shore, float foam, vec3 normal) {
    float depth = 1.0 - exp(-shore / WATER_DEPTH_FALLOFF);
    vec3 colour = mix(average * 1.15, average * 0.55, depth);
    colour = mix(colour, colour * detail / max(average, vec3(0.01)), detailAmount);
    float wave = sin(shore * 0.045 + waterTime * 1.6 + normal.x * 4.0); // Crests that run in towards the shore.
    float breaking = smoothstep(0.55, 1.0, wave) * (1.0 - smoothstep(20.0, WATER_FOAM_REACH, shore));
    float edge = 1.0 - smoothstep(0.0, 28.0, shore);
    return mix(colour, vec3(0.85), clamp(breaking + edge * 0.7, 0.0, 1.0) * 0.8 * foam);
}

// Glints of the point lights in reach on the rippled water, as pools reflect no sky.
vec3 WaterLightGlints(vec3 normal, vec3 pos, vec3 viewDir) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < num_point_lights; i++) {
        if (fspointlights[i].type <= 0.0 || fspointlights[i].radius < 1.0)
            continue;
        vec3 toLight = fspointlights[i].position - pos;
        float lightDistance = length(toLight);
        if (lightDistance > fspointlights[i].radius)
            continue;
        float towards = max(dot(reflect(-toLight / lightDistance, normal), viewDir), 0.0);
        result += srgbToLinear(fspointlights[i].diffuse) * pow(towards, 300.0) * (1.0 - lightDistance / fspointlights[i].radius) * WATER_GLINT;
    }
    return result;
}
#endif

// Water lit as before, with the sky reflected over it by Fresnel and the sun's highlight on top.
vec3 WaterSurface(vec3 lit, vec3 normal, vec3 pos, vec3 viewDir) {
    vec3 reflected = reflect(-viewDir, normal);
    vec3 sky = mix(skyHorizon, skyZenith, clamp(reflected.z, 0.0, 1.0)) * SkyGradient(reflected);
    float fresnel = 0.02 + 0.98 * pow(clamp(1.0 - dot(normal, viewDir), 0.0, 1.0), 5.0); // The dot rounds past 1 looking straight down, and pow of a negative base is NaN.
    vec3 result = mix(lit, sky, fresnel * WATER_REFLECTIVITY);

#ifdef WATER_SHADER
    float roughness = clamp(length(normal.xy) * 3.0, 0.0, 1.0); // Rougher water spreads the highlight into glints.
    float towardsSun = max(dot(reflected, sunDirection), 0.0);
    float highlight = (pow(towardsSun, mix(1200.0, 150.0, roughness)) + pow(towardsSun, 40.0) * 0.02) * sunVisibility;
#else
    float highlight = pow(max(dot(reflected, sunDirection), 0.0), 500.0) * sunVisibility;
#endif
#ifdef SUN_SHADOWS
    highlight *= SunShadow(pos, vec3(0.0, 0.0, 1.0));
#endif
    return result + SunColour() * highlight * WATER_SPECULAR;
}
