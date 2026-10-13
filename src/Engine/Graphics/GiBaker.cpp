#include "GiBaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <numbers>
#include <span>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Engine/AssetsManager.h"
#include "Engine/Engine.h"
#include "Engine/Data/TileData.h"
#include "Engine/Data/TileEnums.h"
#include "Engine/Graphics/FaceEnums.h"
#include "Engine/Graphics/Image.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/Lighting.h"
#include "Engine/Graphics/LightsStack.h"
#include "Engine/Graphics/LocationFunctions.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/Sprites.h"
#include "Engine/Objects/Decoration.h"
#include "Engine/Tables/DecorationTable.h"

#include "Application/GameConfig.h"

static constexpr int GI_RAYS_PER_PROBE = 256;
static constexpr float GI_INDOOR_SPACING = 128.0f; // World units between indoor probes, grown until the grid fits.
static constexpr int GI_INDOOR_MAX_PROBES = 65536;
static constexpr float GI_OUTDOOR_SPACING = 1024.0f;
static constexpr int GI_OUTDOOR_GRID = 64; // Probes along x and y, covering the 128 terrain cells of 512 units.
static constexpr std::array<float, 3> GI_OUTDOOR_LAYERS = {64.0f, 256.0f, 768.0f};
static constexpr std::array<float, 7> GI_OUTDOOR_HOURS = {0.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 21.0f};
static constexpr float GI_RAY_LENGTH = 16384.0f;
static constexpr float GI_SURFACE_OFFSET = 1.0f; // How far off a face its light is looked up, against self-shadowing.
static constexpr float GI_MIN_HIT_DISTANCE = 1e-3f;
static constexpr int GI_DILATION_PASSES = 8;
static constexpr int GI_MAX_BACK_FACES = GI_RAYS_PER_PROBE / 4; // Probes that see more backs of faces than this are outside the level.
static constexpr int GI_MAX_INDOOR_MISSES = GI_RAYS_PER_PROBE / 2; // Indoor probes that see out of the level this often are outside it.
static constexpr float SH_Y0 = 0.282095f; // Constant of the L0 spherical harmonic.
static constexpr float SH_Y1 = 0.488603f; // Constant of the L1 spherical harmonics.

using GiProbeCoefficients = std::array<float, GiProbes::COEFFICIENTS>;

/**
 * Triangle of a face, as a corner and the two edges from it, with the face's normal and colour.
 */
struct BakeTriangle {
    Vec3f v0;
    Vec3f e1;
    Vec3f e2;
    Vec3f normal;
    Vec3f albedo; // Linear.
};

/**
 * Point light of a level.
 */
struct BakeLight {
    Vec3f position;
    float radius = 0;
    Vec3f colour; // Linear.
};

/**
 * Light that every face of a level gets at one time key, as the shaders light the world with it.
 */
struct BakeSky {
    Vec3f sunDirection; // Towards the sun.
    float sun = 0; // Brightness of the sunlight, zero at night and indoors.
    float ambient = 0; // Brightness of the light that reaches faces in shadow too.
};

struct BakeBox {
    Vec3f min = Vec3f(1e30f, 1e30f, 1e30f);
    Vec3f max = Vec3f(-1e30f, -1e30f, -1e30f);

    void grow(Vec3f p) {
        min = Vec3f(std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z));
        max = Vec3f(std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z));
    }
};

/**
 * Node of the hierarchy in `BakeScene`.
 */
struct BakeNode {
    BakeBox box;
    int first = 0; // First entry of `BakeScene::_order` for a leaf, the left child otherwise.
    int right = 0; // The right child of an inner node.
    int count = 0; // Triangles in a leaf, 0 for an inner node.
};

/**
 * Triangles of a level in a bounding volume hierarchy, for tracing rays against.
 */
class BakeScene {
 public:
    explicit BakeScene(std::vector<BakeTriangle> triangles) : _triangles(std::move(triangles)) {
        _order.resize(_triangles.size());
        for (size_t i = 0; i < _order.size(); i++)
            _order[i] = static_cast<int>(i);
        if (!_triangles.empty())
            build(0, static_cast<int>(_triangles.size()));
    }

