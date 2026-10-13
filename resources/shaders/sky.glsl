// Sky colours and the sun for the linear pipeline, shared by the sky and the water that reflects it.

uniform vec3 sunDirection; // Towards the sun.
uniform float sunVisibility; // 0 at night, indoors and in foggy weather, 1 with the sun clear of the horizon.

// Colour of the sun disc and its highlights, reddening as the sun nears the horizon.
vec3 SunColour() {
    return mix(vec3(1.0, 0.55, 0.3), vec3(1.0, 0.95, 0.85), smoothstep(0.0, 0.4, sunDirection.z));
}

// Brightness of the sky towards a direction relative to its average: lighter at the horizon, deeper overhead.
float SkyGradient(vec3 direction) {
    return mix(1.15, 0.8, sqrt(clamp(direction.z, 0.0, 1.0)));
}
