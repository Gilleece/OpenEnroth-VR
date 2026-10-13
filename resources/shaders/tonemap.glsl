// Tonemap curves of the composite, which the eye adaptation compares.

const float KNEE = 0.6038; // Linear value of 0.8 sRGB encoded.

// Identity up to the knee, then a curve that keeps the slope of 1 at the knee and approaches white.
vec3 Shoulder(vec3 colour) {
    vec3 over = max(colour - KNEE, vec3(0.0));
    return min(colour, vec3(KNEE)) + (1.0 - KNEE) * (1.0 - exp(-over / (1.0 - KNEE)));
}

const float FILMIC_SCALE = 0.85; // Exposure into the filmic curve.

// Krzysztof Narkowicz's fit of the ACES filmic curve, CC0 or MIT licensed:
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 Filmic(vec3 colour) {
    colour *= FILMIC_SCALE;
    return clamp((colour * (2.51 * colour + 0.03)) / (colour * (2.43 * colour + 0.59) + 0.14), 0.0, 1.0);
}