    /**
     * @param origin                    Start of the ray.
     * @param dir                       Unit direction of the ray.
     * @param maxT                      Length of the ray.
     * @param[out] hitT                 Distance to the hit.
     * @return                          Index of the closest triangle the ray hits, or -1.
     */
    int intersect(Vec3f origin, Vec3f dir, float maxT, float *hitT) const {
        int result = -1;
        *hitT = maxT;
        traverse(origin, dir, hitT, [&](int triangle, float t) {
            *hitT = t;
            result = triangle;
            return false;
        });
        return result;
    }

    [[nodiscard]] bool occluded(Vec3f origin, Vec3f dir, float maxT) const {
        bool result = false;
        traverse(origin, dir, &maxT, [&](int, float) {
            result = true;
            return true;
        });
        return result;
    }

    [[nodiscard]] const BakeTriangle &triangle(int index) const {
        return _triangles[index];
    }

 private:
    int build(int first, int count) {
        int index = static_cast<int>(_nodes.size());
        _nodes.emplace_back();
        BakeBox box, centres;
        for (int i = first; i < first + count; i++) {
            const BakeTriangle &t = _triangles[_order[i]];
            box.grow(t.v0);
            box.grow(t.v0 + t.e1);
            box.grow(t.v0 + t.e2);
            centres.grow(centre(t));
        }
        _nodes[index].box = box;
        if (count <= 4) {
            _nodes[index].first = first;
            _nodes[index].count = count;
            return index;
        }

        Vec3f extent = centres.max - centres.min;
        int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
        int middle = first + count / 2;
        std::nth_element(_order.begin() + first, _order.begin() + middle, _order.begin() + first + count, [&](int a, int b) {
            return component(centre(_triangles[a]), axis) < component(centre(_triangles[b]), axis);
        });
        int left = build(first, middle - first);
        int right = build(middle, first + count - middle);
        _nodes[index].first = left;
        _nodes[index].right = right;
        return index;
    }

    static Vec3f centre(const BakeTriangle &t) {
        return t.v0 + (t.e1 + t.e2) / 3.0f;
    }

    static float component(Vec3f v, int axis) {
        return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
    }

    static bool hitsBox(const BakeBox &box, Vec3f origin, Vec3f invDir, float maxT) {
        float t0 = 0, t1 = maxT;
        for (int axis = 0; axis < 3; axis++) {
            float lo = (component(box.min, axis) - component(origin, axis)) * component(invDir, axis);
            float hi = (component(box.max, axis) - component(origin, axis)) * component(invDir, axis);
            if (lo > hi)
                std::swap(lo, hi);
            t0 = std::max(t0, lo);
            t1 = std::min(t1, hi);
            if (t0 > t1)
                return false;
        }
        return true;
    }

    /**
     * Möller-Trumbore, both sides of the face.
     *
     * @param t                         Triangle to test.
     * @param origin                    Start of the ray.
     * @param dir                       Direction of the ray.
     * @param[out] hitT                 Distance to the hit.
     * @return                          Whether the ray hits the triangle.
     */
    static bool hitsTriangle(const BakeTriangle &t, Vec3f origin, Vec3f dir, float *hitT) {
        Vec3f p = cross(dir, t.e2);
        float det = dot(t.e1, p);
        if (std::abs(det) < 1e-8f)
            return false;
        float inv = 1.0f / det;
        Vec3f s = origin - t.v0;
        float u = dot(s, p) * inv;
        if (u < 0 || u > 1)
            return false;
        Vec3f q = cross(s, t.e1);
        float v = dot(dir, q) * inv;
        if (v < 0 || u + v > 1)
            return false;
        *hitT = dot(t.e2, q) * inv;
        return *hitT > GI_MIN_HIT_DISTANCE;
    }

    /**
     * @param origin                    Start of the ray.
     * @param dir                       Direction of the ray.
     * @param limit                     Length of the ray, hits past it are skipped. The callback may shorten it.
     * @param callback                  Called as `callback(triangle, t)` for each hit, returns true to stop.
     */
    template<class Callback>
    void traverse(Vec3f origin, Vec3f dir, float *limit, Callback &&callback) const {
        if (_nodes.empty())
            return;
        auto inverse = [](float value) { return 1.0f / (value != 0 ? value : 1e-12f); };
        Vec3f invDir(inverse(dir.x), inverse(dir.y), inverse(dir.z));
        std::array<int, 64> stack;
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const BakeNode &node = _nodes[stack[--top]];
            if (!hitsBox(node.box, origin, invDir, *limit))
                continue;
            if (node.count == 0) {
                stack[top++] = node.right;
                stack[top++] = node.first;
                continue;
            }
            for (int i = node.first; i < node.first + node.count; i++) {
                float t = 0;
                if (hitsTriangle(_triangles[_order[i]], origin, dir, &t) && t < *limit && callback(_order[i], t))
                    return;
            }
        }
    }

    std::vector<BakeTriangle> _triangles;
    std::vector<int> _order;
    std::vector<BakeNode> _nodes;
};

