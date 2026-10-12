// Conversions between sRGB encoded and linear colour, with the exact sRGB transfer curves.

vec3 srgbToLinear(vec3 colour) {
    colour = max(colour, vec3(0.0));
    return mix(colour / 12.92, pow((colour + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), colour));
}

vec3 linearToSrgb(vec3 colour) {
    colour = max(colour, vec3(0.0));
    return mix(colour * 12.92, 1.055 * pow(colour, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), colour));
}
