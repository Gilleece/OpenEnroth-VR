#include "precision.glsl"

in vec2 texuv;

out vec4 FragColour;

uniform sampler2D source; // Display colour, sampled with bilinear filtering.
uniform vec2 texelSize;

const float EDGE_THRESHOLD = 0.125; // Smallest local contrast that counts as an edge, relative to the brightest neighbour.
const float EDGE_THRESHOLD_MIN = 0.0312; // Contrast below which dark areas are left alone.
const float SUBPIXEL_QUALITY = 0.75;
const int SEARCH_STEPS = 10;

float Luma(vec3 colour) {
    return dot(colour, vec3(0.299, 0.587, 0.114));
}

float LumaAt(vec2 uv) {
    return Luma(texture(source, uv).rgb);
}

// Distance in texels covered by each step of the search along an edge.
float SearchStep(int step) {
    if (step < 5)
        return 1.0;
    if (step == 5)
        return 1.5;
    if (step < 9)
        return 2.0;
    return 4.0;
}

// Fast approximate antialiasing: finds the edge through each pixel from the luma of its neighbours, searches along
// it for its ends, and blends with the neighbour across the edge by how close the pixel is to the nearer end.
void main() {
    vec4 centre = texture(source, texuv);
    float lumaM = Luma(centre.rgb);
    float lumaS = LumaAt(texuv + vec2(0.0, -texelSize.y));
    float lumaN = LumaAt(texuv + vec2(0.0, texelSize.y));
    float lumaW = LumaAt(texuv + vec2(-texelSize.x, 0.0));
    float lumaE = LumaAt(texuv + vec2(texelSize.x, 0.0));

    float lumaMin = min(lumaM, min(min(lumaS, lumaN), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaS, lumaN), max(lumaW, lumaE)));
    float range = lumaMax - lumaMin;
    if (range < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD)) {
        FragColour = centre;
        return;
    }

    float lumaSW = LumaAt(texuv - texelSize);
    float lumaNE = LumaAt(texuv + texelSize);
    float lumaNW = LumaAt(texuv + vec2(-texelSize.x, texelSize.y));
    float lumaSE = LumaAt(texuv + vec2(texelSize.x, -texelSize.y));

    float lumaSN = lumaS + lumaN;
    float lumaWE = lumaW + lumaE;
    float lumaWestCorners = lumaSW + lumaNW;
    float lumaEastCorners = lumaSE + lumaNE;
    float lumaSouthCorners = lumaSW + lumaSE;
    float lumaNorthCorners = lumaNW + lumaNE;

    float edgeHorizontal = abs(-2.0 * lumaW + lumaWestCorners) + abs(-2.0 * lumaM + lumaSN) * 2.0 + abs(-2.0 * lumaE + lumaEastCorners);
    float edgeVertical = abs(-2.0 * lumaN + lumaNorthCorners) + abs(-2.0 * lumaM + lumaWE) * 2.0 + abs(-2.0 * lumaS + lumaSouthCorners);
    bool horizontal = edgeHorizontal >= edgeVertical;

    // Which side of the pixel the edge is on, and the luma halfway across it.
    float luma1 = horizontal ? lumaS : lumaW;
    float luma2 = horizontal ? lumaN : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool steepest1 = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = horizontal ? texelSize.y : texelSize.x;
    float lumaLocalAverage;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }

    vec2 edgeUv = texuv;
    if (horizontal)
        edgeUv.y += stepLength * 0.5;
    else
        edgeUv.x += stepLength * 0.5;

    // Walk along the edge both ways until the luma leaves the edge's average.
    vec2 offset = horizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    vec2 uv1 = edgeUv - offset;
    vec2 uv2 = edgeUv + offset;
    float lumaEnd1 = LumaAt(uv1) - lumaLocalAverage;
    float lumaEnd2 = LumaAt(uv2) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    for (int step = 1; step < SEARCH_STEPS && !(reached1 && reached2); step++) {
        if (!reached1) {
            uv1 -= offset * SearchStep(step);
            lumaEnd1 = LumaAt(uv1) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += offset * SearchStep(step);
            lumaEnd2 = LumaAt(uv2) - lumaLocalAverage;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }

    float distance1 = horizontal ? texuv.x - uv1.x : texuv.y - uv1.y;
    float distance2 = horizontal ? uv2.x - texuv.x : uv2.y - texuv.y;
    bool direction1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeLength = distance1 + distance2;
    float pixelOffset = -distanceFinal / edgeLength + 0.5;

    // Only blend when the luma at the nearer end varies the same way as the centre does across the edge.
    bool centreSmaller = lumaM < lumaLocalAverage;
    bool correctVariation = ((direction1 ? lumaEnd1 : lumaEnd2) < 0.0) != centreSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Thin features that the edge search misses get a blend from the contrast with the 3x3 average.
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaSN + lumaWE) + lumaWestCorners + lumaEastCorners);
    float subPixel1 = clamp(abs(lumaAverage - lumaM) / range, 0.0, 1.0);
    float subPixel2 = (-2.0 * subPixel1 + 3.0) * subPixel1 * subPixel1;
    finalOffset = max(finalOffset, subPixel2 * subPixel2 * SUBPIXEL_QUALITY);

    vec2 finalUv = texuv;
    if (horizontal)
        finalUv.y += finalOffset * stepLength;
    else
        finalUv.x += finalOffset * stepLength;
    FragColour = vec4(texture(source, finalUv).rgb, centre.a);
}