static float srgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

static Vec3f linearColour(Color colour) {
    return Vec3f(srgbToLinear(colour.r / 255.0f), srgbToLinear(colour.g / 255.0f), srgbToLinear(colour.b / 255.0f));
}

using BakeAlbedos = std::unordered_map<GraphicsImage *, Vec3f>;

/**
 * @param texture                       Texture of a face, or null.
 * @param cache                         Colours of the textures already looked at.
 * @return                              Mean linear colour of its opaque texels.
 */
static Vec3f textureAlbedo(GraphicsImage *texture, BakeAlbedos *cache) {
    Vec3f fallback(0.5f, 0.5f, 0.5f);
    if (!texture)
        return fallback;
    if (auto pos = cache->find(texture); pos != cache->end())
        return pos->second;

    std::span<const Color> pixels = texture->rgba().pixels();
    size_t step = std::max<size_t>(1, pixels.size() / 4096);
    Vec3f sum(0, 0, 0);
    int count = 0;
    for (size_t i = 0; i < pixels.size(); i += step) {
        if (pixels[i].a == 0)
            continue;
        sum += linearColour(pixels[i]);
        count++;
    }
    Vec3f result = count ? sum / static_cast<float>(count) : fallback;
    cache->emplace(texture, result);
    return result;
}

static void addFace(std::vector<BakeTriangle> *triangles, const BLVFace &face, const std::vector<Vec3f> &vertices, BakeAlbedos *albedos) {
    Vec3f albedo = textureAlbedo(face.GetTexture(), albedos);
    for (size_t i = 1; i + 1 < face.vertexIds.size(); i++) {
        Vec3f a = vertices[face.vertexIds[0]];
        Vec3f b = vertices[face.vertexIds[i]];
        Vec3f c = vertices[face.vertexIds[i + 1]];
        triangles->push_back({a, b - a, c - a, face.facePlane.normal, albedo});
    }
}

/**
 * @param face                          Face of a level.
 * @return                              Whether the face reflects light in the bake. Portals and openings to the sky
 *                                      don't.
 */
static bool bakesFace(const BLVFace &face) {
    return !face.Invisible() && !face.isPortal() && !(face.attributes & FACE_INDOOR_SKY) && face.vertexIds.size() >= 3;
}

static std::vector<BakeTriangle> indoorTriangles() {
    std::unordered_set<int> doorFaces;
    for (const BLVDoor &door : pIndoor->doors)
        for (int i = 0; i < door.numFaces; i++)
            doorFaces.insert(door.pFaceIDs[i]);

    std::vector<BakeTriangle> result;
    BakeAlbedos albedos;
    for (size_t i = 0; i < pIndoor->faces.size(); i++)
        if (bakesFace(pIndoor->faces[i]) && !doorFaces.contains(static_cast<int>(i)))
            addFace(&result, pIndoor->faces[i], pIndoor->vertices, &albedos);
    return result;
}

static std::vector<BakeTriangle> outdoorTriangles() {
    std::vector<BakeTriangle> result;
    BakeAlbedos albedos;
    for (int y = 0; y < 127; y++) {
        for (int x = 0; x < 127; x++) {
            const TileData &tile = pOutdoor->pTerrain.tileDataByGrid({x, y});
            Vec3f albedo = textureAlbedo(assets->getBitmap(tile.textureName, static_cast<bool>(tile.flags & TILE_GENERATED_TRANSITION)), &albedos);
            Vec3f v00 = pOutdoor->pTerrain.vertexByGridUnsafe({x, y}).toFloat();
            Vec3f v10 = pOutdoor->pTerrain.vertexByGridUnsafe({x + 1, y}).toFloat();
            Vec3f v01 = pOutdoor->pTerrain.vertexByGridUnsafe({x, y + 1}).toFloat();
            Vec3f v11 = pOutdoor->pTerrain.vertexByGridUnsafe({x + 1, y + 1}).toFloat();
            for (auto [a, b, c] : {std::tuple(v00, v11, v10), std::tuple(v00, v01, v11)}) {
                Vec3f normal = cross(b - a, c - a);
                float length = normal.length();
                if (length == 0)
                    continue;
                normal /= length;
                result.push_back({a, b - a, c - a, normal.z < 0 ? -normal : normal, albedo});
            }
        }
    }
    for (const BSPModel &model : pOutdoor->pBModels)
        for (int faceId : model.faces)
            if (bakesFace(pOutdoor->faces[faceId]))
                addFace(&result, pOutdoor->faces[faceId], pOutdoor->vertices, &albedos);
    return result;
}

