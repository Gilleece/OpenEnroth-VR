layout (location = 0) in vec3 vaOrigin; // Where the emitter's particles rise from.
layout (location = 1) in vec4 vaParticle; // Kind (0 flame, 1 spark, 2 smoke), phase in its life cycle, seed, flame size.
layout (location = 2) in vec4 vaColour; // Colour of a new flame, and the emitter's spread.

out vec2 texuv;
out vec4 colour;
flat out float seed;

uniform mat4 view;
uniform mat4 projection;
uniform float time; // Seconds.
uniform vec2 windDirection;
uniform vec3 smokeAmbient; // Light on the smoke away from its fire.

const vec2 CORNERS[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

float Hash(float n) {
    return fract(sin(n) * 43758.5453);
}

void main() {
    int kind = int(vaParticle.x + 0.5);
    float flameSize = vaParticle.w;
    float grow = flameSize / 22.0;
    float life = kind == 0 ? 0.9 : kind == 1 ? 1.6 : 5.0;

    // Each particle loops through its life, a new one with a new seed every time round.
    float cycle = time / life + vaParticle.y;
    float age = fract(cycle);
    float n = vaParticle.z * 7.31 + floor(cycle) * 13.17;
    seed = Hash(n);
    vec2 start = (vec2(Hash(n + 1.0), Hash(n + 2.0)) * 2.0 - 1.0) * vaColour.w;
    float seconds = age * life;

    vec3 offset;
    float diameter;
    float stretch = 1.0;
    if (kind == 0) {
        // Flames lick upwards, narrowing towards the middle, and lean a little with the wind.
        offset = vec3(start * (1.0 - age * 0.7) + windDirection * seconds * 6.0, (40.0 + 60.0 * Hash(n + 3.0)) * pow(age, 0.8) * grow);
        diameter = flameSize * mix(1.0, 0.35, age);
        stretch = 1.6;
        colour = vec4(vaColour.rgb * mix(1.0, 0.3, smoothstep(0.1, 0.6, age)), (1.0 - age) * smoothstep(0.0, 0.1, age));
    } else if (kind == 1) {
        // Sparks rise fast, wander and burn out.
        vec2 wander = (vec2(Hash(n + 4.0), Hash(n + 5.0)) - 0.5) * 60.0 * age;
        offset = vec3(start * 0.5 + wander + windDirection * seconds * 18.0, 140.0 * age * (0.6 + 0.4 * Hash(n + 6.0)) * grow);
        diameter = 2.5;
        colour = vec4(vaColour.rgb / max(vaColour.r, max(vaColour.g, vaColour.b)) * 4.0 + 0.5, 1.0 - age);
    } else {
        // Smoke rises slowly, swells and drifts along the wind.
        offset = vec3(start * 0.6 + windDirection * seconds * 22.0, (40.0 + 220.0 * age * (0.7 + 0.3 * Hash(n + 8.0))) * grow);
        diameter = flameSize * mix(1.2, 4.5, age);
        vec3 lit = mix(vaColour.rgb * 0.35, smokeAmbient, smoothstep(0.0, 0.5, age));
        colour = vec4(lit * 0.35, 0.35 * smoothstep(0.0, 0.15, age) * (1.0 - age));
    }

    vec2 corner = CORNERS[gl_VertexID % 6];
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up = vec3(view[0][1], view[1][1], view[2][1]);
    vec3 pos = vaOrigin + offset + (right * corner.x + up * corner.y * stretch) * diameter * 0.5;
    texuv = corner * 0.5 + 0.5;

    // Puffs come forward by their size, so that a torch's flames clear the wall it hangs on.
    vec4 viewPos = view * vec4(pos, 1.0);
    viewPos.z += diameter;
    gl_Position = projection * viewPos;
}
