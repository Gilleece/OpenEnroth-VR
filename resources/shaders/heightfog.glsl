// The stock fog as height fog with sunlight scattered in it, for the linear pipeline. Include it after fog.glsl.

uniform vec3 fogEye; // The fog has the stock density at the eye's height and below, and thins above it.
uniform float fogFalloff; // How fast the fog thins with height, per world unit. 0 keeps the stock fog.
uniform vec3 fogSunDirection; // Towards the sun.
uniform vec3 fogInscatter; // How much the fog brightens towards the sun, per colour channel. 0 at night and indoors.

// Average density of the fog along the ray from the eye to pos, relative to its density at the eye.
float FogHeightDensity(vec3 pos) {
    float rise = max(pos.z - fogEye.z, 0.0) * fogFalloff;
    return rise < 0.0001 ? 1.0 : (1.0 - exp(-rise)) / rise;
}

// getFogRatio with the weather's fog thinned by its height. The fade into the far clip plane stays as it is.
float getHeightFogRatio(FogParam fog, float dist, vec3 pos) {
    float weather = fog.weakDensity + (fog.strongDensity - fog.weakDensity) * safeSmooth(fog.weakDistance, fog.strongDistance, dist);
    weather *= FogHeightDensity(pos);
    return weather + (1.0 - weather) * safeSmooth(fog.strongDistance, fog.clipDistance, dist);
}

// The fog colour looking towards pos, brighter towards the sun.
vec3 getHeightFogColour(FogParam fog, vec3 pos) {
    vec3 ray = pos - fogEye;
    float len = length(ray);
    float towardsSun = len > 0.0 ? max(dot(ray / len, fogSunDirection), 0.0) : 0.0;
    return fog.color * (vec3(1.0) + fogInscatter * pow(towardsSun, 4.0));
}