static std::vector<BakeLight> indoorLights() {
    std::vector<BakeLight> result;
    for (unsigned i = 0; i < pStationaryLightsStack->uNumLightsActive; i++) {
        const StationaryLight &light = pStationaryLightsStack->pLights[i];
        result.push_back({light.vPosition, static_cast<float>(light.uRadius), linearColour(light.uLightColor)});
    }
    return result;
}

/**
 * @return                              Lights of the decorations, as the renderer adds them outdoors every frame from the
 *                                      glow of their sprites.
 */
static std::vector<BakeLight> outdoorLights() {
    std::vector<BakeLight> result;
    for (const LevelDecoration &decoration : pLevelDecorations) {
        const DecorationData *data = pDecorationTable->decoration(decoration.uDecorationDescID);
        if ((decoration.uFlags & LEVEL_DECORATION_INVISIBLE) || (data->uFlags & (DECORATION_DATA_EMITS_FIRE | DECORATION_DATA_MARKER | DECORATION_DATA_DONT_DRAW)))
            continue;
        SpriteFrame *frame = pSpriteFrameTable->GetFrame(data->uSpriteID, Duration());
        if (!frame || !frame->glowRadius)
            continue;
        Color colour = engine->config->graphics.DefaultLightColor.value();
        if (engine->config->graphics.ColoredLights.value() && data->uColoredLight != Color(0, 0, 0))
            colour = data->uColoredLight;
        Vec3f position = decoration.vPosition + Vec3f(0, 0, data->uDecorationHeight / 2);
        result.push_back({position, static_cast<float>(frame->glowRadius), linearColour(colour)});
    }
    return result;
}

static BakeSky outdoorSky(float hour) {
    float minutes = hour * 60.0f;
    BakeSky result;
    result.sunDirection = Vec3f(0, 0, 1);
    result.ambient = 0.15f + (std::sin((minutes - 360.0f) * 2 * std::numbers::pi_v<float> / 1440) + 1) * 0.27f;
    float sinceRise = minutes - 300.0f; // The sun is up from 05:00 to 21:00.
    if (sinceRise >= 0 && sinceRise < 960) {
        float angle = sinceRise * std::numbers::pi_v<float> / 960.0f;
        result.sunDirection = Vec3f(std::cos(angle), 0, std::sin(angle));
        result.sun = result.ambient + 0.3f;
    }
    return result;
}

static BakeSky indoorSky() {
    BakeSky result;
    result.ambient = (248.0f - pIndoor->ambientLightLevel() * 8) / 255.0f;
    return result;
}

/**
 * @param index                         Index of a sample.
 * @param count                         Number of samples.
 * @return                              Unit direction of the sample, spread evenly over the sphere.
 */
static Vec3f fibonacciDirection(int index, int count) {
    float z = 1.0f - (index + 0.5f) * 2.0f / count;
    float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float phi = index * 2.39996323f; // The golden angle.
    return Vec3f(r * std::cos(phi), r * std::sin(phi), z);
}

/**
 * @param scene                         Level to trace shadows in.
 * @param lights                        Point lights of the level.
 * @param point                         Point on a face.
 * @param normal                        Normal of the face.
 * @return                              Light that reaches the point from the lights, attenuated as the shaders do it.
 */
static Vec3f directIrradiance(const BakeScene &scene, const std::vector<BakeLight> &lights, Vec3f point, Vec3f normal) {
    Vec3f result(0, 0, 0);
    for (const BakeLight &light : lights) {
        Vec3f toLight = light.position - point;
        float distance = toLight.length();
        if (distance >= light.radius || distance < GI_MIN_HIT_DISTANCE)
            continue;
        toLight /= distance;
        float facing = dot(normal, toLight);
        if (facing <= 0)
            continue;
        float attenuation = 1.0f - distance * distance / (light.radius * light.radius);
        if (!scene.occluded(point, toLight, distance))
            result += light.colour * (attenuation * attenuation * facing);
    }
    return result;
}

/**
 * Light that reaches one probe.
 */
struct BakedProbe {
    std::vector<GiProbeCoefficients> keys; // Coefficients for each time key.
    bool valid = false; // Whether the probe is inside the level.
};

/**
 * @param scene                         Level to trace.
 * @param lights                        Point lights of the level.
 * @param skies                         Sky of each time key.
 * @param position                      Position of the probe.
 * @param maxMisses                     Rays that may leave the level before the probe counts as outside it.
 * @param seed                          Number of the probe, turns its rays against banding between probes.
 * @return                              L1 coefficients of the irradiance at the probe from light bounced once off the
 *                                      level, for each time key. Zero for a probe outside the level.
 */
static BakedProbe bakeProbe(const BakeScene &scene, const std::vector<BakeLight> &lights, const std::vector<BakeSky> &skies, Vec3f position,
                            int maxMisses, int seed) {
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr std::array<float, 4> convolution = {pi, 2 * pi / 3, 2 * pi / 3, 2 * pi / 3}; // Turns radiance into irradiance.
    constexpr float weight = 4 * pi / GI_RAYS_PER_PROBE;

    struct Hit {
        Vec3f dir;
        float t = 0;
        int triangle = -1;
    };
    std::array<Hit, GI_RAYS_PER_PROBE> hits;
    float spin = seed * 0.618034f * 2 * pi;
    float cs = std::cos(spin), sn = std::sin(spin);
    int backFaces = 0;
    int misses = 0;
    for (int i = 0; i < GI_RAYS_PER_PROBE; i++) {
        Vec3f d = fibonacciDirection(i, GI_RAYS_PER_PROBE);
        hits[i].dir = Vec3f(d.x * cs - d.y * sn, d.x * sn + d.y * cs, d.z);
        hits[i].triangle = scene.intersect(position, hits[i].dir, GI_RAY_LENGTH, &hits[i].t);
        if (hits[i].triangle < 0) {
            misses++;
        } else if (dot(scene.triangle(hits[i].triangle).normal, hits[i].dir) > 0) {
            backFaces++;
        }
    }

    BakedProbe result;
    result.keys.assign(skies.size(), GiProbeCoefficients{});
    result.valid = backFaces <= GI_MAX_BACK_FACES && misses <= maxMisses;
    if (!result.valid)
        return result;

    for (const Hit &hit : hits) {
        if (hit.triangle < 0)
            continue;
        const BakeTriangle &triangle = scene.triangle(hit.triangle);
        if (dot(triangle.normal, hit.dir) > 0)
            continue;
        Vec3f point = position + hit.dir * (hit.t - GI_SURFACE_OFFSET);
        Vec3f lit = directIrradiance(scene, lights, point, triangle.normal);
        std::array<float, 4> basis = {SH_Y0, SH_Y1 * hit.dir.x, SH_Y1 * hit.dir.y, SH_Y1 * hit.dir.z};

        for (size_t key = 0; key < skies.size(); key++) {
            const BakeSky &sky = skies[key];
            float light = sky.ambient;
            float facing = dot(triangle.normal, sky.sunDirection);
            if (sky.sun > 0 && facing > 0 && !scene.occluded(point, sky.sunDirection, GI_RAY_LENGTH))
                light += sky.sun * facing;
            Vec3f irradiance = lit + Vec3f(light, light, light);
            Vec3f radiance = Vec3f(triangle.albedo.x * irradiance.x, triangle.albedo.y * irradiance.y, triangle.albedo.z * irradiance.z) / pi;
            for (int channel = 0; channel < 3; channel++) {
                float value = channel == 0 ? radiance.x : channel == 1 ? radiance.y : radiance.z;
                for (int k = 0; k < 4; k++)
                    result.keys[key][channel * 4 + k] += value * basis[k] * weight * convolution[k];
            }
        }
    }
    return result;
}

/**
 * Probes of a level, for each time key.
 */
struct BakedProbes {
    std::vector<std::vector<GiProbeCoefficients>> keys; // For each time key, the coefficients of each probe.
    std::vector<bool> valid; // For each probe, whether it is inside the level.
};

/**
 * Bakes probes on every thread.
 *
 * @param scene                         Level to trace.
 * @param lights                        Point lights of the level.
 * @param skies                         Sky of each time key.
 * @param positions                     Position of each probe.
 * @param maxMisses                     Rays that may leave the level before a probe counts as outside it.
 * @return                              The probes.
 */
static BakedProbes bakeProbes(const BakeScene &scene, const std::vector<BakeLight> &lights, const std::vector<BakeSky> &skies,
                              const std::vector<Vec3f> &positions, int maxMisses) {
    BakedProbes result;
    result.keys.assign(skies.size(), std::vector<GiProbeCoefficients>(positions.size(), GiProbeCoefficients{}));
    std::vector<char> valid(positions.size(), 0); // Not `std::vector<bool>`, threads write neighbouring entries.
    std::atomic<size_t> next = 0;
    auto work = [&] {
        for (size_t i = next++; i < positions.size(); i = next++) {
            BakedProbe probe = bakeProbe(scene, lights, skies, positions[i], maxMisses, static_cast<int>(i));
            valid[i] = probe.valid;
            for (size_t key = 0; key < skies.size(); key++)
                result.keys[key][i] = probe.keys[key];
        }
    };
    std::vector<std::thread> threads;
    unsigned count = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 1; i < count; i++)
        threads.emplace_back(work);
    work();
    for (std::thread &thread : threads)
        thread.join();
    result.valid.assign(valid.begin(), valid.end());
    return result;
}

/**
 * Fills probes outside the level from their neighbours, so that blending probes next to a wall doesn't pull in black.
 *
 * @param probes                        Coefficients of each probe.
 * @param valid                         Whether each probe is inside the level.
 * @param dims                          Probes along x, y and z.
 */
static void dilate(std::vector<GiProbeCoefficients> *probes, std::vector<bool> valid, Vec3i dims) {
    auto index = [&](int x, int y, int z) { return (static_cast<size_t>(z) * dims.y + y) * dims.x + x; };
    for (int pass = 0; pass < GI_DILATION_PASSES; pass++) {
        std::vector<bool> next = valid;
        for (int z = 0; z < dims.z; z++) {
            for (int y = 0; y < dims.y; y++) {
                for (int x = 0; x < dims.x; x++) {
                    size_t i = index(x, y, z);
                    if (valid[i])
                        continue;
                    GiProbeCoefficients sum = {};
                    int count = 0;
                    for (auto [dx, dy, dz] : {std::tuple(-1, 0, 0), std::tuple(1, 0, 0), std::tuple(0, -1, 0), std::tuple(0, 1, 0),
                                              std::tuple(0, 0, -1), std::tuple(0, 0, 1)}) {
                        int nx = x + dx, ny = y + dy, nz = z + dz;
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= dims.x || ny >= dims.y || nz >= dims.z || !valid[index(nx, ny, nz)])
                            continue;
                        for (int k = 0; k < GiProbes::COEFFICIENTS; k++)
                            sum[k] += (*probes)[index(nx, ny, nz)][k];
                        count++;
                    }
                    if (count == 0)
                        continue;
                    for (int k = 0; k < GiProbes::COEFFICIENTS; k++)
                        (*probes)[i][k] = sum[k] / count;
                    next[i] = true;
                }
            }
        }
        valid = std::move(next);
    }
}

/**
 * Appends a time key to `result`, with the average irradiance over the probes inside the level.
 *
 * @param result                        Probes to append to.
 * @param hour                          Time of day of the key.
 * @param probes                        Coefficients of each probe at that time.
 * @param valid                         Whether each probe is inside the level.
 */
static void appendKey(GiProbes *result, float hour, const std::vector<GiProbeCoefficients> &probes, const std::vector<bool> &valid) {
    Vec3f sum(0, 0, 0);
    int count = 0;
    for (size_t i = 0; i < probes.size(); i++) {
        for (float value : probes[i])
            result->coefficients.push_back(floatToHalf(value));
        if (valid[i]) {
            sum += Vec3f(probes[i][0], probes[i][4], probes[i][8]) * SH_Y0; // Irradiance averaged over all normals.
            count++;
        }
    }
    result->timeHours.push_back(hour);
    result->averageIrradiance.push_back(count ? sum / static_cast<float>(count) : Vec3f(0, 0, 0));
}

/**
 * Dilates each time key of `probes` and appends it to `result`.
 *
 * @param result                        Probes to append to.
 * @param hours                         Time of day of each key.
 * @param probes                        Baked probes, with a key for each of `hours`.
 */
static void appendKeys(GiProbes *result, std::span<const float> hours, BakedProbes probes) {
    for (size_t key = 0; key < hours.size(); key++) {
        dilate(&probes.keys[key], probes.valid, result->dims);
        appendKey(result, hours[key], probes.keys[key], probes.valid);
    }
}

static GiProbes bakeIndoor() {
    BakeScene scene(indoorTriangles());

    BakeBox bounds;
    for (size_t i = 1; i < pIndoor->sectors.size(); i++) { // Sector 0 is a placeholder.
        const BBoxf &box = pIndoor->sectors[i].boundingBox;
        bounds.grow(Vec3f(box.x1, box.y1, box.z1));
        bounds.grow(Vec3f(box.x2, box.y2, box.z2));
    }
    Vec3f extent = bounds.max - bounds.min;
    float spacing = GI_INDOOR_SPACING;
    Vec3i dims;
    while (true) {
        auto cells = [&](float length) { return std::max(1, static_cast<int>(std::ceil(length / spacing))); };
        dims = Vec3i(cells(extent.x), cells(extent.y), cells(extent.z));
        if (dims.x * dims.y * dims.z <= GI_INDOOR_MAX_PROBES)
            break;
        spacing *= 1.1f;
    }

    GiProbes result;
    result.layout = GI_PROBES_GRID;
    result.origin = bounds.min + Vec3f(spacing, spacing, spacing) / 2; // Probes sit at cell centres, off the floors.
    result.spacing = Vec3f(spacing, spacing, spacing);
    result.dims = dims;

    std::vector<Vec3f> positions;
    for (int z = 0; z < dims.z; z++)
        for (int y = 0; y < dims.y; y++)
            for (int x = 0; x < dims.x; x++)
                positions.push_back(result.origin + Vec3f(x, y, z) * spacing);

    std::array<float, 1> hours = {0.0f};
    appendKeys(&result, hours, bakeProbes(scene, indoorLights(), {indoorSky()}, positions, GI_MAX_INDOOR_MISSES));
    return result;
}

static GiProbes bakeOutdoor() {
    BakeScene scene(outdoorTriangles());

    GiProbes result;
    result.layout = GI_PROBES_TERRAIN_LAYERS;
    float start = (1 - GI_OUTDOOR_GRID) * GI_OUTDOOR_SPACING / 2; // Centred on the map.
    result.origin = Vec3f(start, start, 0);
    result.spacing = Vec3f(GI_OUTDOOR_SPACING, GI_OUTDOOR_SPACING, 0);
    result.dims = Vec3i(GI_OUTDOOR_GRID, GI_OUTDOOR_GRID, static_cast<int>(GI_OUTDOOR_LAYERS.size()));
    result.layerHeights.assign(GI_OUTDOOR_LAYERS.begin(), GI_OUTDOOR_LAYERS.end());

    std::vector<Vec3f> positions;
    for (int z = 0; z < result.dims.z; z++) {
        for (int y = 0; y < result.dims.y; y++) {
            for (int x = 0; x < result.dims.x; x++) {
                Vec3f position = result.origin + Vec3f(x, y, 0) * GI_OUTDOOR_SPACING;
                position.z = pOutdoor->pTerrain.heightByPos(position) + GI_OUTDOOR_LAYERS[z];
                positions.push_back(position);
            }
        }
    }

    std::vector<BakeSky> skies;
    for (float hour : GI_OUTDOOR_HOURS)
        skies.push_back(outdoorSky(hour));
    appendKeys(&result, GI_OUTDOOR_HOURS, bakeProbes(scene, outdoorLights(), skies, positions, GI_RAYS_PER_PROBE));
    return result;
}

GiProbes bakeGiProbes() {
    return uCurrentlyLoadedLevelType == LEVEL_INDOOR ? bakeIndoor() : bakeOutdoor();
}
