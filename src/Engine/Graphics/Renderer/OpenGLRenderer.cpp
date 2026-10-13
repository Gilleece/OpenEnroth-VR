#include "OpenGLRenderer.h"

#include "OpenGLVertexBuffer.h"

#include <array>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <map>
#include <string>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include <glad/gl.h> // NOLINT: not a C system header.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui/backends/imgui_impl_opengl3.h> // NOLINT: not a C system header.
#include <imgui/backends/imgui_impl_sdl3.h> // NOLINT: not a C system header.

#include "Engine/Engine.h"
#include "Engine/Resources/EngineFileSystem.h"
#include "Engine/EngineGlobals.h"
#include "Engine/Graphics/BspRenderer.h"
#include "Engine/Graphics/Image.h"
#include "Engine/Graphics/Lighting.h"
#include "Engine/Graphics/DecalBuilder.h"
#include "Engine/Objects/Actor.h"
#include "Engine/Objects/Decoration.h"
#include "Engine/Graphics/LightsStack.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/Sprites.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/ParticleEngine.h"
#include "Engine/Tables/MapTable.h"
#include "Engine/Tables/TextureFrameTable.h"
#include "Engine/Graphics/Viewport.h"
#include "Engine/Graphics/Vis.h"
#include "Engine/Graphics/Weather.h"
#include "Engine/Graphics/WorldFrame.h"
#include "Engine/Tables/TileTable.h"
#include "Engine/Party.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/SpellFxRenderer.h"
#include "Engine/AssetsManager.h"
#include "Engine/EngineCallObserver.h"

#include "Library/FileSystem/Sub/SubFileSystem.h"
#include "Library/Platform/Application/PlatformApplication.h"
#include "Library/Serialization/EnumSerialization.h"
#include "Library/Color/Colorf.h"
#include "Library/Logger/Logger.h"
#include "Library/Geometry/Size.h"
#include "Library/Geometry/Vec.h"
#include "Library/Image/ImageFunctions.h"

#include "Utility/Exception.h"
#include "Utility/String/Format.h"

#include "OpenGLShader.h"

#ifndef LOWORD
    #define LOWORD(l) ((unsigned short)(((std::uintptr_t)(l)) & 0xFFFF))
#endif

static constexpr GLint PALETTE_TEXTURE_UNIT = 1;
static constexpr GLint MATERIAL_TEXTURE_UNIT = 2; // The world shaders sample units 0 and 1 only.
static constexpr int TERRAIN_TILE_TEXTURE_UNIT = 1; // The terrain shader samples tiles from unit 1 and water from unit 0.
static constexpr GLint BLOOM_TEXTURE_UNIT = 3; // Not used by any other shader.
static constexpr GLint CROSSFADE_TEXTURE_UNIT = 5; // Second side of a billboard's sprite.
static constexpr GLint SPRITE_SHAPE_TEXTURE_UNIT = 6;
static constexpr GLint SCENE_DEPTH_TEXTURE_UNIT = 4; // The billboards sample units 0 and 1 only, and 8 in the vertex shader.
static constexpr float SOFT_PARTICLE_DISTANCE = 64.0f; // World units in front of the level over which additive billboards fade out.
static constexpr GLint SUN_SHADOW_TEXTURE_UNIT = 8; // The terrain pass binds its arrays to units 0 to 7.
static constexpr GLint TORCH_SHADOW_TEXTURE_UNIT = 16; // The indoor pass binds its arrays to units 0 to 15.
static constexpr GLint AMBIENT_OCCLUSION_TEXTURE_UNIT = 17;
static constexpr GLint GI_PROBES_TEXTURE_UNIT = 18;
static constexpr GLint GI_TERRAIN_HEIGHTS_TEXTURE_UNIT = 19;
static constexpr int GI_MAX_LAYERS = 8; // As `gi.glsl` declares it.
static constexpr int GI_BLEND_STEPS = 32; // Steps between two time keys, a few game minutes each.
static constexpr int TORCH_SHADOW_SIZE = 512;
static constexpr float TORCH_SHADOW_NEAR = 4.0f;
static constexpr float SUN_SHADOW_SPLIT_LAMBDA = 0.75f; // Blend of logarithmic and uniform cascade splits.
static constexpr float SUN_SHADOW_EXTRUSION = 16384.0f; // How far towards the sun the cascades reach past what they cover, for the casters.
static constexpr float SUN_SHADOW_FADE_ELEVATION = 8.0f; // Sun elevation in degrees below which shadows fade out.
static constexpr float SUN_SHADOW_STEP_HEIGHT = 500.0f; // Height of an object whose shadow the sun's steps move by one texel at a time.
static constexpr float CONTACT_SHADOW_STRENGTH = 0.7f; // Darkening of the linear colour at the middle of the ellipse in full daylight.
static constexpr float CONTACT_SHADOW_SUNLIT_SCALE = 0.6f; // Part of the strength left when the sun casts shadows.
static constexpr float CONTACT_SHADOW_MIN_SIZE = 24.0f; // World units across the ellipse's long axis.
static constexpr float CONTACT_SHADOW_MAX_SIZE = 160.0f;
static constexpr float CONTACT_SHADOW_FULL_HEIGHT = 16.0f; // Height above the floor up to which a sprite casts the full shadow.
static constexpr float CONTACT_SHADOW_NO_HEIGHT = 96.0f; // Height above the floor from which it casts none.
static constexpr float CONTACT_SHADOW_PROBE_HEIGHT = 40.0f; // How far above a point the floor under it is looked for.
static constexpr float CONTACT_SHADOW_LIFT = 6.0f; // World units above the floor, on top of the polygon offset.
static constexpr GLint WATER_NORMALS_TEXTURE_UNIT = 9; // After the terrain arrays, the material array and the sun shadow map.
static constexpr int WATER_NORMALS_SIZE = 256;
static constexpr float INDOOR_WATER_REFLECTION = 0.25f; // Brightness of the walls in the ambient light, which indoor water reflects.
static constexpr float FOG_HEIGHT_FALLOFF = 1.0f / 1536.0f; // The fog thins by a factor of e every 1536 units above the eye.
static constexpr float FOG_INSCATTER = 0.8f; // How much brighter the fog gets looking straight at the sun.
static constexpr float SUN_SHADOW_SLOPE_BIAS = 2.0f;
static constexpr float SUN_SHADOW_CONSTANT_BIAS = 4.0f;
static constexpr int BLOOM_MAX_LEVELS = 6;
static constexpr int BLOOM_MIN_LEVEL_SIZE = 8; // In pixels, on the shorter side.
static constexpr float LINEAR_ADDITIVE_GAIN = 2.0f; // Additive billboards were made to add up in sRGB, where the sum is brighter than in linear.
static constexpr int PALETTE_COUNT = 1000; // pal000 to pal999 in bitmaps.lod.


static constexpr GLenum TEXTURE_MAX_ANISOTROPY = 0x84FE; // From GL_ARB_texture_filter_anisotropic and GL_EXT_texture_filter_anisotropic.
static constexpr GLenum MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF; // From the same extensions.
static constexpr float ANISOTROPY_LIMIT = 16.0f;

// globals
//TODO(pskelton): Combine and contain
RenderBillboard pBillboardRenderList[500];
int uNumBillboardsToDraw;
int uNumDecorationsDrawnThisFrame;
int uNumSpritesDrawnThisFrame;
RenderVertexSoft array_73D150[20];
RenderVertexSoft VertexRenderList[50];

static bool OpenGLES = false;

namespace detail_gl_error {
MM_DEFINE_ENUM_SERIALIZATION_FUNCTIONS(GLenum, CASE_SENSITIVE, {
    { GL_INVALID_OPERATION, "INVALID_OPERATION" },
    { GL_INVALID_ENUM, "INVALID_ENUM" },
    { GL_INVALID_VALUE, "INVALID_VALUE" },
    { GL_OUT_OF_MEMORY, "OUT_OF_MEMORY" },
    { GL_INVALID_FRAMEBUFFER_OPERATION, "INVALID_FRAMEBUFFER_OPERATION" }
})
} // namespace detail_gl_error

// improved error check - using glad post call back
void GL_Check_Errors(void *ret, const char *name, GLADapiproc apiproc, int len_args, ...) {
    GLenum err = glad_glGetError();

    while (err != GL_NO_ERROR) {
        std::string error;
        if (!detail_gl_error::trySerialize(err, &error))
            error = "Unknown Error";

        MM_WARNING("OpenGL error ({}): {} from function {}", err, error, name);

        err = glad_glGetError();
    }
}

// sky billboard stuff

void SkyBillboardStruct::CalcSkyFrustumVec(int x1, int y1, int z1, int x2, int y2, int z2) {
    // 6 0 0 0 6 0

    // TODO(pskelton): clean up and move out of here

    float cosz = pCamera3D->_yawRotationCosine;
    float cosx = pCamera3D->_pitchRotationCosine;
    float sinz = pCamera3D->_yawRotationSine;
    float sinx = pCamera3D->_pitchRotationSine;

    // positions all minus ?
    float v11 = cosz * -pCamera3D->vCameraPos.x + sinz * -pCamera3D->vCameraPos.y;
    float v24 = cosz * -pCamera3D->vCameraPos.y - sinz * -pCamera3D->vCameraPos.x;

    // cam position transform
    if (pCamera3D->_viewPitch) {
        this->field_0_party_dir_x = (v11 * cosx) + (-pCamera3D->vCameraPos.z * sinx);
        this->field_4_party_dir_y = v24;
        this->field_8_party_dir_z = (-pCamera3D->vCameraPos.z * cosx) /*-*/ + (v11 * sinx);
    } else {
        this->field_0_party_dir_x = v11;
        this->field_4_party_dir_y = v24;
        this->field_8_party_dir_z = (-pCamera3D->vCameraPos.z);
    }

    // set 1 position transfrom (6 0 0) looks like cam left vector
    if (pCamera3D->_viewPitch) {
        float v17 = (x1 * cosz) + (y1 * sinz);

        this->CamVecLeft_Y = (v17 * cosx) + (z1 * sinx);  // dz
        this->CamVecLeft_X = (y1 * cosz) - (x1 * sinz);  // dx
        this->CamVecLeft_Z = (z1 * cosx) /*-*/ + (v17 * sinx);  // dy
    } else {
        this->CamVecLeft_Y = (x1 * cosz) + (y1 * sinz);  // dz
        this->CamVecLeft_X = (y1 * cosz) - (x1 * sinz);  // dx
        this->CamVecLeft_Z = static_cast<float>(z1);  // dy
    }

    // set 2 position transfrom (0 1 0) looks like cam front vector
    if (pCamera3D->_viewPitch) {
        float v19 = (x2 * cosz) + (y2 * sinz);

        this->CamVecFront_Y = (v19 * cosx) + (z2 * sinx);  // dz
        this->CamVecFront_X = (y2 * cosz) - (x2 * sinz);  // dx
        this->CamVecFront_Z = (z2 * cosx) /*-*/ + (v19 * sinx);  // dy
    } else {
        this->CamVecFront_Y = (x2 * cosz) + (y2 * sinz);  // dz
        this->CamVecFront_X = (y2 * cosz) - (x2 * sinz);  // dx
        this->CamVecFront_Z = static_cast<float>(z2);  // dy
    }

    this->CamLeftDot =
        (this->CamVecLeft_X * this->field_0_party_dir_x) +
        (this->CamVecLeft_Y * this->field_4_party_dir_y) +
        (this->CamVecLeft_Z * this->field_8_party_dir_z);
    this->CamFrontDot =
        (this->CamVecFront_X * this->field_0_party_dir_x) +
        (this->CamVecFront_Y * this->field_4_party_dir_y) +
        (this->CamVecFront_Z * this->field_8_party_dir_z);
}

/**
 * @return                              Anisotropy level for `TEXTURE_FILTER_MIPMAPPED`, 1 if the driver doesn't support
 *                                      anisotropic filtering.
 */
static float maxTextureAnisotropy() {
    static const float result = [] {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; i++) {
            const char *name = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
            if (!name)
                continue;

            std::string_view extension = name;
            if (extension == "GL_ARB_texture_filter_anisotropic" || extension == "GL_EXT_texture_filter_anisotropic") {
                float max = 1.0f;
                glGetFloatv(MAX_TEXTURE_MAX_ANISOTROPY, &max);
                return std::min(max, ANISOTROPY_LIMIT);
            }
        }
        return 1.0f;
    }();
    return result;
}

/**
 * Switches the bound texture to `TEXTURE_FILTER_MIPMAPPED`.
 *
 * @param target                        Target the texture is bound to. Its mipmaps must already be generated.
 */
static void setMipmappedFilter(GLenum target) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    float anisotropy = maxTextureAnisotropy();
    if (anisotropy > 1.0f)
        glTexParameterf(target, TEXTURE_MAX_ANISOTROPY, anisotropy);
}

/**
 * Uploads pixels into a layer of the bound texture array.
 *
 * @param pixels                        Pixels to upload, `pixelScale` times the logical size of the array's layers.
 * @param pixelScale                    Scale of `pixels`.
 * @param layer                         Array layer to upload into.
 * @param width                         Logical width of the array's layers.
 * @param height                        Logical height of the array's layers.
 * @param scale                         Scale of the array, a multiple of `pixelScale`. The pixels are upscaled to it.
 */
static void uploadArrayLayer(RgbaImageView pixels, int pixelScale, int layer, int width, int height, int scale) {
    RgbaImage scaled;
    if (pixelScale != scale) {
        scaled = scaleNearest(pixels, scale / pixelScale);
        pixels = scaled;
    }

    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width * scale, height * scale, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.pixels().data());
}

/**
 * Uploads a texture into a layer of the bound texture array.
 *
 * @param texture                       Texture to upload.
 * @param layer                         Array layer to upload into.
 * @param width                         Logical width of the array's layers.
 * @param height                        Logical height of the array's layers.
 * @param scale                         Scale of the array, a multiple of `texture->textureScale()`. The texture is
 *                                      upscaled to it.
 */
static void uploadArrayLayer(GraphicsImage *texture, int layer, int width, int height, int scale) {
    uploadArrayLayer(texture->textureRgba(), texture->textureScale(), layer, width, height, scale);
}

/**
 * Loads the texture pack replacements of the bitmaps that texture arrays are built from all at once, on several
 * threads. Does nothing without a texture pack.
 *
 * @param textures                      Texture array map, by bitmap name.
 */
static void loadPackReplacements(const std::map<std::string, int> &textures) {
    if (!engine->resources()->hasTexturePack())
        return;

    std::vector<GraphicsImage *> images;
    for (const auto &[name, comb] : textures)
        if (!name.starts_with("generated")) // Generated tiles aren't in the pack.
            images.push_back(assets->getBitmap(name));
    GraphicsImage::loadReplacements(images);
}

/**
 * Creates the material array that goes next to a texture array. Each layer holds the material of the texture in the
 * same layer of the texture array, or zeros if that texture has no material.
 *
 * @param layers                        The texture array's textures, each with the layer it's in.
 * @param layerCount                    Number of layers in the texture array.
 * @param width                         Logical width of the array's layers.
 * @param height                        Logical height of the array's layers.
 * @param scale                         Scale of the texture array.
 * @param wrap                          Wrap mode of the texture array.
 * @return                              The material array, bound to `GL_TEXTURE0`, or 0 if none of the textures has
 *                                      a material.
 */
static GLuint createMaterialArray(std::span<const std::pair<GraphicsImage *, int>> layers, int layerCount, int width, int height, int scale, GLint wrap) {
    bool hasMaterials = std::ranges::any_of(layers, [](const auto &layer) { return static_cast<bool>(layer.first->materialRgba()); });
    if (!hasMaterials)
        return 0;

    GLuint result = 0;
    glGenTextures(1, &result);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, result);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width * scale, height * scale, layerCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    RgbaImage noMaterial = RgbaImage::solid(Color(), width * scale, height * scale);
    for (const auto &[texture, layer] : layers) {
        if (RgbaImageView material = texture->materialRgba()) {
            uploadArrayLayer(material, texture->textureScale(), layer, width, height, scale);
        } else {
            uploadArrayLayer(noMaterial, scale, layer, width, height, scale);
        }
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, wrap);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    if (scale > 1)
        setMipmappedFilter(GL_TEXTURE_2D_ARRAY);
    return result;
}

/**
 * @param value                         Grid coordinate.
 * @return                              A hash bit of the coordinate.
 */
static int terrainHashBit(uint32_t value) {
    uint32_t hash = value * 374761393u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    return static_cast<int>((hash ^ (hash >> 16)) & 1);
}

/**
 * @param x                             Terrain grid x of a square.
 * @param y                             Terrain grid y of a square.
 * @return                              Attribute bits 0x4 and 0x8 that mirror the square's texture along u and v in the
 *                                      linear pipeline's terrain variation. The u mirror only depends on the column and
 *                                      the v mirror only on the row, which keeps a tileable texture seamless: a mirror
 *                                      across an edge is continuous, one along it is not.
 */
static int terrainMirrorBits(int x, int y) {
    return (terrainHashBit(static_cast<uint32_t>(x)) << 2) | (terrainHashBit(static_cast<uint32_t>(y) + 0x9E3779B9u) << 3);
}

static int waterAnimationFrame() {
    // Water animation in vanilla was borked, or so it seems. Water has 7 frames, frame durations are:
    //
    // Frame    0       1       2       3       4       5       6       Total
    // Vanilla  1/12s   1/6s    1/6s    1/6s    1/6s    1/6s    1/12s   1s
    // OE       1/7s    1/7s    1/7s    1/7s    1/7s    1/7s    1/7s    1s
    return static_cast<int>(std::floor(std::fmod(animTimer->time().realtimeMillisecondsFloat(), 1.0f) * 7.0f));
}

OpenGLRenderer::OpenGLRenderer(
    std::shared_ptr<GameConfig> config,
    DecalBuilder *decal_builder,
    SpellFxRenderer *spellfx,
    std::shared_ptr<ParticleEngine> particle_engine,
    Vis *vis
) : BaseRenderer(config, decal_builder, spellfx, particle_engine, vis) {}

OpenGLRenderer::~OpenGLRenderer() {
    MM_INFO("RenderGl - Destructor");
    releaseSolidFillTexture();
    _shutdownImGui();
}

RgbaImage OpenGLRenderer::ReadScreenPixels() {
    Sizei size = uiTargetSize();
    RgbaImage result = RgbaImage::uninitialized(size.w, size.h);
    if (_usesUiTarget()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _uiTarget.framebuffer());
    }
    glReadPixels(0, 0, size.w, size.h, GL_RGBA, GL_UNSIGNED_BYTE, result.pixels().data());
    if (_usesUiTarget()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    return result;
}

void OpenGLRenderer::ClearTarget(Color uColor) {
    /* TODO(Gerark) Should we bind to the framebuffer before clearing?
    if (outputRender != outputPresent) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    }
    */

    glClearColor(0, 0, 0, 0/*0.9f, 0.5f, 0.1f, 1.0f*/);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    return;
}

void OpenGLRenderer::BeginLines2D() {
    if (!_lineVertices.empty())
        MM_TRACE("BeginLines with points still stored in buffer");

    DrawTwodVerts();

    if (!_lineBuffer) {
        _lineBuffer.reset(GL_DYNAMIC_DRAW,
            &LineVertex::pos,
            &LineVertex::color);
    }
}

/**
 * @param lines                         Pairs of line ends, in the units of the 2D projection.
 * @return                              Two triangles for each line, one unit wide, covering the units that a one pixel
 *                                      wide line covers at one pixel per unit.
 */
static std::vector<LineVertex> lineQuads(const std::vector<LineVertex> &lines) {
    std::vector<LineVertex> result;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        Vec2f a = lines[i].pos;
        Vec2f b = lines[i + 1].pos;
        float length = (b - a).length();
        if (length == 0)
            continue; // A line of no length draws nothing, as with GL_LINES.
        Vec2f along = (b - a) / length * 0.5f;
        Vec2f across = Vec2f(-along.y, along.x);
        LineVertex a0 = {a - along - across, lines[i].color};
        LineVertex a1 = {a - along + across, lines[i].color};
        LineVertex b0 = {b - along - across, lines[i + 1].color};
        LineVertex b1 = {b - along + across, lines[i + 1].color};
        result.insert(result.end(), {a0, b0, b1, a0, b1, a1}); // Ends like GL_LINES, the last unit is left out.
    }
    return result;
}

void OpenGLRenderer::EndLines2D() {
    if (_lineVertices.empty()) return;

    // Core profiles only draw lines one pixel wide, so lines in a UI scaled up are drawn as quads.
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    bool wide = _uiScale > 1 && static_cast<GLuint>(framebuffer) == _uiTarget.framebuffer();
    if (wide)
        _lineVertices = lineQuads(_lineVertices);

    _lineBuffer.update(_lineVertices);
    _lineBuffer.bind();

    lineshader.use();

    LineUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.submit(lineshader);

    glDrawArrays(wide ? GL_TRIANGLES : GL_LINES, 0, _lineVertices.size());
    drawcalls++;

    lineshader.unuse();
    _lineBuffer.unbind();

    _lineVertices.clear();
}

void OpenGLRenderer::RasterLine2D(Pointi a, Pointi b, Color acolor, Color bcolor) {
    LineVertex &v1 = _lineVertices.emplace_back();
    v1.pos = a.toFloat();
    v1.color = acolor.toColorf();

    LineVertex &v2 = _lineVertices.emplace_back();
    v2.pos = b.toFloat();
    v2.color = bcolor.toColorf();
}

void OpenGLRenderer::BeginScene3D(const CameraView &view) {
    _view = view;

    // Setup for 3D

    if (OpenGLRenderTarget *target = _currentWorldTarget()) {
        target->bind();
        glDisable(GL_SCISSOR_TEST);
    } else if (_usesUiTarget()) {
        _uiTarget.bind();
    }
    _setWorldViewport();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 0/*0.9f, 0.5f, 0.1f, 1.0f*/);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SetFogParametersGL();
    gamma = GetGamma();
    worldBillboardsDrawn = 0;
    _sceneDepthCopied = false;
}

void OpenGLRenderer::EndScene3D() {
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    OpenGLRenderTarget *target = _currentWorldTarget();
    if (!target) {
        _set_ortho_projection();
        _set_ortho_modelview();
        DrawTwodVerts();
        return;
    }

    if (_linearPipeline && target == &_externalLinearTarget) {
        // An external target has no 2D pass of its own, so it gets the tonemapped world here and the 2D quads over it.
        _externalOutput->bind();
        drawComposite(*target, Recti(Pointi(0, 0), target->size()), GL_LINEAR);
        target = _externalOutput;
    } else if (_linearPipeline) {
        _bindUiTarget(); // The 2D quads are display colours, the 2D pass draws them over the tonemapped world.
        glEnable(GL_SCISSOR_TEST);
        _setUiViewport();
        return;
    }

    // The queued 2D quads are in UI coordinates, this maps the game viewport onto the whole world target.
    glViewport(0, 0, target->size().w, target->size().h);
    projmat = glm::ortho(float(pViewport.x), float(pViewport.x + pViewport.w), float(pViewport.y + pViewport.h), float(pViewport.y), float(-1), float(1));
    _set_ortho_modelview();
    DrawTwodVerts();

    _bindUiTarget();
    glEnable(GL_SCISSOR_TEST);
    _setUiViewport();
}

Recti OpenGLRenderer::worldTargetViewport() {
    if (_worldTarget)
        return Recti(Pointi(0, 0), _worldTarget.size());
    return pViewport;
}

void OpenGLRenderer::compositeWorldTarget() {
    OpenGLRenderTarget *source = _mirrorPending ? &_mirrorTarget : (_worldTarget ? &_worldTarget : nullptr);
    _mirrorPresentPending = _uiLayer && _mirrorPending;
    _mirrorPending = false;
    if (!source)
        return;

    _bindUiTarget();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (_mirrorPresentPending) {
        glEnable(GL_SCISSOR_TEST);
        return;
    }

    GLenum filter = config->graphics.WorldTargetFilter.value() == 1 ? GL_LINEAR : GL_NEAREST;
    Recti viewport = _uiPixels(pViewport);
    if (_linearPipeline && source == &_worldTarget) {
        drawComposite(*source, viewport, filter);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source->framebuffer());
        glBlitFramebuffer(0, 0, source->size().w, source->size().h,
                          viewport.x, viewport.y, viewport.x + viewport.w, viewport.y + viewport.h, GL_COLOR_BUFFER_BIT, filter);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    glEnable(GL_SCISSOR_TEST);
}

GLint OpenGLRenderer::_colorArrayFormat() const {
    return _linearPipeline ? GL_SRGB8_ALPHA8 : GL_RGBA8;
}

Colorf OpenGLRenderer::_worldColor(Colorf color) const {
    if (!_linearPipeline)
        return color;

    auto decode = [](float value) {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    return Colorf(decode(color.r), decode(color.g), decode(color.b), color.a);
}

void OpenGLRenderer::drawComposite(const OpenGLRenderTarget &source, const Recti &viewport, GLenum filter) {
    _ensureFullscreenTriangle();

    GLint destination = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);

    GLuint bloom = 0;
    if (config->graphics.Bloom.value()) {
        bloom = _drawBloom(source);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    }

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    bool fxaa = config->graphics.Antialiasing.value() == ANTIALIASING_FXAA;
    if (fxaa) {
        if (_antialiasingTarget.size() != viewport.size())
            _antialiasingTarget.reset(viewport.size(), RENDER_TARGET_FORMAT_RGBA8, false);
        _antialiasingTarget.bind();
        glViewport(0, 0, viewport.w, viewport.h);
    } else {
        glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
    }

    if (bloom) {
        glActiveTexture(GL_TEXTURE0 + BLOOM_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, bloom);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.colorTexture());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    compositeshader.use();
    CompositeUniforms uniforms;
    uniforms.exposure = config->graphics.Exposure.value();
    uniforms.gamma = GetGamma();
    uniforms.bloom = BLOOM_TEXTURE_UNIT;
    uniforms.bloomIntensity = bloom ? config->graphics.BloomIntensity.value() : 0.0f;
    uniforms.submit(compositeshader);

    _compositeBuffer.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    drawcalls++;

    if (fxaa) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
        glBindTexture(GL_TEXTURE_2D, _antialiasingTarget.colorTexture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // FXAA samples past the edges, which must not wrap to the other side.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        fxaashader.use();
        FxaaUniforms fxaaUniforms;
        fxaaUniforms.texelSize = Vec2f(1.0f / viewport.w, 1.0f / viewport.h);
        fxaaUniforms.submit(fxaashader);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        drawcalls++;
    }
    _compositeBuffer.unbind();

    compositeshader.unuse();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (bloom) {
        glActiveTexture(GL_TEXTURE0 + BLOOM_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
}

GLuint OpenGLRenderer::_drawBloom(const OpenGLRenderTarget &source) {
    Sizei first(source.size().w / 2, source.size().h / 2);
    if (_bloomTargets.empty() || _bloomTargets[0]->size() != first) {
        _bloomTargets.clear();
        for (Sizei size = first; std::min(size.w, size.h) >= BLOOM_MIN_LEVEL_SIZE && _bloomTargets.size() < BLOOM_MAX_LEVELS; size = Sizei(size.w / 2, size.h / 2)) {
            _bloomTargets.push_back(std::make_unique<OpenGLRenderTarget>());
            _bloomTargets.back()->reset(size, RENDER_TARGET_FORMAT_R11G11B10F, false);
        }
    }
    if (_bloomTargets.empty())
        return 0;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    _compositeBuffer.bind();

    auto drawLevel = [&](OpenGLShader &shader, OpenGLRenderTarget &target, GLuint input, Sizei inputSize, bool brightPass) {
        target.bind();
        glViewport(0, 0, target.size().w, target.size().h);
        glBindTexture(GL_TEXTURE_2D, input);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        BloomUniforms uniforms;
        uniforms.texelSize = Vec2f(1.0f / inputSize.w, 1.0f / inputSize.h);
        uniforms.brightPass = brightPass;
        uniforms.threshold = config->graphics.BloomThreshold.value();
        uniforms.submit(shader);

        glDrawArrays(GL_TRIANGLES, 0, 3);
        drawcalls++;
    };

    bloomdownshader.use();
    GLuint input = source.colorTexture();
    Sizei inputSize = source.size();
    for (size_t i = 0; i < _bloomTargets.size(); i++) {
        drawLevel(bloomdownshader, *_bloomTargets[i], input, inputSize, i == 0);
        input = _bloomTargets[i]->colorTexture();
        inputSize = _bloomTargets[i]->size();
    }

    bloomupshader.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = _bloomTargets.size() - 1; i > 0; i--)
        drawLevel(bloomupshader, *_bloomTargets[i - 1], _bloomTargets[i]->colorTexture(), _bloomTargets[i]->size(), false);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);

    bloomupshader.unuse();
    _compositeBuffer.unbind();
    glBindTexture(GL_TEXTURE_2D, 0);
    return _bloomTargets[0]->colorTexture();
}

void OpenGLRenderer::_ensureFullscreenTriangle() {
    if (_compositeBuffer)
        return;

    _compositeBuffer.reset(GL_STATIC_DRAW, &CompositeVertex::pos);
    std::array<CompositeVertex, 3> triangle = {{{Vec2f(-1, -1)}, {Vec2f(3, -1)}, {Vec2f(-1, 3)}}}; // Covers the viewport.
    _compositeBuffer.update(triangle);
}

TextureRenderId OpenGLRenderer::uiTargetTexture() {
    if (!_usesUiTarget())
        return TextureRenderId();
    return TextureRenderId(_uiTarget.colorTexture());
}

Sizei OpenGLRenderer::uiTargetSize() {
    return Sizei(outputRender.w * _uiScale, outputRender.h * _uiScale);
}

void OpenGLRenderer::setExternalWorldTarget(TextureRenderId colorTexture, Sizei size) {
    _externalOutput = colorTexture ? _externalTarget(colorTexture, size, _externalTargetsHaveDepth()) : nullptr;
    _externalWorldTarget = _externalOutput;
    if (_externalOutput && _linearPipeline) {
        if (_externalLinearTarget.size() != size)
            _externalLinearTarget.reset(size, RENDER_TARGET_FORMAT_R11G11B10F, true);
        _externalWorldTarget = &_externalLinearTarget;
    }
}

void OpenGLRenderer::setViewportMirror(TextureRenderId colorTexture, Sizei size) {
    OpenGLRenderTarget *source = _externalTarget(colorTexture, size, _externalTargetsHaveDepth());
    Sizei mirrorSize = _uiPixels(pViewport).size();
    if (_mirrorTarget.size() != mirrorSize)
        _mirrorTarget.reset(mirrorSize, RENDER_TARGET_FORMAT_RGBA8, false);

    Recti crop(Pointi(0, 0), size);
    if (size.w * pViewport.h > size.h * pViewport.w) {
        crop.w = size.h * pViewport.w / pViewport.h;
        crop.x = (size.w - crop.w) / 2;
    } else {
        crop.h = size.w * pViewport.h / pViewport.w;
        crop.y = (size.h - crop.h) / 2;
    }

    glDisable(GL_SCISSOR_TEST);
    _mirrorTarget.bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source->framebuffer());
    glBlitFramebuffer(crop.x, crop.y, crop.x + crop.w, crop.y + crop.h, 0, 0, mirrorSize.w, mirrorSize.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    _bindUiTarget();
    glEnable(GL_SCISSOR_TEST);

    _mirrorPending = true;
}

void OpenGLRenderer::setExternalFramePacing(bool external) {
    _externalFramePacing = external;
}

/**
 * @param time                          Party time.
 * @param[out] fade                     How strong the sun's shadows are, from 0 below the horizon to 1 at
 *                                      `SUN_SHADOW_FADE_ELEVATION` and above.
 * @return                              Unit vector towards the sun. Same as `OutdoorLocation::UpdateSunlightVectors`,
 *                                      but with the seconds too, so that the sky moves smoothly instead of a step a
 *                                      minute.
 */
static Vec3f sunDirection(Time time, float *fade) {
    float minutes = (time.toSeconds() % 86400) / 60.0f - 5 * 60.0f; // The sun is up from 05:00 to 21:00.
    if (minutes < 0 || minutes >= 960) {
        *fade = 0;
        return Vec3f(0, 0, 1);
    }

    float angle = minutes * static_cast<float>(M_PI) / 960.0f;
    float elevation = std::min(angle, static_cast<float>(M_PI) - angle);
    *fade = std::clamp(elevation / glm::radians(SUN_SHADOW_FADE_ELEVATION), 0.0f, 1.0f);
    return Vec3f(std::cos(angle), 0, std::sin(angle));
}

/**
 * @param cascade                       Index of a cascade.
 * @param cascades                      Number of cascades.
 * @param nearClip                      Distance along the view where the first cascade starts.
 * @param farClip                       Distance along the view where the last cascade ends.
 * @return                              Distance along the view where `cascade` ends.
 */
static float sunShadowSplit(int cascade, int cascades, float nearClip, float farClip) {
    float part = (cascade + 1.0f) / cascades;
    float logarithmic = nearClip * std::pow(farClip / nearClip, part);
    float uniform = nearClip + (farClip - nearClip) * part;
    return SUN_SHADOW_SPLIT_LAMBDA * logarithmic + (1 - SUN_SHADOW_SPLIT_LAMBDA) * uniform;
}

/**
 * @param view                          View the sun shadow cascades cover.
 * @param splitNear                     Distance along the view where a cascade starts.
 * @param splitFar                      Distance along the view where it ends.
 * @param[out] centre                   Centre of a sphere around the slice of the view between the two.
 * @return                              Radius of the sphere, which doesn't change as the view turns.
 */
static float sunShadowSphere(const CameraView &view, float splitNear, float splitFar, glm::vec3 *centre) {
    glm::mat4 viewToWorld = glm::inverse(view.viewMatrix);
    std::array<glm::vec3, 8> corners;
    *centre = glm::vec3(0);
    for (int i = 0; i < 8; i++) {
        float depth = (i & 4) ? splitFar : splitNear;
        float x = ((i & 1) ? view.projection.tanRight : view.projection.tanLeft) * depth;
        float y = ((i & 2) ? view.projection.tanUp : view.projection.tanDown) * depth;
        corners[i] = glm::vec3(viewToWorld * glm::vec4(x, y, depth, 1));
        *centre += corners[i] / 8.0f;
    }
    float radius = 0;
    for (const glm::vec3 &corner : corners)
        radius = std::max(radius, glm::length(corner - *centre));
    return std::ceil(radius / 16.0f) * 16.0f;
}

/**
 * @return                              Brightness of a sprite in sun shadow relative to one in the sun. Terrain and
 *                                      buildings in shadow keep the sun's ambient light, while in the sun they reach
 *                                      the 0.85 the shaders clamp the sun to, so sprites take their ratio.
 */
static float sunShadowBillboardRatio() {
    float minutes = pParty->uCurrentMinute + pParty->uCurrentHour * 60.0f;
    float ambient = 0.15f + (std::sin((minutes - 360.0f) * 2 * static_cast<float>(M_PI) / 1440) + 1) * 0.27f; // As DrawOutdoorTerrain.
    return std::clamp(ambient / 0.85f, 0.0f, 1.0f);
}

void OpenGLRenderer::drawTorchShadows() {
    _torchShadowLights.clear();
    if (!_torchShadows || uCurrentlyLoadedLevelType != LEVEL_INDOOR)
        return;

    if (!_torchShadowMaps) {
        _torchShadowCount = config->graphics.TorchShadowCount.value();
        glGenTextures(1, &_torchShadowMaps);
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, _torchShadowMaps);
        glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT16, TORCH_SHADOW_SIZE, TORCH_SHADOW_SIZE, _torchShadowCount * 6, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, 0);

        GLenum none = GL_NONE;
        _torchShadowFramebuffers.resize(_torchShadowCount * 6);
        glGenFramebuffers(static_cast<GLsizei>(_torchShadowFramebuffers.size()), _torchShadowFramebuffers.data());
        for (size_t i = 0; i < _torchShadowFramebuffers.size(); i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, _torchShadowFramebuffers[i]);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _torchShadowMaps, 0, static_cast<GLint>(i));
            glDrawBuffers(1, &none);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                MM_ERROR("Torch shadow framebuffer {} is incomplete.", i);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        _torchShadowBuffer.reset(GL_DYNAMIC_DRAW, &TorchShadowVertex::pos, &TorchShadowVertex::normal);
    }

    // The nearest lights, by how far the camera is from the edge of their reach.
    Vec3f camera(pCamera3D->vCameraPos.x, pCamera3D->vCameraPos.y, pCamera3D->vCameraPos.z);
    std::vector<std::pair<float, int>> candidates;
    for (int i = 0; i < pStationaryLightsStack->uNumLightsActive; i++) {
        const StationaryLight &light = pStationaryLightsStack->pLights[i];
        if (light.uRadius >= 1)
            candidates.emplace_back((light.vPosition - camera).length() - light.uRadius, i);
    }
    int count = std::min(static_cast<int>(candidates.size()), _torchShadowCount);
    std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end());

    std::vector<std::pair<Vec3f, int>> drawnLights;
    for (int c = 0; c < count; c++) {
        const StationaryLight &light = pStationaryLightsStack->pLights[candidates[c].second];
        _torchShadowLights.push_back(candidates[c].second);
        drawnLights.emplace_back(light.vPosition, light.uRadius);
    }
    if (drawnLights == _torchShadowDrawnLights && pIndoor->geometryVersion == _torchShadowDrawnVersion)
        return; // The cubes already hold these lights and the walls haven't changed since.
    _torchShadowDrawnLights = std::move(drawnLights);
    _torchShadowDrawnVersion = pIndoor->geometryVersion;

    // Walls, floors and ceilings of the sectors each light reaches.
    std::vector<std::pair<int, int>> ranges;
    _torchShadowVertices.clear();
    for (int c = 0; c < count; c++) {
        const StationaryLight &light = pStationaryLightsStack->pLights[candidates[c].second];
        int first = static_cast<int>(_torchShadowVertices.size());
        for (const BLVSector &sector : pIndoor->sectors) {
            if (!sector.boundingBox.intersectsCube(light.vPosition, static_cast<float>(light.uRadius)))
                continue;
            for (std::span<uint16_t> ids : {sector.floorIds, sector.wallIds, sector.ceilingIds}) {
                for (uint16_t faceId : ids) {
                    const BLVFace &face = pIndoor->faces[faceId];
                    if (face.Invisible() || face.isPortal() || face.vertexIds.size() < 3)
                        continue;
                    for (int z = 0; z + 2 < static_cast<int>(face.vertexIds.size()); z++)
                        for (int i : {0, z + 1, z + 2})
                            _torchShadowVertices.push_back({pIndoor->vertices[face.vertexIds[i]], face.facePlane.normal});
                }
            }
        }
        ranges.emplace_back(first, static_cast<int>(_torchShadowVertices.size()) - first);
    }
    if (_torchShadowVertices.empty())
        return;
    _torchShadowBuffer.update(_torchShadowVertices);

    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glViewport(0, 0, TORCH_SHADOW_SIZE, TORCH_SHADOW_SIZE);
    torchshadowshader.use();
    _torchShadowBuffer.bind();

    // Cube map face directions and up vectors, in the order of the faces in a cube map array layer.
    static const std::array<std::pair<glm::vec3, glm::vec3>, 6> faces = {{
        {{1, 0, 0}, {0, -1, 0}}, {{-1, 0, 0}, {0, -1, 0}}, {{0, 1, 0}, {0, 0, 1}},
        {{0, -1, 0}, {0, 0, -1}}, {{0, 0, 1}, {0, -1, 0}}, {{0, 0, -1}, {0, -1, 0}}
    }};
    for (size_t c = 0; c < ranges.size(); c++) {
        const StationaryLight &light = pStationaryLightsStack->pLights[_torchShadowLights[c]];
        glm::vec3 position(light.vPosition.x, light.vPosition.y, light.vPosition.z);
        glm::mat4 projection = glm::perspectiveRH_NO(glm::radians(90.0f), 1.0f, TORCH_SHADOW_NEAR, static_cast<float>(light.uRadius));
        glUniform3f(torchshadowshader.uniformLocation("lightPosition"), position.x, position.y, position.z);
        glUniform1f(torchshadowshader.uniformLocation("lightRadius"), static_cast<float>(light.uRadius));
        for (int face = 0; face < 6; face++) {
            glm::mat4 lightMatrix = projection * glm::lookAtRH(position, position + faces[face].first, faces[face].second);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _torchShadowFramebuffers[c * 6 + face]);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(torchshadowshader.uniformLocation("lightMatrix"), 1, GL_FALSE, &lightMatrix[0][0]);
            if (ranges[c].second > 0) {
                glDrawArrays(GL_TRIANGLES, ranges[c].first, ranges[c].second);
                drawcalls++;
            }
        }
    }

    _torchShadowBuffer.unbind();
    torchshadowshader.unuse();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDepthMask(depthMask);
    if (cullFace)
        glEnable(GL_CULL_FACE);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (!depthTest)
        glDisable(GL_DEPTH_TEST);
}

void OpenGLRenderer::drawAmbientOcclusion() {
    if (!_ambientOcclusion || (uCurrentlyLoadedLevelType != LEVEL_OUTDOOR && uCurrentlyLoadedLevelType != LEVEL_INDOOR))
        return;

    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    _aoViewport = Recti(viewport[0], viewport[1], viewport[2], viewport[3]);
    Sizei size(std::max(1, viewport[2] / 2), std::max(1, viewport[3] / 2));
    if (_aoDepthTarget.size() != size) {
        _aoDepthTarget.reset(size, RENDER_TARGET_FORMAT_R16F, true);
        _aoTarget.reset(size, RENDER_TARGET_FORMAT_RG16F, false);
        _aoBlurTarget.reset(size, RENDER_TARGET_FORMAT_RG16F, false);
    }

    if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        _ensureOutdoorDepthGeometry();
    } else if (!_aoIndoorBuffer || _aoIndoorVersion != pIndoor->geometryVersion) {
        std::vector<ShadowVertex> vertices;
        for (const BLVFace &face : pIndoor->faces) {
            if (face.Invisible() || face.isPortal() || face.vertexIds.size() < 3)
                continue;
            for (int z = 0; z + 2 < static_cast<int>(face.vertexIds.size()); z++)
                for (int i : {0, z + 1, z + 2})
                    vertices.push_back({pIndoor->vertices[face.vertexIds[i]]});
        }
        if (!_aoIndoorBuffer)
            _aoIndoorBuffer.reset(GL_DYNAMIC_DRAW, &ShadowVertex::pos);
        if (!vertices.empty())
            _aoIndoorBuffer.update(vertices);
        _aoIndoorVertexCount = static_cast<int>(vertices.size());
        _aoIndoorVersion = pIndoor->geometryVersion;
    }

    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    std::array<GLfloat, 4> clearColor;
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    // The distance to the nearest face at every pixel, at half resolution.
    _aoDepthTarget.bind();
    glViewport(0, 0, size.w, size.h);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    aodepthshader.use();
    glUniformMatrix4fv(aodepthshader.uniformLocation("projection"), 1, GL_FALSE, &_view.projectionMatrix[0][0]);
    glUniformMatrix4fv(aodepthshader.uniformLocation("view"), 1, GL_FALSE, &_view.viewMatrix[0][0]);
    if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        _sunShadowTerrainBuffer.bind();
        glDrawArrays(GL_TRIANGLES, 0, 127 * 127 * 6);
        _sunShadowBuildingBuffer.bind();
        if (!_sunShadowModelRanges.empty())
            glDrawArrays(GL_TRIANGLES, 0, _sunShadowModelRanges.back().first + _sunShadowModelRanges.back().second);
        _sunShadowBuildingBuffer.unbind();
        drawcalls += 2;
    } else if (_aoIndoorVertexCount > 0) {
        _aoIndoorBuffer.bind();
        glDrawArrays(GL_TRIANGLES, 0, _aoIndoorVertexCount);
        _aoIndoorBuffer.unbind();
        drawcalls++;
    }
    glDisable(GL_DEPTH_TEST);

    _ensureFullscreenTriangle();
    _compositeBuffer.bind();
    glActiveTexture(GL_TEXTURE0);
    auto drawPass = [&](OpenGLShader &shader, OpenGLRenderTarget &target, const OpenGLRenderTarget &source, const char *sampler) {
        target.bind();
        glBindTexture(GL_TEXTURE_2D, source.colorTexture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        shader.use();
        glUniform1i(shader.uniformLocation(sampler), 0);
        if (&shader == &aoshader) {
            const glm::mat4 &projection = _view.projectionMatrix;
            glUniform4f(shader.uniformLocation("projection"), projection[0][0], projection[1][1], projection[2][0], projection[2][1]);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        drawcalls++;
    };
    drawPass(aoshader, _aoTarget, _aoDepthTarget, "depthMap");
    drawPass(aoblurshader, _aoBlurTarget, _aoTarget, "source");
    _compositeBuffer.unbind();
    aoblurshader.unuse();
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glDepthMask(depthMask);
    if (depthTest)
        glEnable(GL_DEPTH_TEST);
    if (cullFace)
        glEnable(GL_CULL_FACE);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

void OpenGLRenderer::_submitAmbientOcclusion(const OpenGLShader &shader) {
    if (!shader.hasDefine("AMBIENT_OCCLUSION"))
        return;

    glActiveTexture(GL_TEXTURE0 + AMBIENT_OCCLUSION_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, _aoBlurTarget.colorTexture());
    glActiveTexture(GL_TEXTURE0);
    const glm::mat4 &projection = _view.projectionMatrix;
    glUniform1i(shader.uniformLocation("ambientOcclusionMap"), AMBIENT_OCCLUSION_TEXTURE_UNIT);
    glUniform4f(shader.uniformLocation("ambientOcclusionViewport"), _aoViewport.x, _aoViewport.y, _aoViewport.w, _aoViewport.h);
    glUniform2f(shader.uniformLocation("ambientOcclusionDepth"), projection[2][2], projection[3][2]);
}

/**
 * @param probes                        Baked indirect light of a map.
 * @return                              Why the renderer can't use `probes`, or an empty string if it can.
 */
static std::string giProbesProblem(const GiProbes &probes) {
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &maxSize);
    if (std::max({probes.dims.x, probes.dims.y, probes.dims.z * 3}) > maxSize)
        return fmt::format("its grid of {}x{}x{} probes doesn't fit in a 3D texture", probes.dims.x, probes.dims.y, probes.dims.z);
    if (probes.layout == GI_PROBES_TERRAIN_LAYERS && (probes.dims.z > GI_MAX_LAYERS || std::ssize(probes.layerHeights) != probes.dims.z))
        return fmt::format("it has {} layers over the terrain, at most {} are supported", probes.dims.z, GI_MAX_LAYERS);
    if (probes.spacing.x <= 0 || probes.spacing.y <= 0 || (probes.layout == GI_PROBES_GRID && probes.spacing.z <= 0))
        return "its probe spacing isn't positive";
    if (!std::ranges::is_sorted(probes.timeHours))
        return "its time keys aren't in order";
    return {};
}

void OpenGLRenderer::_updateGlobalIllumination() {
    if (!_globalIllumination)
        return;

    if (_giMap != engine->_currentLoadedMapId) {
        _giMap = engine->_currentLoadedMapId;
        _giBlend = -1;
        _giKeys.clear();
        _giProbes.reset();
        if (_giMap != MAP_INVALID) {
            std::string_view mapFile = pMapTable->pInfos[_giMap].fileName;
            _giProbes = engine->resources()->packGiProbes(mapFile);
            if (_giProbes) {
                if (std::string problem = giProbesProblem(*_giProbes); !problem.empty()) {
                    MM_WARNING("Baked indirect light of '{}' isn't used, {}.", mapFile, problem);
                    _giProbes.reset();
                }
            }
        }
        if (!_giProbes)
            return;

        // The file holds each probe's red, green and blue coefficients together, the texture holds each colour's
        // coefficients as a block along z.
        const GiProbes &probes = *_giProbes;
        int count = probes.probeCount();
        _giKeys.resize(probes.timeHours.size() * count * GiProbes::COEFFICIENTS);
        for (size_t key = 0; key < probes.timeHours.size(); key++) {
            const uint16_t *src = probes.coefficients.data() + key * count * GiProbes::COEFFICIENTS;
            float *dst = _giKeys.data() + key * count * GiProbes::COEFFICIENTS;
            for (int probe = 0; probe < count; probe++)
                for (int channel = 0; channel < 3; channel++)
                    for (int i = 0; i < 4; i++)
                        dst[(channel * count + probe) * 4 + i] = halfToFloat(src[probe * GiProbes::COEFFICIENTS + channel * 4 + i]);
        }

        if (!_giTexture)
            glGenTextures(1, &_giTexture);
        glBindTexture(GL_TEXTURE_3D, _giTexture);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, probes.dims.x, probes.dims.y, probes.dims.z * 3, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
            glTexParameteri(GL_TEXTURE_3D, wrap, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);

        if (probes.layout == GI_PROBES_TERRAIN_LAYERS) {
            std::vector<float> heights; // Terrain height and whether it's water, for each column of probes.
            for (int y = 0; y < probes.dims.y; y++) {
                for (int x = 0; x < probes.dims.x; x++) {
                    Vec3f column = probes.origin + Vec3f(x * probes.spacing.x, y * probes.spacing.y, 0);
                    heights.push_back(pOutdoor->pTerrain.heightByPos(column));
                    heights.push_back(pOutdoor->pTerrain.isWaterByPos(column) ? 1.0f : 0.0f);
                }
            }
            if (!_giTerrainHeights)
                glGenTextures(1, &_giTerrainHeights);
            glBindTexture(GL_TEXTURE_2D, _giTerrainHeights);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, probes.dims.x, probes.dims.y, 0, GL_RG, GL_FLOAT, heights.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    if (!_giProbes)
        return;

    // The keys before and after the time of day, around midnight if it's past the last key or before the first.
    const std::vector<float> &hours = _giProbes->timeHours;
    float hour = pParty->uCurrentHour + pParty->uCurrentMinute / 60.0f;
    size_t next = std::ranges::upper_bound(hours, hour) - hours.begin();
    size_t prev = next == 0 ? hours.size() - 1 : next - 1;
    next %= hours.size();
    float span = std::fmod(hours[next] - hours[prev] + 24.0f, 24.0f);
    int step = span > 0 ? static_cast<int>(std::fmod(hour - hours[prev] + 24.0f, 24.0f) / span * GI_BLEND_STEPS) : 0;
    int blend = static_cast<int>(prev) * (GI_BLEND_STEPS + 1) + step;
    if (blend == _giBlend)
        return;
    _giBlend = blend;
    float weight = static_cast<float>(step) / GI_BLEND_STEPS;

    size_t size = _giKeys.size() / hours.size();
    std::vector<float> blended(size);
    const float *a = _giKeys.data() + prev * size;
    const float *b = _giKeys.data() + next * size;
    for (size_t i = 0; i < size; i++)
        blended[i] = a[i] + (b[i] - a[i]) * weight;
    _giAverage = _giProbes->averageIrradiance[prev] + (_giProbes->averageIrradiance[next] - _giProbes->averageIrradiance[prev]) * weight;

    const Vec3i &dims = _giProbes->dims;
    glBindTexture(GL_TEXTURE_3D, _giTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, dims.x, dims.y, dims.z * 3, GL_RGBA, GL_FLOAT, blended.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}

void OpenGLRenderer::_submitGlobalIllumination(const OpenGLShader &shader) {
    if (!shader.hasDefine("GLOBAL_ILLUMINATION"))
        return;

    glActiveTexture(GL_TEXTURE0 + GI_PROBES_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_3D, _giProbes ? _giTexture : 0);
    glActiveTexture(GL_TEXTURE0 + GI_TERRAIN_HEIGHTS_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, _giProbes ? _giTerrainHeights : 0);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shader.uniformLocation("giProbes"), GI_PROBES_TEXTURE_UNIT);
    glUniform1i(shader.uniformLocation("giTerrainHeights"), GI_TERRAIN_HEIGHTS_TEXTURE_UNIT);

    if (!_giProbes) { // A zero average turns the indirect light off.
        glUniform3f(shader.uniformLocation("giOrigin"), 0, 0, 0);
        glUniform3f(shader.uniformLocation("giSpacing"), 1, 1, 1);
        glUniform3i(shader.uniformLocation("giDims"), 1, 1, 1);
        glUniform1i(shader.uniformLocation("giTerrainLayers"), 0);
        glUniform3f(shader.uniformLocation("giAverage"), 0, 0, 0);
        return;
    }

    const GiProbes &probes = *_giProbes;
    bool terrainLayers = probes.layout == GI_PROBES_TERRAIN_LAYERS;
    glUniform3f(shader.uniformLocation("giOrigin"), probes.origin.x, probes.origin.y, probes.origin.z);
    glUniform3f(shader.uniformLocation("giSpacing"), probes.spacing.x, probes.spacing.y, terrainLayers ? 1.0f : probes.spacing.z);
    glUniform3i(shader.uniformLocation("giDims"), probes.dims.x, probes.dims.y, probes.dims.z);
    glUniform1i(shader.uniformLocation("giTerrainLayers"), terrainLayers);
    if (terrainLayers)
        glUniform1fv(shader.uniformLocation("giLayerHeights"), probes.dims.z, probes.layerHeights.data());
    glUniform3f(shader.uniformLocation("giAverage"), _giAverage.x, _giAverage.y, _giAverage.z);
}

void OpenGLRenderer::_submitTorchShadows(const OpenGLShader &shader, std::span<const int> layers) {
    if (!shader.hasDefine("TORCH_SHADOWS"))
        return;

    glActiveTexture(GL_TEXTURE0 + TORCH_SHADOW_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, _torchShadowMaps);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shader.uniformLocation("torchShadowMaps"), TORCH_SHADOW_TEXTURE_UNIT);
    glUniform1iv(shader.uniformLocation("torchShadowLayers"), static_cast<GLsizei>(layers.size()), layers.data());
}

void OpenGLRenderer::drawSunShadows(std::span<const int> casterModels, std::span<const SunShadowSprite> casterSprites) {
    _sunShadowFade = 0;
    if (!_sunShadows || uCurrentlyLoadedLevelType != LEVEL_OUTDOOR)
        return;

    float fade = 0;
    Vec3f sun = sunDirection(pParty->GetPlayingTime(), &fade);
    if (fade <= 0 || pWeather->bNight)
        return;

    _ensureSunShadowResources();

    CameraView view = CameraView::fromCamera3D(*pCamera3D, worldTargetViewport());
    float nearClip = view.projection.nearClip;
    float farClip = std::min(static_cast<float>(config->graphics.ShadowDistance.value()), view.projection.farClip);

    // The shadow maps follow the sun in steps, because every texel of a map that turns lands somewhere else each frame.
    glm::vec3 nearestCentre;
    float nearestTexel = 2 * sunShadowSphere(view, nearClip, sunShadowSplit(0, SUN_SHADOW_CASCADES, nearClip, farClip), &nearestCentre) / _sunShadowMapSize;
    float shadowLength = std::round(SUN_SHADOW_STEP_HEIGHT * sun.x / sun.z / nearestTexel) * nearestTexel;
    sun = Vec3f(shadowLength, 0, SUN_SHADOW_STEP_HEIGHT);
    sun /= sun.length();
    _sunShadowDirection = sun;

    // The sprite quads stand upright and turn to face the sun, the same quads go into every cascade.
    Vec3f right = cross(Vec3f(-sun.x, -sun.y, 0), Vec3f(0, 0, 1));
    right /= right.length();
    struct SpriteBatch {
        GLuint texture;
        bool palette;
        int first;
        int count;
    };
    std::vector<SpriteBatch> spriteBatches;
    _sunShadowSpriteVertices.clear();
    for (const SunShadowSprite &caster : casterSprites) {
        int paletteId = caster.paletteId;
        GLuint texture = static_cast<GLuint>(billboardTextureId(caster.sprite->texture, &paletteId));
        float halfWidth = caster.sprite->uWidth / 2.0f * caster.scale;
        float height = caster.sprite->uHeight * caster.scale;
        float left = caster.mirrored ? 1.0f : 0.0f;
        std::array<ShadowSpriteVertex, 4> corners = {{
            {caster.pos - right * halfWidth + Vec3f(0, 0, height), Vec2f(left, 0), static_cast<float>(paletteId)},
            {caster.pos - right * halfWidth, Vec2f(left, 1), static_cast<float>(paletteId)},
            {caster.pos + right * halfWidth, Vec2f(1 - left, 1), static_cast<float>(paletteId)},
            {caster.pos + right * halfWidth + Vec3f(0, 0, height), Vec2f(1 - left, 0), static_cast<float>(paletteId)}
        }};
        if (spriteBatches.empty() || spriteBatches.back().texture != texture)
            spriteBatches.push_back({texture, paletteId != 0, static_cast<int>(_sunShadowSpriteVertices.size()), 0});
        for (int i : {0, 1, 2, 0, 2, 3})
            _sunShadowSpriteVertices.push_back(corners[i]);
        spriteBatches.back().count += 6;
    }
    if (!_sunShadowSpriteBuffer) {
        _sunShadowSpriteBuffer.reset(GL_DYNAMIC_DRAW, &ShadowSpriteVertex::pos, &ShadowSpriteVertex::texuv, &ShadowSpriteVertex::paletteId);
        shadowspriteshader.use();
        glUniform1i(shadowspriteshader.uniformLocation("texture0"), 0);
    }
    if (!_sunShadowSpriteVertices.empty())
        _sunShadowSpriteBuffer.update(_sunShadowSpriteVertices);

    glm::mat4 lightRotation = glm::lookAtRH(glm::vec3(0), -glm::vec3(sun.x, sun.y, sun.z), glm::vec3(0, 1, 0)); // The sun never leaves the X-Z plane.
    glm::mat4 textureSpace = glm::translate(glm::mat4(1), glm::vec3(0.5f)) * glm::scale(glm::mat4(1), glm::vec3(0.5f));

    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glDisable(GL_CULL_FACE); // Terrain is a one-sided heightfield, it has to cast from its back too.
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(SUN_SHADOW_SLOPE_BIAS, SUN_SHADOW_CONSTANT_BIAS);
    glViewport(0, 0, _sunShadowMapSize, _sunShadowMapSize);
    shadowshader.use();

    float splitNear = nearClip;
    for (int cascade = 0; cascade < SUN_SHADOW_CASCADES; cascade++) {
        float splitFar = sunShadowSplit(cascade, SUN_SHADOW_CASCADES, nearClip, farClip);
        glm::vec3 centre;
        float radius = sunShadowSphere(view, splitNear, splitFar, &centre);
        float texel = 2 * radius / _sunShadowMapSize;

        // Snapping the centre to whole texels keeps the shadow edges still as the camera moves.
        glm::vec3 lightCentre = glm::vec3(lightRotation * glm::vec4(centre, 1));
        lightCentre.x = std::floor(lightCentre.x / texel) * texel;
        lightCentre.y = std::floor(lightCentre.y / texel) * texel;
        lightCentre.z = std::floor(lightCentre.z / texel) * texel;
        float zNear = -lightCentre.z - radius - SUN_SHADOW_EXTRUSION;
        float zFar = -lightCentre.z + radius;
        glm::mat4 projection = glm::ortho(lightCentre.x - radius, lightCentre.x + radius, lightCentre.y - radius, lightCentre.y + radius, zNear, zFar);
        glm::mat4 lightMatrix = projection * lightRotation;
        _sunShadowMatrices[cascade] = textureSpace * lightMatrix;
        _sunShadowTexels[cascade] = texel;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _sunShadowFramebuffers[cascade]);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(shadowshader.uniformLocation("lightMatrix"), 1, GL_FALSE, &lightMatrix[0][0]);

        _sunShadowTerrainBuffer.bind();
        glDrawArrays(GL_TRIANGLES, 0, 127 * 127 * 6);
        drawcalls++;

        _sunShadowBuildingBuffer.bind();
        for (int modelId : casterModels) {
            const BSPModel &model = pOutdoor->pBModels[modelId];
            const auto &[first, count] = _sunShadowModelRanges[modelId];
            glm::vec3 modelCentre = glm::vec3(lightRotation * glm::vec4(model.boundingCenter.x, model.boundingCenter.y, model.boundingCenter.z, 1));
            float reach = radius + model.boundingRadius;
            if (count == 0 || std::abs(modelCentre.x - lightCentre.x) > reach || std::abs(modelCentre.y - lightCentre.y) > reach)
                continue;
            if (-modelCentre.z + model.boundingRadius < zNear || -modelCentre.z - model.boundingRadius > zFar)
                continue;
            glDrawArrays(GL_TRIANGLES, first, count);
            drawcalls++;
        }

        if (!spriteBatches.empty()) {
            shadowspriteshader.use();
            glUniformMatrix4fv(shadowspriteshader.uniformLocation("lightMatrix"), 1, GL_FALSE, &lightMatrix[0][0]);
            _sunShadowSpriteBuffer.bind();
            glActiveTexture(GL_TEXTURE0);
            for (const SpriteBatch &batch : spriteBatches) {
                glBindTexture(GL_TEXTURE_2D, batch.texture);
                GLint filter = batch.palette ? GL_NEAREST : GL_LINEAR; // Palette sprites hold indices, which don't filter.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
                drawcalls++;
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            shadowshader.use();
        }

        splitNear = splitFar;
    }

    _sunShadowBuildingBuffer.unbind();
    shadowshader.unuse();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDepthMask(depthMask);
    if (cullFace)
        glEnable(GL_CULL_FACE);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (!depthTest)
        glDisable(GL_DEPTH_TEST);

    _sunShadowFade = fade;
}

void OpenGLRenderer::_ensureSunShadowResources() {
    if (!_sunShadowMap) {
        _sunShadowMapSize = config->graphics.ShadowMapSize.value();
        glGenTextures(1, &_sunShadowMap);
        glBindTexture(GL_TEXTURE_2D_ARRAY, _sunShadowMap);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, _sunShadowMapSize, _sunShadowMapSize, SUN_SHADOW_CASCADES, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        GLenum none = GL_NONE;
        glGenFramebuffers(SUN_SHADOW_CASCADES, _sunShadowFramebuffers.data());
        for (int cascade = 0; cascade < SUN_SHADOW_CASCADES; cascade++) {
            glBindFramebuffer(GL_FRAMEBUFFER, _sunShadowFramebuffers[cascade]);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _sunShadowMap, 0, cascade);
            glDrawBuffers(1, &none);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                MM_ERROR("Sun shadow framebuffer {} is incomplete.", cascade);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    _ensureOutdoorDepthGeometry();
}

void OpenGLRenderer::_ensureOutdoorDepthGeometry() {
    if (uCurrentlyLoadedLevelType != LEVEL_OUTDOOR || _sunShadowTerrainBuffer)
        return;

    std::vector<ShadowVertex> vertices;
    vertices.reserve(127 * 127 * 6);
    for (int y = 0; y < 127; y++) {
        for (int x = 0; x < 127; x++) {
            for (Pointi corner : {Pointi(x, y), Pointi(x + 1, y + 1), Pointi(x + 1, y), Pointi(x, y), Pointi(x, y + 1), Pointi(x + 1, y + 1)})
                vertices.push_back({pOutdoor->pTerrain.vertexByGridUnsafe(corner).toFloat()});
        }
    }
    _sunShadowTerrainBuffer.reset(GL_STATIC_DRAW, &ShadowVertex::pos);
    _sunShadowTerrainBuffer.update(vertices);

    vertices.clear();
    _sunShadowModelRanges.clear();
    for (const BSPModel &model : pOutdoor->pBModels) {
        int first = static_cast<int>(vertices.size());
        for (int faceId : model.faces) {
            const BLVFace &face = pOutdoor->faces[faceId];
            if (face.Invisible() || face.isFluid() || face.isPortal() || face.vertexIds.size() < 3)
                continue;
            for (int z = 0; z + 2 < static_cast<int>(face.vertexIds.size()); z++)
                for (int i : {0, z + 1, z + 2})
                    vertices.push_back({pOutdoor->vertices[face.vertexIds[i]]});
        }
        _sunShadowModelRanges.emplace_back(first, static_cast<int>(vertices.size()) - first);
    }
    _sunShadowBuildingBuffer.reset(GL_STATIC_DRAW, &ShadowVertex::pos);
    if (!vertices.empty())
        _sunShadowBuildingBuffer.update(vertices);
}

OpenGLRenderer::SkyState OpenGLRenderer::_skyState() {
    SkyState result;
    float fade = 0;
    result.sunDirection = sunDirection(pParty->GetPlayingTime(), &fade);

    Color fogColor = uCurrentlyLoadedLevelType == LEVEL_OUTDOOR ? GetLevelFogColor() : Color();
    bool day = uCurrentlyLoadedLevelType == LEVEL_OUTDOOR && !pWeather->bNight;
    result.daylight = day ? std::clamp(result.sunDirection.z / 0.05f, 0.0f, 1.0f) : 0.0f; // Sets over its last 3 degrees.
    result.sunVisibility = fogColor == Color() ? result.daylight : 0.0f;

    GraphicsImage *texture = uCurrentlyLoadedLevelType == LEVEL_OUTDOOR ? pOutdoor->sky_texture : nullptr;
    if (texture && texture != _skyAverageTexture) {
        float r = 0, g = 0, b = 0;
        const RgbaImage &pixels = texture->rgba();
        for (Color pixel : pixels.pixels()) {
            r += pixel.r;
            g += pixel.g;
            b += pixel.b;
        }
        float count = 255.0f * pixels.width() * pixels.height();
        _skyAverage = Colorf(r / count, g / count, b / count);
        _skyAverageTexture = texture;
    }

    // The same tint the sky is drawn with, it darkens the sky at night and fades it into the horizon.
    Colorf tint = GetActorTintColor(31, 0, pCamera3D->GetFarClip(), 1, 0).toColorf();
    Colorf horizon = fogColor != Color() ? fogColor.toColorf() : tint;
    result.horizon = _worldColor(horizon);
    result.zenith = _worldColor(Colorf(_skyAverage.r * tint.r, _skyAverage.g * tint.g, _skyAverage.b * tint.b));
    return result;
}

void OpenGLRenderer::_submitWater(const OpenGLShader &shader, const Colorf *reflection) {
    if (!shader.hasDefine("LINEAR_PIPELINE"))
        return;

    _ensureWaterNormals();
    glActiveTexture(GL_TEXTURE0 + WATER_NORMALS_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, _waterNormals);
    glActiveTexture(GL_TEXTURE0);

    SkyState sky = _skyState();
    if (reflection)
        sky.zenith = sky.horizon = _worldColor(*reflection);
    glUniform1i(shader.uniformLocation("waterNormals"), WATER_NORMALS_TEXTURE_UNIT);
    glUniform1f(shader.uniformLocation("waterTime"), animTimer->time().realtimeMillisecondsFloat());
    glUniform3f(shader.uniformLocation("skyZenith"), sky.zenith.r, sky.zenith.g, sky.zenith.b);
    glUniform3f(shader.uniformLocation("skyHorizon"), sky.horizon.r, sky.horizon.g, sky.horizon.b);
    glUniform3f(shader.uniformLocation("sunDirection"), sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z);
    glUniform1f(shader.uniformLocation("sunVisibility"), sky.sunVisibility);
}

void OpenGLRenderer::_ensureWaterNormals() {
    if (_waterNormals)
        return;

    // A sum of waves that each repeat a whole number of times across the map, so that it tiles.
    static constexpr std::array<std::array<int, 2>, 12> waves = {{
        {{1, 2}}, {{3, -1}}, {{-2, 3}}, {{4, 1}}, {{-3, -4}}, {{5, -2}}, {{2, 5}}, {{-6, 1}}, {{7, 3}}, {{-4, 6}}, {{8, -5}}, {{3, 9}}
    }};
    static constexpr float SLOPE_SCALE = 0.03f;
    RgbaImage image = RgbaImage::uninitialized(WATER_NORMALS_SIZE, WATER_NORMALS_SIZE);
    for (int y = 0; y < WATER_NORMALS_SIZE; y++) {
        for (int x = 0; x < WATER_NORMALS_SIZE; x++) {
            float dx = 0, dy = 0;
            for (size_t i = 0; i < waves.size(); i++) {
                float kx = static_cast<float>(waves[i][0]);
                float ky = static_cast<float>(waves[i][1]);
                float amplitude = 1.0f / std::sqrt(kx * kx + ky * ky);
                float phase = 2 * static_cast<float>(M_PI) * (kx * x + ky * y) / WATER_NORMALS_SIZE + i * 2.39996f; // Golden angle apart.
                float slope = amplitude * 2 * static_cast<float>(M_PI) * std::cos(phase);
                dx += slope * kx;
                dy += slope * ky;
            }
            Vec3f normal(-dx * SLOPE_SCALE, -dy * SLOPE_SCALE, 1);
            normal /= normal.length();
            image[y][x] = Color(static_cast<uint8_t>((normal.x * 0.5f + 0.5f) * 255), static_cast<uint8_t>((normal.y * 0.5f + 0.5f) * 255),
                                static_cast<uint8_t>((normal.z * 0.5f + 0.5f) * 255), 255);
        }
    }

    glGenTextures(1, &_waterNormals);
    glBindTexture(GL_TEXTURE_2D, _waterNormals);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WATER_NORMALS_SIZE, WATER_NORMALS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    setMipmappedFilter(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/**
 * @param sunDirection                  Direction towards the sun.
 * @return                              Colour of the sunlight, as `SunColour` in `sky.glsl` computes it.
 */
static Colorf sunColour(Vec3f sunDirection) {
    float t = std::clamp(sunDirection.z / 0.4f, 0.0f, 1.0f);
    t = t * t * (3 - 2 * t);
    return Colorf(1.0f, 0.55f + 0.4f * t, 0.3f + 0.55f * t);
}

void OpenGLRenderer::_submitHeightFog(const OpenGLShader &shader, bool worldPositions) {
    if (!shader.hasDefine("LINEAR_PIPELINE"))
        return;

    SkyState sky = _skyState();
    Colorf sun = sunColour(sky.sunDirection);
    float inscatter = worldPositions ? sky.daylight * FOG_INSCATTER : 0.0f;
    glUniform3f(shader.uniformLocation("fogEye"), _view.position.x, _view.position.y, _view.position.z);
    glUniform1f(shader.uniformLocation("fogFalloff"), worldPositions ? FOG_HEIGHT_FALLOFF : 0.0f);
    glUniform3f(shader.uniformLocation("fogSunDirection"), sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z);
    glUniform3f(shader.uniformLocation("fogInscatter"), sun.r * inscatter, sun.g * inscatter, sun.b * inscatter);
}

void OpenGLRenderer::_submitSunShadow(const OpenGLShader &shader) {
    if (!shader.hasDefine("SUN_SHADOWS"))
        return;

    glActiveTexture(GL_TEXTURE0 + SUN_SHADOW_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, _sunShadowMap);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shader.uniformLocation("sunShadowMap"), SUN_SHADOW_TEXTURE_UNIT);
    glUniformMatrix4fv(shader.uniformLocation("sunShadowMatrices"), SUN_SHADOW_CASCADES, GL_FALSE, &_sunShadowMatrices[0][0][0]);
    glUniform1fv(shader.uniformLocation("sunShadowTexels"), SUN_SHADOW_CASCADES, _sunShadowTexels.data());
    glUniform1f(shader.uniformLocation("sunShadowFade"), _sunShadowFade);
    glUniform1f(shader.uniformLocation("sunShadowStrength"), config->graphics.ShadowStrength.value());
    bool pcf1 = _externalWorldTarget || config->graphics.ShadowFilter.value() == SHADOW_FILTER_PCF1; // The 3x3 filter costs too much at the size of a headset's eye images.
    glUniform1i(shader.uniformLocation("sunShadowFilter"), pcf1);
}

void OpenGLRenderer::_readPipelineSettings() {
    _linearPipeline = config->graphics.LinearPipeline.value();
    _sunShadows = config->graphics.SunShadows.value();
    _torchShadows = config->graphics.TorchShadows.value();
    _ambientOcclusion = _linearPipeline && config->graphics.AmbientOcclusion.value();
    _globalIllumination = _linearPipeline && config->graphics.GlobalIllumination.value();
    _terrainVariation = _linearPipeline && config->graphics.TerrainVariation.value();
    _spriteCrossfade = config->graphics.SpriteCrossfade.value();
    _spriteDepth = _linearPipeline && config->graphics.SpriteDepth.value();
    _spriteLighting = _spriteDepth && config->graphics.SpriteLighting.value();
}

void OpenGLRenderer::applyPipelineSettings() {
    _readPipelineSettings();
    ReloadShaders();
    Reinitialize(false);
}

bool OpenGLRenderer::spriteShapes() {
    return _spriteDepth && worldSpaceBillboards();
}

bool OpenGLRenderer::worldSpaceBillboards() {
    return _externalWorldTarget || config->graphics.WorldSpaceBillboards.value();
}

void OpenGLRenderer::setUiLayer(bool enabled) {
    _uiLayer = enabled;
    _mirrorPresentPending = false;
}

bool OpenGLRenderer::uiLayer() {
    return _uiLayer;
}

void OpenGLRenderer::copyUiLayer(TextureRenderId colorTexture, Sizei size) {
    assert(_uiLayer);

    OpenGLRenderTarget *target = _externalTarget(colorTexture, size, false);
    Sizei uiSize = uiTargetSize();
    glDisable(GL_SCISSOR_TEST);
    target->bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _uiTarget.framebuffer());
    glBlitFramebuffer(0, 0, uiSize.w, uiSize.h, 0, 0, size.w, size.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    _bindUiTarget();
    glEnable(GL_SCISSOR_TEST);
}

void OpenGLRenderer::beginUiCard(TextureRenderId colorTexture, Sizei size, const Recti &screenRect) {
    assert(!_uiCard);
    DrawTwodVerts();
    EndTextNew();

    _uiCard = UiCard{_externalTarget(colorTexture, size, false), screenRect};
    GLfloat clearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    _uiCard->target->bind();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glEnable(GL_SCISSOR_TEST);
    _set_ortho_projection();
    _set_ortho_modelview();
    ResetUIClipRect();
}

void OpenGLRenderer::endUiCard() {
    assert(_uiCard);
    DrawTwodVerts();
    EndTextNew();
    _uiCard.reset();
    _bindUiTarget();
    _set_ortho_projection();
    ResetUIClipRect();
}

void OpenGLRenderer::copyUiLayerRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Pointi position) {
    assert(_uiLayer);

    Recti from = _uiPixels(rect);
    OpenGLRenderTarget *target = _externalTarget(colorTexture, size, false);
    glDisable(GL_SCISSOR_TEST);
    target->bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _uiTarget.framebuffer());
    glBlitFramebuffer(from.x, from.y, from.x + from.w, from.y + from.h, position.x, position.y, position.x + from.w, position.y + from.h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    _bindUiTarget();
    glEnable(GL_SCISSOR_TEST);
}

void OpenGLRenderer::fillExternalRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Color color) {
    GLfloat clearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    _externalTarget(colorTexture, size, false)->bind();
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    _bindUiTarget();
    SetUIClipRect(clipRect);
}

void OpenGLRenderer::forgetExternalTextures(std::span<const TextureRenderId> colorTextures) {
    for (TextureRenderId colorTexture : colorTextures) {
        GLuint texture = static_cast<GLuint>(colorTexture.value());
        assert(!_externalOutput || _externalOutput->colorTexture() != texture);
        _externalTargets.erase(texture);
    }
}

void OpenGLRenderer::DrawProjectile(float srcX, float srcY, float srcworldview, float srcfovoworldview,
                                    float dstX, float dstY, float dstworldview, float dstfovoworldview,
                                    GraphicsImage *texture) {
    // billboards projectile - lightning bolt

    int xDifference = static_cast<int>(std::round(dstX - srcX));
    int yDifference = static_cast<int>(std::round(dstY - srcY));
    int distapprox = Vec2i(xDifference, yDifference).length();

    float v16 = 1.0f / (float)distapprox;
    float srcxmod = (float)yDifference * v16 * srcfovoworldview;
    float srcymod = (float)xDifference * v16 * srcfovoworldview;

    float v20 = srcworldview * 4000.0f / pCamera3D->GetNearClip() / pCamera3D->GetFarClip();
    float v25 = dstworldview * 4000.0f / pCamera3D->GetNearClip() / pCamera3D->GetFarClip();
    float srcrhw = 1.0f / srcworldview;
    float dstxmod = (float)yDifference * v16 * dstfovoworldview;
    float dstymod = (float)xDifference * v16 * dstfovoworldview;
    float srcz = 1.0f - 1.0f / v20;
    float dstz = 1.0f - 1.0f / v25;
    float dstrhw = 1.0f / dstworldview;


    RenderVertexD3D3 v29[4];
    v29[0].pos.x = srcX + srcxmod;
    v29[0].pos.y = srcY - srcymod;
    v29[0].pos.z = srcz;
    v29[0].rhw = srcrhw;
    v29[0].diffuse = colorTable.White;
    v29[0].texcoord.x = 1.0;
    v29[0].texcoord.y = 0.0;

    v29[1].pos.x = dstxmod + dstX;
    v29[1].pos.y = dstY - dstymod;
    v29[1].pos.z = dstz;
    v29[1].rhw = dstrhw;
    v29[1].diffuse = colorTable.White;
    v29[1].texcoord.x = 1.0;
    v29[1].texcoord.y = 1.0;

    v29[2].pos.x = dstX - dstxmod;
    v29[2].pos.y = dstymod + dstY;
    v29[2].pos.z = dstz;
    v29[2].rhw = dstrhw;
    v29[2].diffuse = colorTable.White;
    v29[2].texcoord.x = 0.0;
    v29[2].texcoord.y = 1.0;

    v29[3].pos.x = srcX - srcxmod;
    v29[3].pos.y = srcymod + srcY;
    v29[3].pos.z = srcz;
    v29[3].rhw = srcrhw;
    v29[3].diffuse = colorTable.White;
    v29[3].texcoord.x = 0.0;
    v29[3].texcoord.y = 0.0;

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    // ErrD3D(pRenderD3D->pDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, FALSE));
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    int texid = 0;

    if (texture) {
        texid = texture->renderId().value();
    }

    // load up poly
    for (int z = 0; z < 2; z++) {
        // 123, 134, 145, 156..
        // copy first
        ForcePerVertex &v0 = _forcePerVertices.emplace_back();
        v0.pos = v29[0].pos;
        v0.w = 1.0f;
        v0.texuv = v29[0].texcoord;
        v0.texw = v29[0].rhw;
        v0.screenspace = srcworldview;
        v0.color = colorTable.White.toColorf();
        v0.texid = texid;

        // copy other two (z+1)(z+2)
        for (unsigned i = 1; i < 3; ++i) {
            ForcePerVertex &v = _forcePerVertices.emplace_back();
            v.pos = v29[z + i].pos;
            v.w = 1.0f;
            v.texuv = v29[z + i].texcoord;
            v.texw = v29[z + i].rhw;
            v.screenspace = (z + i == 3) ? srcworldview: dstworldview;
            v.color = colorTable.White.toColorf();
            v.texid = texid;
        }
    }

    // TODO(pskelton): do these need batching?
    DrawForcePerVerts();

    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);

    //ErrD3D(pRenderD3D->pDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, TRUE));
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void OpenGLRenderer::ScreenFade(Color color, float t) {
    Colorf cf = color.toColorf();
    cf.a = std::clamp(t, 0.0f, 1.0f);

    float drawx = static_cast<float>(pViewport.x);
    float drawy = static_cast<float>(pViewport.y);
    float drawz = static_cast<float>(pViewport.x + pViewport.w - 1);
    float draww = static_cast<float>(pViewport.y + pViewport.h - 1);

    float gltexid = static_cast<float>(solidFillTexture()->renderId().value());

    // 0 1 2 / 0 2 3

    TwoDVertex &v0 = _twodVertices.emplace_back();
    v0.pos = Vec3f(drawx, drawy, 0);
    v0.texuv = Vec2f(0.5f, 0.5f);
    v0.color = cf;
    v0.texid = gltexid;
    v0.paletteid = 0;

    TwoDVertex &v1 = _twodVertices.emplace_back();
    v1.pos = Vec3f(drawz, drawy, 0);
    v1.texuv = Vec2f(0.5f, 0.5f);
    v1.color = cf;
    v1.texid = gltexid;
    v1.paletteid = 0;

    TwoDVertex &v2 = _twodVertices.emplace_back();
    v2.pos = Vec3f(drawz, draww, 0);
    v2.texuv = Vec2f(0.5f, 0.5f);
    v2.color = cf;
    v2.texid = gltexid;
    v2.paletteid = 0;

    ////////////////////////////////

    TwoDVertex &v3 = _twodVertices.emplace_back();
    v3.pos = Vec3f(drawx, drawy, 0);
    v3.texuv = Vec2f(0.5f, 0.5f);
    v3.color = cf;
    v3.texid = 0;
    v3.paletteid = 0;

    TwoDVertex &v4 = _twodVertices.emplace_back();
    v4.pos = Vec3f(drawz, draww, 0);
    v4.texuv = Vec2f(0.5f, 0.5f);
    v4.color = cf;
    v4.texid = gltexid;
    v4.paletteid = 0;

    TwoDVertex &v5 = _twodVertices.emplace_back();
    v5.pos = Vec3f(drawx, draww, 0);
    v5.texuv = Vec2f(0.5f, 0.5f);
    v5.color = cf;
    v5.texid = gltexid;
    v5.paletteid = 0;

    return;
}


void OpenGLRenderer::DrawImage(GraphicsImage *img, const Recti &rect, int paletteid, Color uColor32) {
    if (!img) {
        MM_TRACE("Null img passed to DrawImage");
        return;
    }

    if (GraphicsImage *variant = assets->getSpriteVariant(img, paletteid)) {
        img = variant;
        paletteid = 0; // The variant has the palette applied.
    }

    Colorf cf = uColor32.toColorf();

    int width = img->width();
    int height = img->height();

    int x = rect.x;
    int y = rect.y;
    int z = rect.x + rect.w;
    int w = rect.y + rect.h;

    // check bounds
    if (x >= outputRender.w || y >= outputRender.h)
        return;

    // check for overlap
    Recti clippedRect = rect & this->clipRect;
    if (clippedRect.isEmpty())
        return;

    float gltexid = static_cast<float>(img->renderId().value());

    float drawx = clippedRect.x;
    float drawy = clippedRect.y;
    float drawz = clippedRect.x + clippedRect.w;
    float draww = clippedRect.y + clippedRect.h;

    float texx = (drawx - x) / float(z - x);
    float texy = (drawy - y) / float(w - y);
    float texz = (drawz - x) / float(z - x);
    float texw = (draww - y) / float(w - y);

    // 0 1 2 / 0 2 3

    TwoDVertex &v0 = _twodVertices.emplace_back();
    v0.pos = Vec3f(drawx, drawy, 0);
    v0.texuv = Vec2f(texx, texy);
    v0.color = cf;
    v0.texid = gltexid;
    v0.paletteid = paletteid;

    TwoDVertex &v1 = _twodVertices.emplace_back();
    v1.pos = Vec3f(drawz, drawy, 0);
    v1.texuv = Vec2f(texz, texy);
    v1.color = cf;
    v1.texid = gltexid;
    v1.paletteid = paletteid;

    TwoDVertex &v2 = _twodVertices.emplace_back();
    v2.pos = Vec3f(drawz, draww, 0);
    v2.texuv = Vec2f(texz, texw);
    v2.color = cf;
    v2.texid = gltexid;
    v2.paletteid = paletteid;

    ////////////////////////////////

    TwoDVertex &v3 = _twodVertices.emplace_back();
    v3.pos = Vec3f(drawx, drawy, 0);
    v3.texuv = Vec2f(texx, texy);
    v3.color = cf;
    v3.texid = gltexid;
    v3.paletteid = paletteid;

    TwoDVertex &v4 = _twodVertices.emplace_back();
    v4.pos = Vec3f(drawz, draww, 0);
    v4.texuv = Vec2f(texz, texw);
    v4.color = cf;
    v4.texid = gltexid;
    v4.paletteid = paletteid;

    TwoDVertex &v5 = _twodVertices.emplace_back();
    v5.pos = Vec3f(drawx, draww, 0);
    v5.texuv = Vec2f(texx, texw);
    v5.color = cf;
    v5.texid = gltexid;
    v5.paletteid = paletteid;

    return;
}

// TODO(pskelton): sort this - forcing the draw is slow
// TODO(pskelton): stencil masking with opacity would be a better way to do this
void OpenGLRenderer::BlendTextures(int x, int y, GraphicsImage *imgin, GraphicsImage *imgblend, int time, int start_opacity,
                                   int end_opacity) {
    // thrown together as a crude estimate of the enchaintg effects
    // leaves gap where it shouldnt on dark pixels currently
    // doesnt use opacity params

    if (imgin && imgblend) {  // 2 images to blend
        const RgbaImage &itemImage = imgin->rgba();
        const RgbaImage &maskImage = imgblend->rgba();

        RgbaImage dstImage = RgbaImage::solid(Color(), imgin->size());

        Color c = maskImage.pixels()[2700];  // guess at brightest pixel
        unsigned int rmax = c.r;
        unsigned int gmax = c.g;
        unsigned int bmax = c.b;

        unsigned int bmin = bmax / 10;
        unsigned int gmin = gmax / 10;
        unsigned int rmin = rmax / 10;

        unsigned int bstep = (bmax - bmin) / 128;
        unsigned int gstep = (gmax - gmin) / 128;
        unsigned int rstep = (rmax - rmin) / 128;

        int w = imgin->width();
        int h = imgin->height();
        for (int ydraw = 0; ydraw < h; ++ydraw) {
            for (int xdraw = 0; xdraw < w; ++xdraw) {
                // should go blue -> black -> blue reverse
                // patchy -> solid -> patchy

                if (itemImage[ydraw][xdraw] != Color()) {  // check orig item not got blakc pixel
                    Color pixcol = maskImage[ydraw % maskImage.height()][xdraw % maskImage.width()];

                    unsigned int rcur = pixcol.r;
                    unsigned int gcur = pixcol.g;
                    unsigned int bcur = pixcol.b;

                    int steps = (time) % 128;

                    if ((time) % 256 >= 128) {  // step down
                        bcur += bstep * (128 - steps);
                        gcur += gstep * (128 - steps);
                        rcur += rstep * (128 - steps);
                    } else {  // step up
                        bcur += bstep * steps;
                        gcur += gstep * steps;
                        rcur += rstep * steps;
                    }

                    bcur = std::clamp(bcur, bmin, bmax);
                    gcur = std::clamp(gcur, gmin, gmax);
                    rcur = std::clamp(rcur, rmin, rmax);

                    dstImage[ydraw][xdraw] = Color(rcur, gcur, bcur);
                }
            }
        }

        // draw image
        std::unique_ptr<GraphicsImage> temp = GraphicsImage::Create(std::move(dstImage));
        render->DrawQuad2D(temp.get(), {x, y});

        render->DrawTwodVerts();
    }
}

// TODO(pskelton): renderbase
void OpenGLRenderer::DrawIndoorSky(int /*uNumVertices*/, int uFaceID) {
    BLVFace *pFace = &pIndoor->faces[uFaceID];
    if (pFace->vertexIds.empty()) return;

    // TODO(yoctozepto, pskelton): we should probably try to handle these faces as they are otherwise marked as visible (see also BSPRenderer)
    if (!pFace->GetTexture()) return;

    if (worldSpaceBillboards() || _linearPipeline) { // The world space sky fades to the horizon in its shader, the fade polygons of the screen space sky come out lighter in linear.
        queueWorldIndoorSky(pFace);
        return;
    }

    int dimming_level = 0;
    unsigned int uNumVertices = pFace->vertexIds.size();


    // TODO(pskelton): repeated maths could be saved when calculating sky planes
    double rot_to_rads = ((2 * M_PI) / 2048);

    // lowers clouds as party goes up
    float  blv_horizon_height_offset = ((pCamera3D->ViewPlaneDistPixels * pCamera3D->vCameraPos.z)
        / (pCamera3D->ViewPlaneDistPixels + pCamera3D->GetFarClip())
        + (pBLVRenderParams->viewportRect.center().y));

    double cam_y_rot_rad = (double)pCamera3D->_viewPitch * rot_to_rads;

    float depth_to_far_clip = std::cos(pCamera3D->_viewPitch * rot_to_rads) * pCamera3D->GetFarClip();
    float height_to_far_clip = std::sin(pCamera3D->_viewPitch * rot_to_rads) * pCamera3D->GetFarClip();

    float blv_bottom_y_proj = ((pBLVRenderParams->viewportRect.center().y) -
        pCamera3D->ViewPlaneDistPixels /
        (depth_to_far_clip + 0.0000001f) *
        (height_to_far_clip - pCamera3D->vCameraPos.z));

    // rotation vec for sky plane - pitch
    float v_18x = -std::sin((-pCamera3D->_viewPitch + 16) * rot_to_rads);
    float v_18y = 0.0f;
    float v_18z = -std::cos((pCamera3D->_viewPitch + 16) * rot_to_rads);

    float inv_viewplanedist = 1.0f / pCamera3D->ViewPlaneDistPixels;

    RenderVertexSoft originalVertices[50];

    // copy to buff in
    for (unsigned i = 0; i < pFace->vertexIds.size(); ++i) {
        originalVertices[i].vWorldPosition = pIndoor->vertices[pFace->vertexIds[i]];
        originalVertices[i].u = (signed short)pFace->textureUs[i];
        originalVertices[i].v = (signed short)pFace->textureVs[i];
    }

    // clip accurately to camera
    pCamera3D->ClipFaceToFrustum(originalVertices, &uNumVertices, VertexRenderList, pBspRenderer->nodes[0].ViewportNodeFrustum.data());
    if (!uNumVertices) return;

    pCamera3D->ViewTransform(VertexRenderList, uNumVertices);
    pCamera3D->Project(VertexRenderList, uNumVertices, false);

    unsigned _507D30_idx = 0;
    for (; _507D30_idx < uNumVertices; _507D30_idx++) {
        // outbound screen x dist
        float x_dist = inv_viewplanedist * (pBLVRenderParams->viewportRect.center().x - VertexRenderList[_507D30_idx].vWorldViewProj.x);
        // outbound screen y dist
        float y_dist = inv_viewplanedist * (blv_horizon_height_offset - VertexRenderList[_507D30_idx].vWorldViewProj.y);

        // rotate vectors to cam facing
        float skyfinalleft = (SkyBillboard.CamVecLeft_X * x_dist) + (SkyBillboard.CamVecLeft_Z * y_dist) + SkyBillboard.CamVecLeft_Y;
        float skyfinalfront = (SkyBillboard.CamVecFront_X * x_dist) + (SkyBillboard.CamVecFront_Z * y_dist) + SkyBillboard.CamVecFront_Y;

        // pitch rotate sky to get top projection
        float newX = v_18x + v_18y + (v_18z * y_dist);
        float worldviewdepth = -512.0f / newX;

        // offset tex coords
        float texoffset_U = animTimer->time().realtimeMillisecondsFloat() + ((skyfinalleft * worldviewdepth) / 16.0f);
        VertexRenderList[_507D30_idx].u = texoffset_U / (pFace->GetTexture()->width());
        float texoffset_V = animTimer->time().realtimeMillisecondsFloat() + ((skyfinalfront * worldviewdepth) / 16.0f);
        VertexRenderList[_507D30_idx].v = texoffset_V / (pFace->GetTexture()->height());

        // this basically acts as texture perspective correction
        VertexRenderList[_507D30_idx]._rhw = worldviewdepth;
    }

    // no clipped polygon so draw and return??
    if (_507D30_idx >= uNumVertices) {
        DrawIndoorSkyPolygon(uNumVertices, pFace->GetTexture(), dimming_level);
        return;
    }
}

void OpenGLRenderer::DrawIndoorSkyPolygon(int uNumVertices, GraphicsImage *texture, int dimmingLevel) {
    int texid = texture->renderId().value();

    Colorf uTint = GetActorTintColor(dimmingLevel, 0, VertexRenderList[0].vWorldViewPosition.x, 1, 0).toColorf();
    float scrspace = pCamera3D->GetFarClip();

    float oneon = 1.0f / (pCamera3D->GetNearClip() * 2.0f);
    float oneof = 1.0f / (pCamera3D->GetFarClip());

    // load up poly
    for (int z = 0; z < (uNumVertices - 2); z++) {
        // 123, 134, 145, 156..
        float oneoz = 1.0f / VertexRenderList[0].vWorldViewPosition.x;
        float thisdepth = (oneoz - oneon) / (oneof - oneon);
        // copy first
        ForcePerVertex &v0 = _forcePerVertices.emplace_back();
        v0.pos = Vec3f(VertexRenderList[0].vWorldViewProj.x, VertexRenderList[0].vWorldViewProj.y, thisdepth);
        v0.w = VertexRenderList[0]._rhw;
        v0.texuv = Vec2f(VertexRenderList[0].u, VertexRenderList[0].v);
        v0.texw = 1.0f;
        v0.screenspace = scrspace;
        v0.color = uTint;
        v0.texid = texid;

        // copy other two (z+1)(z+2)
        for (unsigned i = 1; i < 3; ++i) {
            oneoz = 1.0f / VertexRenderList[z + i].vWorldViewPosition.x;
            thisdepth = (oneoz - oneon) / (oneof - oneon);
            ForcePerVertex &v = _forcePerVertices.emplace_back();
            v.pos = Vec3f(VertexRenderList[z + i].vWorldViewProj.x, VertexRenderList[z + i].vWorldViewProj.y, thisdepth);
            v.w = VertexRenderList[z + i]._rhw;
            v.texuv = Vec2f(VertexRenderList[z + i].u, VertexRenderList[z + i].v);
            v.texw = 1.0f;
            v.screenspace = scrspace;
            v.color = uTint;
            v.texid = texid;
        }
    }
}

RgbaImage OpenGLRenderer::MakeViewportScreenshot(const int width, const int height) {
    // TODO(pskelton): should this call drawworld instead??

    pCamera3D->updateFromParty();

    BeginScene3D(CameraView::fromCamera3D(*pCamera3D, worldTargetViewport()));
    resetBillboardRenderList();
    if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR)
        render->uFogColor = GetLevelFogColor();
    engine->_worldFrame->prepare();
    engine->_worldFrame->drawWithoutDecals();
    if (_worldTarget)
        EndScene3D(); // Without a world target, the 2D quads queued by the 3D pass are drawn by the next 2D flush.
    engine->_worldFrame->finish();

    if (_worldTarget)
        return makeWorldTargetScreenshot(width, height);

    // TODO(captainurist): subImage().scale()
    RgbaImage sPixels = ReadScreenPixels();
    float interval_x = static_cast<float>(pViewport.w) / width;
    float interval_y = static_cast<float>(pViewport.h) / height;

    RgbaImage pPixels = RgbaImage::solid(Color(), width, height);

    if (uCurrentlyLoadedLevelType != LEVEL_NULL) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                pPixels[y][x] = sPixels[_uiScale * (outputRender.h - (y + 1) * interval_y - pViewport.y)][_uiScale * (x * interval_x + pViewport.x)];
            }
        }
    }

    return pPixels;
}

RgbaImage OpenGLRenderer::MakeFullScreenshot() {
    return flipVertically(ReadScreenPixels());
}

void OpenGLRenderer::BeginDecals() {
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    // gen buffers

    if (!_decalBuffer) {
        _decalBuffer.reset(GL_DYNAMIC_DRAW,
            &DecalVertex::pos,
            &DecalVertex::texuv,
            &DecalVertex::texunit,
            &DecalVertex::color);
    }

    _decalVertices.clear();
}

void OpenGLRenderer::EndDecals() {
    if (_decalVertices.empty()) return;

    _decalBuffer.update(_decalVertices);

    _set_3d_projection_matrix();
    _set_3d_modelview_matrix();

    decalshader.use();

    DecalUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.fog = fog;
    uniforms.submit(decalshader);

    glActiveTexture(GL_TEXTURE0);

    GraphicsImage *texture = assets->getBitmap("hwsplat04");
    glBindTexture(GL_TEXTURE_2D, texture->renderId().value());

    _decalBuffer.bind();
    glDrawArrays(GL_TRIANGLES, 0, _decalVertices.size());
    drawcalls++;

    // unload
    decalshader.unuse();
    _decalBuffer.unbind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}



void OpenGLRenderer::DrawDecal(Decal *pDecal, float z_bias) {
    if (pDecal->uNumVertices < 3) {
        MM_WARNING("Decal has < 3 vertices");
        return;
    }

    float color_mult = pDecal->Fade_by_time();
    if (color_mult == 0.0f) return;

    // temp - bloodsplat persistance
    // color_mult = 1;

    // load into buffer
    Colorf decalColorMult = pDecal->uColorMultiplier.toColorf();

    for (int z = 0; z < (pDecal->uNumVertices - 2); z++) {
        // 123, 134, 145, 156..
        Colorf uTint = GetActorTintColor(pDecal->DimmingLevel, 0, pDecal->pVertices[0].vWorldViewPosition.x, 0, nullptr).toColorf();

        float uFinalR = uTint.r * color_mult * decalColorMult.r;
        float uFinalG = uTint.g * color_mult * decalColorMult.g;
        float uFinalB = uTint.b * color_mult * decalColorMult.b;

        // copy first
        DecalVertex &v0 = _decalVertices.emplace_back();
        v0.pos = pDecal->pVertices[0].vWorldPosition;
        v0.texuv = Vec2f(pDecal->pVertices[0].u, pDecal->pVertices[0].v);
        v0.texunit = 0;
        v0.color = Colorf(uFinalR, uFinalG, uFinalB, 1.0f);

        // copy other two (z+1)(z+2)
        for (unsigned i = 1; i < 3; ++i) {
            uTint = GetActorTintColor(pDecal->DimmingLevel, 0, pDecal->pVertices[z + i].vWorldViewPosition.x, 0, nullptr).toColorf();
            uFinalR = uTint.r * color_mult * decalColorMult.r;
            uFinalG = uTint.g * color_mult * decalColorMult.g;
            uFinalB = uTint.b * color_mult * decalColorMult.b;

            DecalVertex &v = _decalVertices.emplace_back();
            v.pos = pDecal->pVertices[z + i].vWorldPosition;
            v.texuv = Vec2f(pDecal->pVertices[z + i].u, pDecal->pVertices[z + i].v);
            v.texunit = 0;
            v.color = Colorf(uFinalR, uFinalG, uFinalB, 1.0f);
        }
    }
}

TextureRenderId OpenGLRenderer::CreateTexture(RgbaImageView image, TextureFilter filter) {
    assert(image);

    GLuint glId;
    glGenTextures(1, &glId);
    glBindTexture(GL_TEXTURE_2D, glId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (filter == TEXTURE_FILTER_MIPMAPPED) {
        glGenerateMipmap(GL_TEXTURE_2D);
        setMipmappedFilter(GL_TEXTURE_2D);
        _mipmappedTextures.insert(glId);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    return TextureRenderId(glId);
}

void OpenGLRenderer::updateTexture(TextureRenderId id, RgbaImageView image) {
    assert(id && image);

    GLuint glId = static_cast<GLuint>(id.value());
    glBindTexture(GL_TEXTURE_2D, glId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width(), image.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.pixels().data());
    if (_mipmappedTextures.contains(glId))
        glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DeleteTexture(TextureRenderId id) {
    if (!id)
        return;

    // Texture ids will be released after swapBuffers(). We might have the passed id saved in the render lists, so
    // can't release it yet.
    _texturesForDeletion.push_back(id.value());
}

void OpenGLRenderer::_set_3d_projection_matrix() {
    projmat = _view.projectionMatrix;
}

void OpenGLRenderer::_set_3d_modelview_matrix() {
    viewmat = _view.viewMatrix;
}

// TODO(pskelton): to camera?
void OpenGLRenderer::_set_ortho_projection(bool gameviewport) {
    if (_uiCard) { // Over the card's rectangle of the screen.
        _setUiViewport();
        const Recti &rect = _uiCard->screenRect;
        projmat = glm::ortho(float(rect.x), float(rect.x + rect.w), float(rect.y + rect.h), float(rect.y), float(-1), float(1));
    } else if (!gameviewport) {  // project over entire window
        _setUiViewport();
        projmat = glm::ortho(float(0), float(outputRender.w), float(outputRender.h), float(0), float(-1), float(1));
    } else {  // project to game viewport
        _setWorldViewport();
        projmat = glm::ortho(float(pViewport.x), float(pViewport.x + pViewport.w - 1), float(pViewport.y + pViewport.h - 1), float(pViewport.y), float(1), float(-1));
    }
}

// TODO(pskelton): to camera?
void OpenGLRenderer::_set_ortho_modelview() {
    // load identity matrix
    viewmat = glm::mat4x4(1);
}

void OpenGLRenderer::_setWorldViewport() {
    const Recti &rect = _view.viewport;
    OpenGLRenderTarget *target = _currentWorldTarget();
    Recti pixels = target ? Recti(rect.x, target->size().h - (rect.y + rect.h), rect.w, rect.h) : _uiPixels(rect);
    glViewport(pixels.x, pixels.y, pixels.w, pixels.h);
}

void OpenGLRenderer::_setUiViewport() {
    Sizei size = _uiCard ? _uiCard->target->size() : uiTargetSize();
    glViewport(0, 0, size.w, size.h);
}

Recti OpenGLRenderer::_uiPixels(const Recti &rect) const {
    if (_uiCard) {
        const Recti &card = _uiCard->screenRect;
        Sizei size = _uiCard->target->size();
        auto x = [&](int value) { return static_cast<int>(std::lround(static_cast<float>(value - card.x) * size.w / card.w)); };
        auto y = [&](int value) { return static_cast<int>(std::lround(static_cast<float>(card.y + card.h - value) * size.h / card.h)); };
        return Recti(x(rect.x), y(rect.y + rect.h), x(rect.x + rect.w) - x(rect.x), y(rect.y) - y(rect.y + rect.h));
    }
    return Recti(rect.x * _uiScale, (outputRender.h - (rect.y + rect.h)) * _uiScale, rect.w * _uiScale, rect.h * _uiScale);
}

OpenGLRenderTarget *OpenGLRenderer::_currentWorldTarget() {
    if (_externalWorldTarget)
        return _externalWorldTarget;
    if (_worldTarget)
        return &_worldTarget;
    return nullptr;
}

bool OpenGLRenderer::_externalTargetsHaveDepth() const {
    return !_linearPipeline; // With the linear pipeline the world is drawn into `_externalLinearTarget` and composited.
}

OpenGLRenderTarget *OpenGLRenderer::_externalTarget(TextureRenderId colorTexture, Sizei size, bool withDepth) {
    std::unique_ptr<OpenGLRenderTarget> &target = _externalTargets[static_cast<GLuint>(colorTexture.value())];
    if (!target)
        target = std::make_unique<OpenGLRenderTarget>();
    if (target->size() != size || target->hasDepth() != withDepth)
        target->resetExternal(static_cast<GLuint>(colorTexture.value()), size, withDepth);
    return target.get();
}

void OpenGLRenderer::_bindUiTarget() {
    if (_uiCard) {
        _uiCard->target->bind();
    } else if (_usesUiTarget()) {
        _uiTarget.bind();
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
}

bool OpenGLRenderer::_usesUiTarget() const {
    return outputRender != outputPresent || _uiLayer || _uiScale != 1;
}

void OpenGLRenderer::_setUiBlendFunc() {
    if (_uiLayer) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

RgbaImage OpenGLRenderer::makeWorldTargetScreenshot(int width, int height) {
    Sizei size = _worldTarget.size();
    GLuint framebuffer = _worldTarget.framebuffer();
    OpenGLRenderTarget tonemapped;
    if (_linearPipeline) {
        tonemapped.reset(size, RENDER_TARGET_FORMAT_RGBA8, false);
        tonemapped.bind();
        drawComposite(_worldTarget, Recti(Pointi(0, 0), size), GL_NEAREST);
        _bindUiTarget();
        glEnable(GL_SCISSOR_TEST);
        _setUiViewport();
        framebuffer = tonemapped.framebuffer();
    }

    RgbaImage pixels = RgbaImage::uninitialized(size.w, size.h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadPixels(0, 0, size.w, size.h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.pixels().data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    float intervalX = static_cast<float>(size.w) / width;
    float intervalY = static_cast<float>(size.h) / height;
    RgbaImage result = RgbaImage::solid(Color(), width, height);
    if (uCurrentlyLoadedLevelType != LEVEL_NULL) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                result[y][x] = pixels[size.h - (y + 1) * intervalY][x * intervalX];
    }
    return result;
}


// ---------------------- terrain -----------------------
void OpenGLRenderer::DrawOutdoorTerrain(const LightsSnapshot &lights) {
    _initWaterTiles();
    _updateGlobalIllumination();
    // shader version
    // draws entire terrain in one go at the moment
    // textures must all be square and same size
    // terrain is static and verts only submitted once on VAO creation

    // face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // camera matrices
    _set_3d_projection_matrix();
    _set_3d_modelview_matrix();

    // TODO(pskelton): move this to map loading
    // generate array and populate data
    if (!_terrainBuffer) {
        std::vector<RenderVertexSoft> pTerrainVertices(128 * 128);

        // generate vertex locations
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                pTerrainVertices[y * 128 + x].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({x, y}).toFloat();

        // reserve first 7 layers for water tiles in unit 0
        auto wtrtexture = this->hd_water_tile_anim[0];
        terraintexturesizes[0] = wtrtexture->width();

        for (int buff = 0; buff < 7; buff++) {
            std::string container_name = fmt::format("HDWTR{:03}", buff);
            terraintexmap.insert(std::make_pair(container_name, terraintexmap.size()));
            numterraintexloaded[0]++;
        }

        for (int y = 0; y < 127; ++y) {
            for (int x = 0; x < 127; ++x) {
                // map is 127 x 127 squares - each square has two triangles - each tri has 3 verts

                // first find all required textures for terrain and add to map
                const auto &tile = pOutdoor->pTerrain.tileDataByGrid({x, y});
                int tileunit = 0;
                int tilelayer = 0;
                bool isWater = (tile.textureName == "wtrtyl");

                // check if tile->name is already in list
                auto mapiter = terraintexmap.find(tile.textureName);
                if (mapiter != terraintexmap.end()) {
                    // if so, extract unit and layer
                    int unitlayer = mapiter->second;
                    tilelayer = unitlayer & 0xFF;
                    tileunit = (unitlayer & 0xFF00) >> 8;
                } else if (isWater) {
                    // water tile
                    tilelayer = 0;
                } else {
                    // else need to add it
                    auto thistexture = assets->getBitmap(tile.textureName, tile.flags & TILE_GENERATED_TRANSITION);
                    int width = thistexture->width();
                    // check size to see what unit it needs
                    int i;
                    for (i = 0; i < 8; i++) {
                        if (terraintexturesizes[i] == width || terraintexturesizes[i] == 0) break;
                    }

                    if (i == 8) {
                        MM_WARNING("Texture unit full - draw terrain!");
                        tileunit = 0;
                        tilelayer = 0;
                    } else {
                        if (terraintexturesizes[i] == 0) terraintexturesizes[i] = width;
                        tileunit = i;
                        tilelayer = numterraintexloaded[i];

                        // encode unit and layer together
                        int encode = (tileunit << 8) | tilelayer;

                        if (numterraintexloaded[i] < 256) {
                            // intsert into tex map
                            terraintexmap.insert(std::make_pair(tile.textureName, encode));
                            numterraintexloaded[i]++;
                        } else {
                            MM_WARNING("Texture layer full - draw terrain!");
                            tileunit = 0;
                            tilelayer = 0;
                        }
                    }
                }

                // next calculate all vertices vertices
                const auto &[norm, norm2] = pOutdoor->pTerrain.normalsByGridUnsafe({x, y});

                int attribs = isWater ? 0x1 : 0;
                bool plain = !isWater && tile.variant >= TILE_VARIANT_BASE1 && tile.variant <= TILE_VARIANT_BASE4 && !(tile.flags & TILE_GENERATED_TRANSITION);
                if (plain && _terrainVariation)
                    attribs |= terrainMirrorBits(x, y);

                // calc each vertex
                // [0] - x,y        n1
                _terrainVertices[6 * (x + (127 * y))].pos = pTerrainVertices[y * 128 + x].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y))].texuv = Vec2f(0, 0);
                _terrainVertices[6 * (x + (127 * y))].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y))].normal = norm;
                _terrainVertices[6 * (x + (127 * y))].attribs = static_cast<float>(attribs);

                // [1] - x+1,y+1    n1
                _terrainVertices[6 * (x + (127 * y)) + 1].pos = pTerrainVertices[(y + 1) * 128 + x + 1].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y)) + 1].texuv = Vec2f(1, 1);
                _terrainVertices[6 * (x + (127 * y)) + 1].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y)) + 1].normal = norm;
                _terrainVertices[6 * (x + (127 * y)) + 1].attribs = static_cast<float>(attribs);

                // [2] - x+1,y      n1
                _terrainVertices[6 * (x + (127 * y)) + 2].pos = pTerrainVertices[y * 128 + x + 1].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y)) + 2].texuv = Vec2f(1, 0);
                _terrainVertices[6 * (x + (127 * y)) + 2].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y)) + 2].normal = norm;
                _terrainVertices[6 * (x + (127 * y)) + 2].attribs = static_cast<float>(attribs);

                // [3] - x,y        n2
                _terrainVertices[6 * (x + (127 * y)) + 3].pos = pTerrainVertices[y * 128 + x].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y)) + 3].texuv = Vec2f(0, 0);
                _terrainVertices[6 * (x + (127 * y)) + 3].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y)) + 3].normal = norm2;
                _terrainVertices[6 * (x + (127 * y)) + 3].attribs = static_cast<float>(attribs);

                // [4] - x,y+1      n2
                _terrainVertices[6 * (x + (127 * y)) + 4].pos = pTerrainVertices[(y + 1) * 128 + x].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y)) + 4].texuv = Vec2f(0, 1);
                _terrainVertices[6 * (x + (127 * y)) + 4].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y)) + 4].normal = norm2;
                _terrainVertices[6 * (x + (127 * y)) + 4].attribs = static_cast<float>(attribs);

                // [5] - x+1,y+1    n2
                _terrainVertices[6 * (x + (127 * y)) + 5].pos = pTerrainVertices[(y + 1) * 128 + x + 1].vWorldPosition;
                _terrainVertices[6 * (x + (127 * y)) + 5].texuv = Vec2f(1, 1);
                _terrainVertices[6 * (x + (127 * y)) + 5].texturelayer = tilelayer;
                _terrainVertices[6 * (x + (127 * y)) + 5].normal = norm2;
                _terrainVertices[6 * (x + (127 * y)) + 5].attribs = static_cast<float>(attribs);
            }
        }

        // generate VAO/VBO
        _terrainBuffer.reset(GL_STATIC_DRAW,
            &ShaderVertex::pos, &ShaderVertex::texuv, &ShaderVertex::texturelayer,
            &ShaderVertex::normal, &ShaderVertex::attribs);
        _terrainBuffer.update(_terrainVertices);

        // texture set up - load in all previously found
        loadPackReplacements(terraintexmap);
        for (int unit = 0; unit < 8; unit++) {
            assert(numterraintexloaded[unit] <= 256);
            // skip if textures are empty
            if (numterraintexloaded[unit] == 0) continue;

            glGenTextures(1, &terraintextures[unit]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, terraintextures[unit]);

            int scale = 1;
            for (const auto &[name, comb] : terraintexmap)
                if (((comb & 0xFF00) >> 8) == unit)
                    scale = std::max(scale, assets->getBitmap(name, name.starts_with("generated"))->textureScale());

            // create blank memory for later texture submission
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, _colorArrayFormat(), terraintexturesizes[unit] * scale, terraintexturesizes[unit] * scale, numterraintexloaded[unit],
                         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            // loop through texture map
            std::vector<std::pair<GraphicsImage *, int>> layers;
            std::map<std::string, int>::iterator it = terraintexmap.begin();
            while (it != terraintexmap.end()) {
                int comb = it->second;
                int tlayer = comb & 0xFF;
                int tunit = (comb & 0xFF00) >> 8;

                if (tunit == unit) {
                    // get texture
                    auto texture = assets->getBitmap(it->first, it->first.starts_with("generated")); // TODO(captainurist): terrible, terrible hack, redo this.
                    // send texture data to gpu
                    uploadArrayLayer(texture, tlayer, terraintexturesizes[unit], terraintexturesizes[unit], scale);
                    layers.emplace_back(texture, tlayer);
                }

                it++;
            }

            // last texture setups
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            if (scale > 1)
                setMipmappedFilter(GL_TEXTURE_2D_ARRAY);

            terrainmaterials[unit] = createMaterialArray(layers, numterraintexloaded[unit], terraintexturesizes[unit], terraintexturesizes[unit], scale, GL_CLAMP_TO_EDGE);
        }
    }

/////////////////////////////////////////////////////
    // actual drawing

    // terrain debug
    if (config->debug.Terrain.value())
        // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
        if (!OpenGLES)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // load texture arrays in - we only use unit 0 for water and unit 1 for tiles for time being
    for (int unit = 0; unit < 8; unit++) {
        // skip if textures are empty
        if (numterraintexloaded[unit] > 0) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, terraintextures[unit]);
        }
    }

    // load terrain verts
    _terrainBuffer.bind();

    // use the terrain shader
    GLuint tileMaterials = terrainmaterials[TERRAIN_TILE_TEXTURE_UNIT];
    OpenGLShader &shader = tileMaterials ? terrainmaterialshader : terrainshader;
    shader.use();

    // set base uniforms
    TerrainUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.cameraPos = _view.position;
    uniforms.fog = fog;
    uniforms.gamma = gamma;
    uniforms.waterframe = waterAnimationFrame();

    // sun lighting stuff
    float ambient = pParty->uCurrentMinute + pParty->uCurrentHour * 60.0;  // 0 - > 1439
    ambient = 0.15 + (sinf(((ambient - 360.0) * 2 * M_PI) / 1440) + 1) * 0.27;
    float diffuseon = pWeather->bNight ? 0 : 1;

    uniforms.sun.direction = pOutdoor->vSunlight;
    uniforms.sun.ambient = Colorf(ambient, ambient, ambient);
    uniforms.sun.diffuse = Colorf(diffuseon * (ambient + 0.3), diffuseon * (ambient + 0.3), diffuseon * (ambient + 0.3));
    uniforms.sun.specular = Colorf(0.0f, 0.0f, 0.0f);

    // red colouring
    if (pParty->armageddon_timer) {
        uniforms.sun.ambient = Colorf(1.0f, 0.0f, 0.0f);
        uniforms.sun.diffuse = Colorf(1.0f, 0.0f, 0.0f);
        uniforms.sun.specular = Colorf(0.0f, 0.0f, 0.0f);
    }

    // Sea colouring
    if (engine->IsUnderwater()) {
        Colorf sea = colorTable.Eucalyptus.toColorf();
        uniforms.sun.ambient = Colorf(sea.r * ambient, sea.g * ambient, sea.b * ambient);
        uniforms.sun.diffuse = Colorf(sea.r * (ambient + 0.3), sea.g * (ambient + 0.3), sea.b * (ambient + 0.3));
    }

    // TODO(pskelton): this should be a separate function
    // rest of lights stacking
    int num_lights = 0;

    // get party torchlight as priority - can be radius == 0
    if (lights.mobile.uNumLightsActive >= 1) {
        const MobileLight &test = lights.mobile.pLights[0];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 2.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = Colorf(0.0f, 0.0f, 0.0f);
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    for (int i = 0; i < lights.stationary.uNumLightsActive; ++i) {
        // maximum 20 lights sent to shader at the moment
        // TODO(pskelton): make this configurable - also lights should be sorted by distance so nearest are used first
        if (num_lights >= 20) break;

        const StationaryLight &test = lights.stationary.pLights[i];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 1.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = color;
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    // mobile
    for (int i = 1; i < lights.mobile.uNumLightsActive; ++i) {
        // maximum 20 lights sent to shader at the moment
        // TODO(pskelton): make this configurable - also lights should be sorted by distance so nearest are used first
        if (num_lights >= 20) break;

        const MobileLight &test = lights.mobile.pLights[i];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 2.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = color;
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    // remaining lights are default-initialized (type = 0)
    uniforms.submit(shader);
    _submitSunShadow(shader);
    _submitHeightFog(shader, true);
    _submitAmbientOcclusion(shader);
    _submitGlobalIllumination(shader);
    _submitWater(shader, nullptr);
    if (tileMaterials) {
        glUniform1i(shader.uniformLocation("materialArray"), MATERIAL_TEXTURE_UNIT);
        glActiveTexture(GL_TEXTURE0 + MATERIAL_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tileMaterials);
    }

    // actually draw the whole terrain
    glDrawArrays(GL_TRIANGLES, 0, (127 * 127 * 6));
    drawcalls++;

    // unload
    shader.unuse();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    //end terrain debug
    if (config->debug.Terrain.value())
        // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
        if (!OpenGLES)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// TODO(pskelton): renderbase
void OpenGLRenderer::DrawOutdoorSky() {
    double rot_to_rads = ((2 * M_PI) / 2048);

    // lowers clouds as party goes up
    float  horizon_height_offset = ((double)(pCamera3D->ViewPlaneDistPixels * pCamera3D->vCameraPos.z)
        / ((double)pCamera3D->ViewPlaneDistPixels + pCamera3D->GetFarClip())
        + (double)(pViewport.center().y));

    float depth_to_far_clip = std::cos((double)pCamera3D->_viewPitch * rot_to_rads) * pCamera3D->GetFarClip();
    float height_to_far_clip = std::sin((double)pCamera3D->_viewPitch * rot_to_rads) * pCamera3D->GetFarClip();

    float bot_y_proj = ((double)(pViewport.center().y) -
        (double)pCamera3D->ViewPlaneDistPixels /
        (depth_to_far_clip + 0.0000001) *
        (height_to_far_clip - (double)pCamera3D->vCameraPos.z));

    // if ( pParty->uCurrentHour > 20 || pParty->uCurrentHour < 5 )
    // pSkyPolygon.uTileBitmapID = pOutdoor->New_SKY_NIGHT_ID;
    // else
    // pSkyPolygon.uTileBitmapID = pOutdoor->sSky_TextureID;//179(original 166)
    // pSkyPolygon.pTexture = (Texture_MM7 *)(pSkyPolygon.uTileBitmapID != -1 ?
    // (int)&pBitmaps_LOD->pTextures[pSkyPolygon.uTileBitmapID] : 0);

    if (!pOutdoor->sky_texture)
        pOutdoor->sky_texture = assets->getBitmap("plansky3"); // TODO(pskelton): do we need this?

    if (worldSpaceBillboards() || _linearPipeline) { // The world space sky fades to the horizon in its shader, the fade polygons of the screen space sky come out lighter in linear.
        if (pOutdoor->sky_texture)
            drawWorldOutdoorSky();
        return;
    }

    if (pOutdoor->sky_texture) {
        int dimming_level = (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR)? 31 : 0;
        int uNumVertices = 4;

        // plane of sky polygon rotation vector - pitch rotation around y
        float v18x = -std::sin((-pCamera3D->_viewPitch + 16) * rot_to_rads);
        float v18y = 0;
        float v18z = -std::cos((pCamera3D->_viewPitch + 16) * rot_to_rads);

        // Sky position on screen.
        //                X
        // 0._____________________________.3
        //  |8,8                    468,8 |
        //  |                             |
        //  |                             |
        // Y|                             |
        //  |                             |
        //  |8,351                468,351 |
        // 1._____________________________.2
        //
        VertexRenderList[0].vWorldViewProj.x = (double)pViewport.x;  // 8
        VertexRenderList[0].vWorldViewProj.y = (double)pViewport.y;  // 8

        VertexRenderList[1].vWorldViewProj.x = (double)pViewport.x;   // 8
        VertexRenderList[1].vWorldViewProj.y = (double)bot_y_proj + 1;  // 247

        VertexRenderList[2].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);   // 468
        VertexRenderList[2].vWorldViewProj.y = (double)bot_y_proj + 1;  // 247

        VertexRenderList[3].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);  // 468
        VertexRenderList[3].vWorldViewProj.y = (double)pViewport.y;  // 8

        float widthperpixel = 1 / pCamera3D->ViewPlaneDistPixels;

        for (unsigned i = 0; i < uNumVertices; ++i) {
            // outbound screen X dist
            float x_dist = widthperpixel * (pViewport.center().x - VertexRenderList[i].vWorldViewProj.x);
            // outbound screen y dist
            float y_dist = widthperpixel * (horizon_height_offset - VertexRenderList[i].vWorldViewProj.y);

            // rotate vectors to cam facing
            float skyfinalleft = (SkyBillboard.CamVecLeft_X * x_dist) + (SkyBillboard.CamVecLeft_Z * y_dist) + SkyBillboard.CamVecLeft_Y;
            float skyfinalfront = (SkyBillboard.CamVecFront_X * x_dist) + (SkyBillboard.CamVecFront_Z * y_dist) + SkyBillboard.CamVecFront_Y;

            // pitch rotate sky to get top
            float top_y_proj = v18x + v18y + v18z * y_dist;
            if (top_y_proj > 0.0f) top_y_proj = -0.0000001f;

            float worldviewdepth = -64.0 / top_y_proj;
            if (worldviewdepth < 0) worldviewdepth = pCamera3D->GetFarClip();

            // offset tex coords
            float texoffset_U = animTimer->time().realtimeMillisecondsFloat() + ((skyfinalleft * worldviewdepth));
            VertexRenderList[i].u = texoffset_U / ((float) pOutdoor->sky_texture->width());
            float texoffset_V = animTimer->time().realtimeMillisecondsFloat() + ((skyfinalfront * worldviewdepth));
            VertexRenderList[i].v = texoffset_V / ((float) pOutdoor->sky_texture->height());

            VertexRenderList[i].vWorldViewPosition.x = pCamera3D->GetFarClip();

            // this basically acts as texture perspective correction
            VertexRenderList[i]._rhw = (double)(worldviewdepth);
        }

        if (config->graphics.Fog.value()) {
            // fade sky
            VertexRenderList[4].vWorldViewProj.x = (double)pViewport.x;
            VertexRenderList[4].vWorldViewProj.y = (double)pViewport.y;
            VertexRenderList[5].vWorldViewProj.x = (double)pViewport.x;
            VertexRenderList[5].vWorldViewProj.y = (double)bot_y_proj - config->graphics.FogHorizon.value();
            VertexRenderList[6].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);
            VertexRenderList[6].vWorldViewProj.y = (double)bot_y_proj - config->graphics.FogHorizon.value();
            VertexRenderList[7].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);
            VertexRenderList[7].vWorldViewProj.y = (double)pViewport.y;

            // sub sky
            VertexRenderList[8].vWorldViewProj.x = (double)pViewport.x;
            VertexRenderList[8].vWorldViewProj.y = (double)bot_y_proj - config->graphics.FogHorizon.value();
            VertexRenderList[9].vWorldViewProj.x = (double)pViewport.x;
            VertexRenderList[9].vWorldViewProj.y = (double)(pViewport.y + pViewport.h - 1) + 1;
            VertexRenderList[10].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);
            VertexRenderList[10].vWorldViewProj.y = (double)(pViewport.y + pViewport.h - 1) + 1;
            VertexRenderList[11].vWorldViewProj.x = (double)(pViewport.x + pViewport.w - 1);
            VertexRenderList[11].vWorldViewProj.y = (double)bot_y_proj - config->graphics.FogHorizon.value();
        }

        _set_ortho_projection(1);
        _set_ortho_modelview();
        DrawOutdoorSkyPolygon(uNumVertices, pOutdoor->sky_texture, dimming_level);
    }
}



//----- (004A2DA3) --------------------------------------------------------
void OpenGLRenderer::DrawOutdoorSkyPolygon(int numVertices, GraphicsImage *texture, int dimmingLevel) {
    auto texid = texture->renderId().value();

    float texidsolid = static_cast<float>(solidFillTexture()->renderId().value());

    //glBindTexture(GL_TEXTURE_2D, texture->GetOpenGlTexture());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    Colorf uTint = GetActorTintColor(dimmingLevel, 0, VertexRenderList[0].vWorldViewPosition.x, 1, 0).toColorf();
    float scrspace = pCamera3D->GetFarClip();

    // load up poly
    for (int z = 0; z < (numVertices - 2); z++) {
        // 123, 134, 145, 156..
        // copy first
        ForcePerVertex &v0 = _forcePerVertices.emplace_back();
        v0.pos = Vec3f(VertexRenderList[0].vWorldViewProj.x, VertexRenderList[0].vWorldViewProj.y, 1.0f);
        v0.w = VertexRenderList[0]._rhw;
        v0.texuv = Vec2f(VertexRenderList[0].u, VertexRenderList[0].v);
        v0.texw = 1.0f;
        v0.screenspace = scrspace;
        v0.color = uTint;
        v0.texid = texid;

        // copy other two (z+1)(z+2)
        for (unsigned i = 1; i < 3; ++i) {
            ForcePerVertex &v = _forcePerVertices.emplace_back();
            v.pos = Vec3f(VertexRenderList[z + i].vWorldViewProj.x, VertexRenderList[z + i].vWorldViewProj.y, 1.0f);
            v.w = VertexRenderList[z + i]._rhw;
            v.texuv = Vec2f(VertexRenderList[z + i].u, VertexRenderList[z + i].v);
            v.texw = 1.0f;
            v.screenspace = scrspace;
            v.color = uTint;
            v.texid = texid;
        }
    }

    if (config->graphics.Fog.value()) {
        // draw blend sky
        // load up poly
        for (int z = 4; z < 6; z++) {
            // 456, 467..
            // copy first
            ForcePerVertex &v0 = _forcePerVertices.emplace_back();
            v0.pos = Vec3f(VertexRenderList[4].vWorldViewProj.x, VertexRenderList[4].vWorldViewProj.y, 1.0f);
            v0.w = 1.0f;
            v0.texuv = Vec2f(0.5f, 0.5f);
            v0.texw = 1.0f;
            v0.screenspace = scrspace;
            v0.color = uTint;
            v0.color.a = 0;
            v0.texid = texidsolid;

            // copy other two (z+1)(z+2)
            for (unsigned i = 1; i < 3; ++i) {
                ForcePerVertex &v = _forcePerVertices.emplace_back();
                v.pos = Vec3f(VertexRenderList[z + i].vWorldViewProj.x, VertexRenderList[z + i].vWorldViewProj.y, 1.0f);
                v.w = 1.0f;
                v.texuv = Vec2f(0.5f, 0.5f);
                v.texw = 1.0f;
                v.screenspace = scrspace;
                v.color = uTint;
                v.color.a = ((z + i) == 7) ? 0.0f : 1.0f;
                v.texid = texidsolid;
            }
        }

        // draw sub sky
        // load up poly
        for (int z = 8; z < 10; z++) {
            // 456, 467..
            // copy first
            ForcePerVertex &v0 = _forcePerVertices.emplace_back();
            v0.pos = Vec3f(VertexRenderList[8].vWorldViewProj.x, VertexRenderList[8].vWorldViewProj.y, 1.0f);
            v0.w = 1.0f;
            v0.texuv = Vec2f(0.5f, 0.5f);
            v0.texw = 1.0f;
            v0.screenspace = scrspace;
            v0.color = uTint;
            v0.texid = texidsolid;

            // copy other two (z+1)(z+2)
            for (unsigned i = 1; i < 3; ++i) {
                ForcePerVertex &v = _forcePerVertices.emplace_back();
                v.pos = Vec3f(VertexRenderList[z + i].vWorldViewProj.x, VertexRenderList[z + i].vWorldViewProj.y, 1.0f);
                v.w = 1.0f;
                v.texuv = Vec2f(0.5f, 0.5f);
                v.texw = 1.0f;
                v.screenspace = scrspace;
                v.color = uTint;
                v.texid = texidsolid;
            }
        }
    }

    DrawForcePerVerts();
}

void OpenGLRenderer::DrawForcePerVerts() {
    if (_forcePerVertices.empty()) return;

    if (!_forcePerBuffer) {
        _forcePerBuffer.reset(GL_DYNAMIC_DRAW,
            &ForcePerVertex::pos,
            &ForcePerVertex::w,
            &ForcePerVertex::texuv,
            &ForcePerVertex::texw,
            &ForcePerVertex::screenspace,
            &ForcePerVertex::color);
    }

    _forcePerBuffer.update(_forcePerVertices);
    _forcePerBuffer.bind();

    forcepershader.use();

    // set fog - force perspective is handled slightly differently because of the sky
    ForcePerUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.fog = forcedPerspectiveFog(_forcePerVertices[0].color.r);
    uniforms.submit(forcepershader);

    // draw all similar textures in batches
    size_t offset = 0;
    while (offset < _forcePerVertices.size()) {
        // set texture
        GLfloat thistex = _forcePerVertices[offset].texid;
        glBindTexture(GL_TEXTURE_2D, thistex);

        size_t cnt = 0;
        do {
            cnt++;
            if (offset + (3 * cnt) >= _forcePerVertices.size()) {
                break;
            }
        } while (_forcePerVertices[offset + (cnt * 3)].texid == thistex);

        glDrawArrays(GL_TRIANGLES, offset, (3 * cnt));
        drawcalls++;

        offset += (3 * cnt);
    }

    forcepershader.unuse();
    _forcePerBuffer.unbind();

    _forcePerVertices.clear();
}

FogUniforms OpenGLRenderer::forcedPerspectiveFog(float normalWeatherGrey) {
    FogUniforms result;
    if (config->graphics.Fog.value() && uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        Color fpfogcol = GetLevelFogColor();
        if (fpfogcol != Color()) {
            // Foggy weather.
            result.weakDensity = 0.25;
            result.strongDensity = 0.85;
            result.weakDistance = fog.weakDistance;
            result.strongDistance = fog.strongDistance;
            result.clipDistance = fog.clipDistance;
            result.color = _worldColor(Colorf(fpfogcol.r / 255.0f, fpfogcol.g / 255.0f, fpfogcol.b / 255.0f));
        } else {
            // Normal weather.
            result.weakDensity = 0.0f;
            result.strongDensity = 0.0f;
            result.weakDistance = pCamera3D->GetFarClip();
            result.strongDistance = pCamera3D->GetFarClip();
            result.clipDistance = pCamera3D->GetFarClip();
            result.color = _worldColor(Colorf(normalWeatherGrey, normalWeatherGrey, normalWeatherGrey));
        }
    } else {
        result.weakDensity = 0.0f;
        result.strongDensity = 0.0f;
        result.weakDistance = pCamera3D->GetFarClip();
        result.strongDistance = pCamera3D->GetFarClip();
        result.clipDistance = pCamera3D->GetFarClip();
        result.color = Colorf();
    }
    return result;
}

// TODO(pskelton): move ?
void OpenGLRenderer::SetFogParametersGL() {
    if (config->graphics.Fog.value() && uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        Color fogcol = GetLevelFogColor();

        if (fogcol != Color()) {
            fog.weakDensity = 0.25;
            fog.strongDensity = 0.85;
            fog.weakDistance = pOutdoor->weather.fogWeakDistance;
            fog.strongDistance = pOutdoor->weather.fogStrongDistance;
            fog.clipDistance = pCamera3D->GetFarClip();
            fog.color = _worldColor(fogcol.toColorf());
        } else {
            fog.weakDensity = 0;
            fog.strongDensity = 0;
            fog.weakDistance = pCamera3D->GetFarClip() * config->graphics.FogDepthRatio.value();
            fog.strongDistance = pCamera3D->GetFarClip() * (1 + config->graphics.FogDepthRatio.value()) / 2;
            fog.clipDistance = pCamera3D->GetFarClip();

            // grabs sky back fog colour
            Color uTint = GetActorTintColor(31, 0, fog.clipDistance, 1, 0);
            fog.color = _worldColor(uTint.toColorf());
        }
    } else {
        // puts fog beyond viewclip so we never see it
        fog.weakDensity = 0;
        fog.strongDensity = 0;
        fog.weakDistance = pCamera3D->GetFarClip();
        fog.strongDistance = pCamera3D->GetFarClip();
        fog.clipDistance = pCamera3D->GetFarClip();
    }
}

static constexpr float PROJECTILE_HALF_WIDTH = 20.0f; // World units on each side of a projectile beam's axis.
static constexpr float SKY_TILT = 16 * 2 * M_PI / 2048; // The forced perspective sky plane tilts by 16 of the 2048 units of a turn.
static constexpr float OUTDOOR_SKY_PLANE_HEIGHT = 64.0f;
static constexpr float INDOOR_SKY_PLANE_HEIGHT = 512.0f / 16.0f; // DrawIndoorSky intersects at 512 and scales down by 16.

//----- (004A1C1E) --------------------------------------------------------
/**
 * @param billboard                     Billboard to draw.
 * @param[out] anchor                   Point the billboard looks up the sun shadow at, halfway up its sprite.
 * @return                              Whether the billboard receives sun shadows. Only sprites drawn opaque do, glows
 *                                      and particles don't.
 */
static bool billboardShadowAnchor(const RenderBillboardD3D &billboard, Vec3f *anchor) {
    if (billboard.sParentBillboardID < 0 || billboard.opacity != RenderBillboardD3D::Transparent)
        return false;

    const RenderBillboard &parent = pBillboardRenderList[billboard.sParentBillboardID];
    if (!parent.hwsprite)
        return false;
    *anchor = parent.worldPos + Vec3f(0, 0, parent.hwsprite->uHeight * parent.scale.y / 2);
    return true;
}

/**
 * @param pos                           Point in the world.
 * @param[out] floor                    Height of the floor under `pos`.
 * @return                              Whether there's a floor under `pos`.
 */
static bool contactShadowFloor(Vec3f pos, float *floor) {
    if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
        int sector = 0;
        int face = -1;
        *floor = GetIndoorFloorZ(pos + Vec3f(0, 0, CONTACT_SHADOW_PROBE_HEIGHT), &sector, &face);
        return *floor != -30000 && face != -1;
    }
    bool onWater = false;
    int face = 0;
    *floor = ODM_GetFloorLevel(pos + Vec3f(0, 0, CONTACT_SHADOW_PROBE_HEIGHT), &onWater, &face);
    return !onWater;
}

void OpenGLRenderer::_drawContactShadows() {
    if (uCurrentlyLoadedLevelType != LEVEL_INDOOR && uCurrentlyLoadedLevelType != LEVEL_OUTDOOR)
        return;

    // The ambient light the world is lit with, as the terrain and indoor shaders compute it.
    float ambient = 0;
    if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        float minutes = pParty->uCurrentMinute + pParty->uCurrentHour * 60.0f;
        ambient = 0.15f + (std::sin((minutes - 360.0f) * 2 * static_cast<float>(M_PI) / 1440) + 1) * 0.27f;
    } else {
        ambient = (248.0f - (pIndoor->ambientLightLevel() << 3)) / 255.0f;
    }
    float strength = CONTACT_SHADOW_STRENGTH * std::sqrt(std::clamp(ambient / 0.69f, 0.0f, 1.0f)); // 0.69 is noon outdoors.
    if (_sunShadowFade > 0)
        strength *= 1.0f - (1.0f - CONTACT_SHADOW_SUNLIT_SCALE) * _sunShadowFade; // The sun already casts a shadow.

    _contactShadowVertices.clear();
    for (int i = 0; i < ::uNumBillboardsToDraw; i++) {
        const RenderBillboard &billboard = pBillboardRenderList[i];
        Vec3f foot;
        if (billboard.object_pid.type() == OBJECT_Actor) {
            foot = pActors[billboard.object_pid.id()].pos;
        } else if (billboard.object_pid.type() == OBJECT_Decoration) {
            foot = pLevelDecorations[billboard.object_pid.id()].vPosition;
        } else {
            continue;
        }
        if (!billboard.hwsprite)
            continue;

        float centreFloor = 0;
        if (!contactShadowFloor(foot, &centreFloor))
            continue;
        float height = foot.z - centreFloor;
        float fade = 1.0f - std::clamp((height - CONTACT_SHADOW_FULL_HEIGHT) / (CONTACT_SHADOW_NO_HEIGHT - CONTACT_SHADOW_FULL_HEIGHT), 0.0f, 1.0f);
        if (fade <= 0)
            continue;

        float width = std::clamp(billboard.hwsprite->uWidth * billboard.scale.x * 0.5f, CONTACT_SHADOW_MIN_SIZE, CONTACT_SHADOW_MAX_SIZE);
        Vec2f radius(width / 2, width * 0.35f);

        // A 2x2 grid of quads, each corner on the floor under it, so the ellipse follows the ground's slopes and folds.
        std::array<std::array<ContactShadowVertex, 3>, 3> grid;
        for (int gy = 0; gy < 3; gy++) {
            for (int gx = 0; gx < 3; gx++) {
                Vec2f local(gx - 1.0f, gy - 1.0f);
                Vec3f pos(foot.x + local.x * radius.x, foot.y + local.y * radius.y, centreFloor);
                float floor = 0;
                if (contactShadowFloor(pos, &floor) && std::abs(floor - centreFloor) < radius.x)
                    pos.z = floor; // Follows the slope, but not off a ledge.
                pos.z += CONTACT_SHADOW_LIFT;
                grid[gy][gx] = {pos, Vec3f(local.x, local.y, strength * fade)};
            }
        }
        for (int gy = 0; gy < 2; gy++) {
            for (int gx = 0; gx < 2; gx++) {
                std::array<ContactShadowVertex, 4> quad = {{grid[gy][gx], grid[gy][gx + 1], grid[gy + 1][gx + 1], grid[gy + 1][gx]}};
                for (int c : {0, 1, 2, 0, 2, 3})
                    _contactShadowVertices.push_back(quad[c]);
            }
        }
    }
    if (_contactShadowVertices.empty())
        return;

    if (!_contactShadowBuffer)
        _contactShadowBuffer.reset(GL_DYNAMIC_DRAW, &ContactShadowVertex::pos, &ContactShadowVertex::shape);
    _contactShadowBuffer.update(_contactShadowVertices);

    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -4.0f);

    contactshadowshader.use();
    glUniformMatrix4fv(contactshadowshader.uniformLocation("projection"), 1, GL_FALSE, &_view.projectionMatrix[0][0]);
    glUniformMatrix4fv(contactshadowshader.uniformLocation("view"), 1, GL_FALSE, &_view.viewMatrix[0][0]);
    _contactShadowBuffer.bind();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(_contactShadowVertices.size()));
    drawcalls++;
    _contactShadowBuffer.unbind();
    contactshadowshader.unuse();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(depthMask);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blend)
        glDisable(GL_BLEND);
    if (cullFace)
        glEnable(GL_CULL_FACE);
}

void OpenGLRenderer::DoRenderBillboards_D3D() {
    if (_linearPipeline)
        _drawContactShadows();

    if (worldSpaceBillboards()) {
        drawWorldBillboards();
        return;
    }

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);  // in theory billboards all sorted by depth so dont cull by depth test
    glDisable(GL_CULL_FACE);  // some quads are reversed to reuse sprites opposite hand

    _set_ortho_projection(1);
    _set_ortho_modelview();

    if (!_billboardVertices.empty())
        MM_TRACE("Billboard shader store isnt empty!");

    // track loaded tex
    float gltexid = 0;
    // track blend mode
    //RenderBillboardD3D::OpacityType blendtrack{ RenderBillboardD3D::NoBlend };

    float oneon = 1.0f / (pCamera3D->GetNearClip() * 2.0f);
    float oneof = 1.0f / (pCamera3D->GetFarClip());

    for (int i = uNumBillboardsToDraw - 1; i >= 0; --i) {
        //if (pBillboardRenderListD3D[i].opacity != RenderBillboardD3D::NoBlend) {
        //    if (blendtrack != pBillboardRenderListD3D[i].opacity) {
        //        blendtrack = pBillboardRenderListD3D[i].opacity;
        //        SetBillboardBlendOptions(blendtrack);
        //    }
        //}

        RenderBillboardD3D* billboard = pSortedBillboardRenderListD3D[i];

        //int palette{ pBillboardRenderListD3D[i].PaletteID};
        int paletteId = billboard->paletteId;
        gltexid = billboardTextureId(billboard->texture, &paletteId);

        //if (gltexid != testtexid) {
        //    gltexid = testtexid;
        //    glBindTexture(GL_TEXTURE_2D, gltexid);
        //}


        float oneoz = 1.0f / billboard->view_space_z;
        float thisdepth = (oneoz - oneon) / (oneof - oneon);

        float thisblend = static_cast<float>(billboard->opacity);

        // Triangle 1: vertices 0, 1, 2
        BillboardVertex &v0 = _billboardVertices.emplace_back();
        v0.pos = Vec3f(billboard->pQuads[0].pos.x, billboard->pQuads[0].pos.y, thisdepth);
        v0.texuv = Vec2f(std::clamp(billboard->pQuads[0].texcoord.x, 0.01f, 0.99f),
                         std::clamp(billboard->pQuads[0].texcoord.y, 0.01f, 0.99f));
        v0.color = billboard->pQuads[0].diffuse.toColorf();
        v0.screenspace = billboard->view_space_L2;
        v0.texid = gltexid;
        v0.blend = thisblend;
        v0.paletteId = paletteId;
        v0.shadowed = billboardShadowAnchor(*billboard, &v0.shadowAnchor);

        BillboardVertex &v1 = _billboardVertices.emplace_back();
        v1.pos = Vec3f(billboard->pQuads[1].pos.x, billboard->pQuads[1].pos.y, thisdepth);
        v1.texuv = Vec2f(std::clamp(billboard->pQuads[1].texcoord.x, 0.01f, 0.99f),
                         std::clamp(billboard->pQuads[1].texcoord.y, 0.01f, 0.99f));
        v1.color = billboard->pQuads[1].diffuse.toColorf();
        v1.screenspace = billboard->view_space_L2;
        v1.texid = gltexid;
        v1.blend = thisblend;
        v1.paletteId = paletteId;

        BillboardVertex &v2 = _billboardVertices.emplace_back();
        v2.pos = Vec3f(billboard->pQuads[2].pos.x, billboard->pQuads[2].pos.y, thisdepth);
        v2.texuv = Vec2f(std::clamp(billboard->pQuads[2].texcoord.x, 0.01f, 0.99f),
                         std::clamp(billboard->pQuads[2].texcoord.y, 0.01f, 0.99f));
        v2.color = billboard->pQuads[2].diffuse.toColorf();
        v2.screenspace = billboard->view_space_L2;
        v2.texid = gltexid;
        v2.blend = thisblend;
        v2.paletteId = paletteId;

        // Triangle 2: vertices 0, 2, 3 (if quad has 4 vertices)
        if (billboard->pQuads[3].pos.x != 0.0f && billboard->pQuads[3].pos.y != 0.0f && billboard->pQuads[3].pos.z != 0.0f) {
            BillboardVertex &v3 = _billboardVertices.emplace_back();
            v3.pos = Vec3f(billboard->pQuads[0].pos.x, billboard->pQuads[0].pos.y, thisdepth);
            v3.texuv = Vec2f(std::clamp(billboard->pQuads[0].texcoord.x, 0.01f, 0.99f),
                             std::clamp(billboard->pQuads[0].texcoord.y, 0.01f, 0.99f));
            v3.color = billboard->pQuads[0].diffuse.toColorf();
            v3.screenspace = billboard->view_space_L2;
            v3.texid = gltexid;
            v3.blend = thisblend;
            v3.paletteId = paletteId;

            BillboardVertex &v4 = _billboardVertices.emplace_back();
            v4.pos = Vec3f(billboard->pQuads[2].pos.x, billboard->pQuads[2].pos.y, thisdepth);
            v4.texuv = Vec2f(std::clamp(billboard->pQuads[2].texcoord.x, 0.01f, 0.99f),
                             std::clamp(billboard->pQuads[2].texcoord.y, 0.01f, 0.99f));
            v4.color = billboard->pQuads[2].diffuse.toColorf();
            v4.screenspace = billboard->view_space_L2;
            v4.texid = gltexid;
            v4.blend = thisblend;
            v4.paletteId = paletteId;

            BillboardVertex &v5 = _billboardVertices.emplace_back();
            v5.pos = Vec3f(billboard->pQuads[3].pos.x, billboard->pQuads[3].pos.y, thisdepth);
            v5.texuv = Vec2f(std::clamp(billboard->pQuads[3].texcoord.x, 0.01f, 0.99f),
                             std::clamp(billboard->pQuads[3].texcoord.y, 0.01f, 0.99f));
            v5.color = billboard->pQuads[3].diffuse.toColorf();
            v5.screenspace = billboard->view_space_L2;
            v5.texid = gltexid;
            v5.blend = thisblend;
            v5.paletteId = paletteId;
        }
    }

    DrawBillboards();

    //glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

float OpenGLRenderer::billboardTextureId(GraphicsImage *texture, int *paletteId) {
    if (texture) {
        if (GraphicsImage *variant = assets->getSpriteVariant(texture, *paletteId)) {
            texture = variant;
            *paletteId = 0; // The variant has the palette applied.
        }
        return texture->renderId().value();
    }

    // TODO(captainurist): function-local static caching an asset, make it a member.
    static GraphicsImage *effpar03 = assets->getBitmap("effpar03");
    return static_cast<float>(effpar03->renderId().value());
}

void OpenGLRenderer::_copySceneDepth() {
    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    Sizei size(viewport[2], viewport[3]);

    // A world target's depth is a texture that can be read directly, at half the resolution the soft edges need.
    OpenGLRenderTarget *world = _currentWorldTarget();
    if (world && world->hasDepth()) {
        Sizei half(std::max(1, size.w / 2), std::max(1, size.h / 2));
        if (_sceneDepthHalf.size() != half)
            _sceneDepthHalf.reset(half, RENDER_TARGET_FORMAT_R32F, false);
        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean blend = glIsEnabled(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        _sceneDepthHalf.bind();
        glViewport(0, 0, half.w, half.h);
        _ensureFullscreenTriangle();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, world->depthTexture());
        depthhalfshader.use();
        glUniform1i(depthhalfshader.uniformLocation("depthMap"), 0);
        glUniform2i(depthhalfshader.uniformLocation("sourceOrigin"), viewport[0], viewport[1]);
        _compositeBuffer.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        drawcalls++;
        _compositeBuffer.unbind();
        depthhalfshader.unuse();
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (scissor)
            glEnable(GL_SCISSOR_TEST);
        if (blend)
            glEnable(GL_BLEND);

        _sceneDepthViewport = Recti(viewport[0], viewport[1], size.w, size.h);
        _sceneDepthTexture = _sceneDepthHalf.colorTexture();
        _sceneDepthCopied = true;
        return;
    }
    if (!_sceneDepth || _sceneDepthSize != size) {
        if (!_sceneDepth) {
            glGenTextures(1, &_sceneDepth);
            glGenFramebuffers(1, &_sceneDepthFramebuffer);
        }
        _sceneDepthSize = size;
        glBindTexture(GL_TEXTURE_2D, _sceneDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.w, size.h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        GLenum none = GL_NONE;
        glBindFramebuffer(GL_FRAMEBUFFER, _sceneDepthFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _sceneDepth, 0);
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            MM_ERROR("Scene depth framebuffer is incomplete.");
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }

    // The billboards can't sample the depth buffer they're tested against, so they get a copy.
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _sceneDepthFramebuffer);
    glBlitFramebuffer(viewport[0], viewport[1], viewport[0] + size.w, viewport[1] + size.h, 0, 0, size.w, size.h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);

    _sceneDepthViewport = Recti(viewport[0], viewport[1], size.w, size.h);
    _sceneDepthTexture = _sceneDepth;
    _sceneDepthCopied = true;
}

void OpenGLRenderer::drawWorldBillboards() {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); // Mirrored quads are wound the other way.

    _set_3d_projection_matrix();
    _set_3d_modelview_matrix();

    if (!_worldBillboardQuadsBuilt)
        buildWorldBillboardQuads(_view, SPRITE_FACING_VIEW_PLANE);
    bool additive = false;
    for (int i = 0; i < uNumBillboardsToDraw && !additive; i++)
        additive = _worldBillboards[i].opacity != RenderBillboardD3D::Transparent;

    auto addBillboard = [&](int index) {
        const WorldBillboard &billboard = _worldBillboards[index];

        int paletteId = billboard.paletteId;
        float texid = billboardTextureId(billboard.texture, &paletteId);
        int crossfadePaletteId = billboard.paletteId;
        float crossfadeTexid = billboard.crossfadeTexture ? billboardTextureId(billboard.crossfadeTexture, &crossfadePaletteId) : texid;

        auto addVertex = [&](int j) {
            BillboardVertex &vertex = _billboardVertices.emplace_back();
            vertex.pos = billboard.corners[j];
            if (billboard.crossfadeTexture) {
                vertex.texuv = billboard.texcoords[j]; // The shader cuts each side off outside its own image.
                vertex.crossfadeTexuv = billboard.crossfadeTexcoords[j];
                vertex.crossfade = billboard.crossfade;
                vertex.crossfadePaletteId = crossfadePaletteId;
            } else {
                vertex.texuv = Vec2f(std::clamp(billboard.texcoords[j].x, 0.01f, 0.99f), std::clamp(billboard.texcoords[j].y, 0.01f, 0.99f));
            }
            vertex.crossfadeTexid = crossfadeTexid;
            vertex.color = billboard.colors[j].toColorf();
            vertex.screenspace = billboard.distance;
            vertex.texid = texid;
            vertex.blend = static_cast<float>(billboard.opacity);
            vertex.paletteId = paletteId;
            vertex.shadowed = billboardShadowAnchor(pBillboardRenderListD3D[index], &vertex.shadowAnchor);
            if (billboard.shape) {
                vertex.texuv = billboard.texcoords[j]; // The parallax can move the lookup off the image.
                vertex.shapeRight = billboard.right;
                vertex.shapeSize = Vec2f(billboard.sideExtent.x * 2, billboard.sideExtent.y);
                vertex.shapeThickness = billboard.shapeThickness;
                vertex.shapeTexid = static_cast<float>(billboard.shape->renderId().value());
            }
        };

        addVertex(0);
        addVertex(1);
        addVertex(2);
        if (billboard.numVertices == 4) {
            addVertex(0);
            addVertex(2);
            addVertex(3);
        }
        worldBillboardsDrawn++;
    };

    // Sprites with shapes write their depth, so they go first, the depth of their opaque parts before their colour.
    std::vector<bool> drawn(uNumBillboardsToDraw, false);
    if (_spriteDepth) {
        for (int i = uNumBillboardsToDraw - 1; i >= 0; --i) {
            int index = pSortedBillboardRenderListD3D[i] - pBillboardRenderListD3D;
            if (_worldBillboards[index].shape && _worldBillboards[index].opacity == RenderBillboardD3D::Transparent) {
                addBillboard(index);
                drawn[index] = true;
            }
        }
        if (!_billboardVertices.empty()) {
            std::vector<BillboardVertex> shapeVertices = _billboardVertices;
            _billboardDepthPrepass = true;
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            DrawBillboards();
            _billboardDepthPrepass = false;
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            _billboardVertices = std::move(shapeVertices);
            DrawBillboards();
        }
    }

    if (_linearPipeline && additive)
        _copySceneDepth();

    for (int i = uNumBillboardsToDraw - 1; i >= 0; --i) {
        int index = pSortedBillboardRenderListD3D[i] - pBillboardRenderListD3D;
        if (!drawn[index])
            addBillboard(index);
    }

    DrawBillboards();

    glDepthMask(GL_TRUE);
}

void OpenGLRenderer::drawWorldProjectile(Vec3f src, Vec3f dst, GraphicsImage *texture) {
    // Across the beam and across the line of sight, so that the beam is seen at its full width.
    Vec3f eye = _worldBillboardQuadsBuilt ? _worldBillboardEye : _view.position;
    Vec3f side = cross(eye - (src + dst) / 2, dst - src);
    float sideLength = side.length();
    if (sideLength == 0)
        return;
    side *= PROJECTILE_HALF_WIDTH / sideLength;

    std::array<Vec3f, 4> corners = {{src + side, dst + side, dst - side, src - side}};
    std::array<Vec2f, 4> texcoords = {{{1, 0}, {1, 1}, {0, 1}, {0, 0}}};
    float texid = texture ? texture->renderId().value() : 0;
    for (int j : {0, 1, 2, 0, 2, 3}) {
        BillboardVertex &vertex = _billboardVertices.emplace_back();
        vertex.pos = corners[j];
        vertex.texuv = texcoords[j];
        vertex.color = colorTable.White.toColorf();
        vertex.screenspace = (corners[j] - eye).length();
        vertex.texid = texid;
        vertex.blend = static_cast<float>(RenderBillboardD3D::Opaque_1);
        vertex.paletteId = 0;
    }

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    _set_3d_projection_matrix();
    _set_3d_modelview_matrix();
    DrawBillboards();

    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

/**
 * @param texture                       Sky texture.
 * @return                              Offset of the sky animation in texels, wrapped to the texture size.
 */
static Vec2f skyTimeOffset(GraphicsImage *texture) {
    float time = animTimer->time().realtimeMillisecondsFloat();
    return Vec2f(std::fmod(time, static_cast<float>(texture->width())), std::fmod(time, static_cast<float>(texture->height())));
}

void OpenGLRenderer::drawWorldOutdoorSky() {
    GraphicsImage *texture = pOutdoor->sky_texture;
    Colorf tint = GetActorTintColor(31, 0, pCamera3D->GetFarClip(), 1, 0).toColorf();
    Vec2f timeOffset = skyTimeOffset(texture);

    // Corners of the view at a depth well inside the far plane, the shader only needs the direction to each pixel.
    glm::mat4 inverse = glm::inverse(_view.projectionMatrix * _view.viewMatrix);
    std::array<glm::vec2, 4> ndc = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    std::array<Vec3f, 4> corners;
    for (int i = 0; i < 4; i++) {
        glm::vec4 corner = inverse * glm::vec4(ndc[i], 0.999f, 1.0f);
        corners[i] = Vec3f(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
    }

    for (int i : {0, 1, 2, 0, 2, 3}) {
        SkyVertex &vertex = _skyVertices.emplace_back();
        vertex.pos = corners[i];
        vertex.color = tint;
        vertex.texsize = Vec2f(texture->width(), texture->height());
        vertex.timeoffset = timeOffset;
        vertex.texid = texture->renderId().value();
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    drawSkyVertices(OUTDOOR_SKY_PLANE_HEIGHT, config->graphics.Fog.value());
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void OpenGLRenderer::queueWorldIndoorSky(BLVFace *face) {
    GraphicsImage *texture = face->GetTexture();
    float depth = pCamera3D->ViewTransform(&pIndoor->vertices[face->vertexIds[0]]).x;
    Colorf tint = GetActorTintColor(0, 0, depth, 1, 0).toColorf();
    Vec2f timeOffset = skyTimeOffset(texture);

    for (int z = 0; z + 2 < static_cast<int>(face->vertexIds.size()); z++) {
        for (int i : {0, z + 1, z + 2}) {
            SkyVertex &vertex = _skyVertices.emplace_back();
            vertex.pos = pIndoor->vertices[face->vertexIds[i]];
            vertex.color = tint;
            vertex.texsize = Vec2f(texture->width(), texture->height());
            vertex.timeoffset = timeOffset;
            vertex.texid = texture->renderId().value();
        }
    }
}

void OpenGLRenderer::drawSkyVertices(float planeHeight, bool fade) {
    if (_skyVertices.empty())
        return;

    if (!_skyBuffer) {
        _skyBuffer.reset(GL_DYNAMIC_DRAW,
            &SkyVertex::pos,
            &SkyVertex::color,
            &SkyVertex::texsize,
            &SkyVertex::timeoffset,
            &SkyVertex::texid);
    }

    _skyBuffer.update(_skyVertices);
    _skyBuffer.bind();

    skyshader.use();

    // These follow DrawOutdoorSky and DrawIndoorSky, with the screen rows they use measured on a level view.
    float viewPlaneDist = pCamera3D->levelViewPlaneDistPixels();
    float farClip = pCamera3D->GetFarClip();
    float cameraZ = pCamera3D->vCameraPos.z;

    SkyUniforms uniforms;
    uniforms.projection = _view.projectionMatrix;
    uniforms.view = _view.viewMatrix;
    uniforms.eye = _view.position;
    uniforms.planeHeight = planeHeight / std::cos(SKY_TILT);
    uniforms.elevationBias = std::tan(SKY_TILT) + cameraZ / (viewPlaneDist + farClip);
    uniforms.fade = fade;
    uniforms.fadeTop = (pViewport.center().y - pViewport.y) / viewPlaneDist;
    uniforms.fadeBottom = config->graphics.FogHorizon.value() / viewPlaneDist - cameraZ / farClip;
    uniforms.horizon = -cameraZ / farClip - 1 / viewPlaneDist;
    uniforms.fog = forcedPerspectiveFog(_skyVertices[0].color.r);
    uniforms.fogDistance = farClip;
    uniforms.submit(skyshader);
    _submitHeightFog(skyshader, true);
    if (skyshader.hasDefine("LINEAR_PIPELINE")) {
        SkyState sky = _skyState();
        glUniform3f(skyshader.uniformLocation("sunDirection"), sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z);
        glUniform1f(skyshader.uniformLocation("sunVisibility"), sky.sunVisibility);
    }

    GLboolean blend = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);

    size_t offset = 0;
    while (offset < _skyVertices.size()) {
        float texid = _skyVertices[offset].texid;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texid));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        size_t count = 0;
        while (offset + count < _skyVertices.size() && _skyVertices[offset + count].texid == texid)
            count += 3;

        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(offset), static_cast<GLsizei>(count));
        drawcalls++;
        offset += count;
    }

    if (blend)
        glEnable(GL_BLEND);

    skyshader.unuse();
    _skyBuffer.unbind();
    _skyVertices.clear();
}

void OpenGLRenderer::ensurePaletteTexture() {
    if (paltex2D != 0)
        return;

    std::vector<Color> palettes;
    palettes.reserve(PALETTE_COUNT * 256);
    for (int paletteId = 0; paletteId < PALETTE_COUNT; paletteId++)
        std::ranges::copy(engine->resources()->palette(paletteId).colors, std::back_inserter(palettes));

    glActiveTexture(GL_TEXTURE0 + PALETTE_TEXTURE_UNIT);
    glGenTextures(1, &paltex2D);
    glBindTexture(GL_TEXTURE_2D, paltex2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, PALETTE_COUNT, 0, GL_RGBA, GL_UNSIGNED_BYTE, palettes.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

// name better
void OpenGLRenderer::DrawBillboards() {
    if (_billboardVertices.empty()) return;

    if (!_billboardBuffer) {
        _billboardBuffer.reset(GL_DYNAMIC_DRAW,
            &BillboardVertex::pos,
            &BillboardVertex::texuv,
            &BillboardVertex::color,
            &BillboardVertex::screenspace,
            &BillboardVertex::texid,
            &BillboardVertex::paletteId,
            &BillboardVertex::shadowAnchor,
            &BillboardVertex::shadowed,
            &BillboardVertex::crossfadeTexuv,
            &BillboardVertex::crossfade,
            &BillboardVertex::crossfadePaletteId,
            &BillboardVertex::shapeRight,
            &BillboardVertex::shapeSize,
            &BillboardVertex::shapeThickness);
    }

    ensurePaletteTexture();

    _billboardBuffer.update(_billboardVertices);
    _billboardBuffer.bind();

    billbshader.use();

    // set sampler to palette
    glActiveTexture(GL_TEXTURE0 + PALETTE_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, paltex2D);

    glActiveTexture(GL_TEXTURE0);

    BillboardUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.fog = fog;
    uniforms.gamma = gamma;
    uniforms.paltex2D = PALETTE_TEXTURE_UNIT;
    uniforms.submit(billbshader);
    _submitHeightFog(billbshader, worldSpaceBillboards());
    _submitGlobalIllumination(billbshader);
    bool soft = _sceneDepthCopied && worldSpaceBillboards();
    if (billbshader.hasDefine("LINEAR_PIPELINE")) {
        glActiveTexture(GL_TEXTURE0 + SCENE_DEPTH_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, _sceneDepthCopied ? _sceneDepthTexture : 0);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(billbshader.uniformLocation("sceneDepth"), SCENE_DEPTH_TEXTURE_UNIT);
        glUniform4f(billbshader.uniformLocation("sceneViewport"), _sceneDepthViewport.x, _sceneDepthViewport.y, _sceneDepthViewport.w, _sceneDepthViewport.h);
        glUniform2f(billbshader.uniformLocation("sceneProjection"), projmat[2][2], projmat[3][2]);
    }
    if (billbshader.hasDefine("SUN_SHADOWS")) {
        _submitSunShadow(billbshader);
        glUniform3f(billbshader.uniformLocation("sunShadowDirection"), _sunShadowDirection.x, _sunShadowDirection.y, _sunShadowDirection.z);
        glUniform1f(billbshader.uniformLocation("sunShadowBillboardRatio"), sunShadowBillboardRatio());
    }

    bool crossfade = billbshader.hasDefine("SPRITE_CROSSFADE");
    if (crossfade)
        glUniform1i(billbshader.uniformLocation("texture1"), CROSSFADE_TEXTURE_UNIT);
    bool shapes = billbshader.hasDefine("SPRITE_DEPTH");
    if (shapes) {
        glUniform1i(billbshader.uniformLocation("spriteShape"), SPRITE_SHAPE_TEXTURE_UNIT);
        glUniform3f(billbshader.uniformLocation("eyePosition"), _view.position.x, _view.position.y, _view.position.z);
        Vec3f origin = _worldBillboardQuadsBuilt ? _worldBillboardEye : _view.position; // The head, where the quads were built.
        glUniform3f(billbshader.uniformLocation("parallaxOrigin"), origin.x, origin.y, origin.z);
        glUniform1i(billbshader.uniformLocation("depthPrepass"), _billboardDepthPrepass);
    }
    if (billbshader.hasDefine("SPRITE_LIGHTING"))
        _spriteLights.submit(billbshader);

    auto setFilter = [&](GLuint texture, bool paletted) {
        if (paletted) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else if (_mipmappedTextures.contains(texture)) {
            setMipmappedFilter(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
    };

    size_t offset = 0;
    while (offset < _billboardVertices.size()) {
        // set texture
        GLfloat thistex = _billboardVertices[offset].texid;
        GLfloat thiscrossfadetex = _billboardVertices[offset].crossfadeTexid;
        GLfloat thisshapetex = _billboardVertices[offset].shapeTexid;
        if (shapes) {
            glActiveTexture(GL_TEXTURE0 + SPRITE_SHAPE_TEXTURE_UNIT);
            glBindTexture(GL_TEXTURE_2D, thisshapetex ? static_cast<GLuint>(thisshapetex) : static_cast<GLuint>(thistex));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glActiveTexture(GL_TEXTURE0);
        }
        if (crossfade) {
            glActiveTexture(GL_TEXTURE0 + CROSSFADE_TEXTURE_UNIT);
            glBindTexture(GL_TEXTURE_2D, thiscrossfadetex ? static_cast<GLuint>(thiscrossfadetex) : static_cast<GLuint>(thistex));
            setFilter(static_cast<GLuint>(thiscrossfadetex), _billboardVertices[offset].crossfadePaletteId != 0);
            glActiveTexture(GL_TEXTURE0);
        }
        glBindTexture(GL_TEXTURE_2D, _billboardVertices[offset].texid);
        setFilter(static_cast<GLuint>(_billboardVertices[offset].texid), _billboardVertices[offset].paletteId != 0);

        GLfloat thisblend = _billboardVertices[offset].blend;
        if (thisblend == 0.0) {
            // disable alpha blending and enable fog for opaque items
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (_linearPipeline) {
                glUniform1f(billbshader.uniformLocation("additivegain"), 1.0f);
                glUniform1f(billbshader.uniformLocation("softDistance"), 0.0f);
            }
            glUniform1f(billbshader.uniformLocation("fog.weakDistance"), GLfloat(fog.weakDistance));
            glUniform1f(billbshader.uniformLocation("fog.strongDistance"), GLfloat(fog.strongDistance));
        } else {
            // enable blending and disable fog for transparent items
            glBlendFunc(GL_ONE, GL_ONE);
            if (_linearPipeline) {
                glUniform1f(billbshader.uniformLocation("additivegain"), LINEAR_ADDITIVE_GAIN);
                glUniform1f(billbshader.uniformLocation("softDistance"), soft ? SOFT_PARTICLE_DISTANCE : 0.0f);
            }
            glUniform1f(billbshader.uniformLocation("fog.weakDistance"), GLfloat(fog.clipDistance));
            glUniform1f(billbshader.uniformLocation("fog.strongDistance"), GLfloat(fog.clipDistance));
        }


        size_t cnt = 0;
        do {
            cnt++;
            if (offset + (3 * cnt) >= _billboardVertices.size()) {
                break;
            }
        } while (_billboardVertices[offset + (cnt * 3)].texid == thistex && _billboardVertices[offset + (cnt * 3)].blend == thisblend &&
                 _billboardVertices[offset + (cnt * 3)].crossfadeTexid == thiscrossfadetex && _billboardVertices[offset + (cnt * 3)].shapeTexid == thisshapetex);

        glDrawArrays(GL_TRIANGLES, offset, (3 * cnt));
        drawcalls++;

        offset += (3 * cnt);
    }

    billbshader.unuse();
    _billboardBuffer.unbind();
    _billboardVertices.clear();
}

//----- (004A1DA8) --------------------------------------------------------
void OpenGLRenderer::SetBillboardBlendOptions(RenderBillboardD3D::OpacityType a1) {
    return;
}

void OpenGLRenderer::SetUIClipRect(const Recti &rect) {
    this->clipRect = rect;
    Recti pixels = _uiPixels(rect);
    glScissor(pixels.x, pixels.y, pixels.w, pixels.h);
}

void OpenGLRenderer::ResetUIClipRect() {
    this->SetUIClipRect(_uiCard ? _uiCard->screenRect : Recti(Pointi(0, 0), outputRender));
}

void OpenGLRenderer::BeginScene2D() {
    // Setup for 2D

    if (_uiCard || _usesUiTarget())
        _bindUiTarget();

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    _set_ortho_projection();
    _set_ortho_modelview();
}

void OpenGLRenderer::DrawQuad2D(GraphicsImage *texture, const Recti &srcRect, const Recti &dstRect, Color color) {
    if (!texture) {
        MM_TRACE("Null texture passed to DrawQuad2D");
        return;
    }

    if (engine->callObserver)
        engine->callObserver->notify(CALL_DRAW_2D_TEXTURE, texture->name());

    // Early out if destination is completely outside render area. A card can reach past it, its clip rectangle limits it.
    if (!_uiCard && (dstRect.x >= outputRender.w || dstRect.y >= outputRender.h))
        return;

    // Clip destination rect against clip rect.
    Recti clippedDst = dstRect & this->clipRect;
    if (clippedDst.isEmpty())
        return;

    // Calculate how much was clipped from each side as a fraction of original size.
    float leftClip = (clippedDst.x - dstRect.x) / float(dstRect.w);
    float topClip = (clippedDst.y - dstRect.y) / float(dstRect.h);
    float rightClip = (clippedDst.x + clippedDst.w - dstRect.x) / float(dstRect.w);
    float bottomClip = (clippedDst.y + clippedDst.h - dstRect.y) / float(dstRect.h);

    // Convert srcRect to UV coordinates and apply proportional clipping.
    float texWidth = texture->width();
    float texHeight = texture->height();
    float srcU1 = srcRect.x / texWidth;
    float srcV1 = srcRect.y / texHeight;
    float srcU2 = (srcRect.x + srcRect.w) / texWidth;
    float srcV2 = (srcRect.y + srcRect.h) / texHeight;

    // Interpolate UVs based on clipping.
    float u1 = srcU1 + (srcU2 - srcU1) * leftClip;
    float v1 = srcV1 + (srcV2 - srcV1) * topClip;
    float u2 = srcU1 + (srcU2 - srcU1) * rightClip;
    float v2 = srcV1 + (srcV2 - srcV1) * bottomClip;

    Colorf cf = color.toColorf();
    int gltexid = texture->renderId().value();

    // Images without a higher resolution replacement stay sharp when the UI scale magnifies them a whole number of times.
    bool nearest = !_uiCard && texture->textureScale() == 1 && _uiScale > 1 && srcRect.w > 0 && srcRect.h > 0 &&
                   dstRect.w * _uiScale % srcRect.w == 0 && dstRect.h * _uiScale % srcRect.h == 0 && dstRect.w * _uiScale > srcRect.w;

    float drawx = clippedDst.x;
    float drawy = clippedDst.y;
    float drawz = clippedDst.x + clippedDst.w;
    float draww = clippedDst.y + clippedDst.h;

    // Triangle 1: top-left, top-right, bottom-right
    TwoDVertex &vert0 = _twodVertices.emplace_back();
    vert0.pos = Vec3f(drawx, drawy, 0);
    vert0.texuv = Vec2f(u1, v1);
    vert0.color = cf;
    vert0.texid = gltexid;
    vert0.paletteid = 0;
    vert0.nearest = nearest;

    TwoDVertex &vert1 = _twodVertices.emplace_back();
    vert1.pos = Vec3f(drawz, drawy, 0);
    vert1.texuv = Vec2f(u2, v1);
    vert1.color = cf;
    vert1.texid = gltexid;
    vert1.paletteid = 0;
    vert1.nearest = nearest;

    TwoDVertex &vert2 = _twodVertices.emplace_back();
    vert2.pos = Vec3f(drawz, draww, 0);
    vert2.texuv = Vec2f(u2, v2);
    vert2.color = cf;
    vert2.texid = gltexid;
    vert2.paletteid = 0;
    vert2.nearest = nearest;

    // Triangle 2: top-left, bottom-right, bottom-left
    TwoDVertex &vert3 = _twodVertices.emplace_back();
    vert3.pos = Vec3f(drawx, drawy, 0);
    vert3.texuv = Vec2f(u1, v1);
    vert3.color = cf;
    vert3.texid = gltexid;
    vert3.paletteid = 0;
    vert3.nearest = nearest;

    TwoDVertex &vert4 = _twodVertices.emplace_back();
    vert4.pos = Vec3f(drawz, draww, 0);
    vert4.texuv = Vec2f(u2, v2);
    vert4.color = cf;
    vert4.texid = gltexid;
    vert4.paletteid = 0;
    vert4.nearest = nearest;

    TwoDVertex &vert5 = _twodVertices.emplace_back();
    vert5.pos = Vec3f(drawx, draww, 0);
    vert5.texuv = Vec2f(u1, v2);
    vert5.color = cf;
    vert5.texid = gltexid;
    vert5.paletteid = 0;
    vert5.nearest = nearest;
}

void OpenGLRenderer::BeginTextNew(GraphicsImage *texture) {
    // draw any images in buffer
    if (!_twodVertices.empty()) {
        DrawTwodVerts();
    }

    // if we are changing font draw whats in the text buffer
    if (texture->renderId().value() != _textTexture) {
        EndTextNew();
    }

    _textTexture = texture->renderId().value();
    _textAtlasSize = texture->size();
}

void OpenGLRenderer::EndTextNew() {
    if (_textVertices.empty()) return;

    if (!_twodVertices.empty()) {
        DrawTwodVerts();
    }

    if (!_textBuffer) {
        _textBuffer.reset(GL_DYNAMIC_DRAW,
            &TextVertex::pos,
            &TextVertex::texuv,
            arrayElement(&TextVertex::colors, 0),
            arrayElement(&TextVertex::colors, 1),
            arrayElement(&TextVertex::colors, 2),
            arrayElement(&TextVertex::colors, 3));
    }

    _textBuffer.update(_textVertices);
    _textBuffer.bind();

    textshader.use();

    glEnable(GL_BLEND);
    _setUiBlendFunc();

    TextUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.submit(textshader);

    // set texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _textTexture);

    glDrawArrays(GL_TRIANGLES, 0, _textVertices.size());
    drawcalls++;

    textshader.unuse();
    _textBuffer.unbind();

    glBindTexture(GL_TEXTURE_2D, 0);

    _textVertices.clear();
}

void OpenGLRenderer::DrawTextNew(const Recti &srcRect, const Recti &dstRect, const TextColors &colors) {
    std::array<Colorf, 4> cf;
    for (size_t i = 0; i < cf.size(); i++) {
        cf[i] = colors[i].toColorf();
        // not 100% sure why this is required but it is
        if (cf[i].r == 0.0f)
            cf[i].r = 0.00392f;
    }

    // check bounds
    if (dstRect.x >= outputRender.w || dstRect.y >= outputRender.h)
        return;

    // check for overlap
    if (!dstRect.intersects(this->clipRect))
        return;

    float drawx = static_cast<float>(dstRect.x);
    float drawy = static_cast<float>(dstRect.y);
    float drawz = static_cast<float>(dstRect.x + dstRect.w);
    float draww = static_cast<float>(dstRect.y + dstRect.h);

    float texx = static_cast<float>(srcRect.x) / _textAtlasSize.w;
    float texy = static_cast<float>(srcRect.y) / _textAtlasSize.h;
    float texz = static_cast<float>(srcRect.x + srcRect.w) / _textAtlasSize.w;
    float texw = static_cast<float>(srcRect.y + srcRect.h) / _textAtlasSize.h;

    // Triangle 1: top-left, top-right, bottom-right
    TextVertex &vert0 = _textVertices.emplace_back();
    vert0.pos = Vec3f(drawx, drawy, 0);
    vert0.texuv = Vec2f(texx, texy);
    vert0.colors = cf;

    TextVertex &vert1 = _textVertices.emplace_back();
    vert1.pos = Vec3f(drawz, drawy, 0);
    vert1.texuv = Vec2f(texz, texy);
    vert1.colors = cf;

    TextVertex &vert2 = _textVertices.emplace_back();
    vert2.pos = Vec3f(drawz, draww, 0);
    vert2.texuv = Vec2f(texz, texw);
    vert2.colors = cf;

    // Triangle 2: top-left, bottom-right, bottom-left
    TextVertex &vert3 = _textVertices.emplace_back();
    vert3.pos = Vec3f(drawx, drawy, 0);
    vert3.texuv = Vec2f(texx, texy);
    vert3.colors = cf;

    TextVertex &vert4 = _textVertices.emplace_back();
    vert4.pos = Vec3f(drawz, draww, 0);
    vert4.texuv = Vec2f(texz, texw);
    vert4.colors = cf;

    TextVertex &vert5 = _textVertices.emplace_back();
    vert5.pos = Vec3f(drawx, draww, 0);
    vert5.texuv = Vec2f(texx, texw);
    vert5.colors = cf;
}

void OpenGLRenderer::flushAndScale() {
    // flush any undrawn items
    DrawTwodVerts();
    EndLines2D();
    EndTextNew();

    if (_usesUiTarget()) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);

        glViewport(0, 0, outputPresent.w, outputPresent.h);
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float ratio_width = (float)outputPresent.w / outputRender.w;
        float ratio_height = (float)outputPresent.h / outputRender.h;
        float ratio = std::min(ratio_width, ratio_height);

        float w = outputRender.w * ratio;
        float h = outputRender.h * ratio;

        Recti rect;
        rect.x = (float)outputPresent.w / 2 - w / 2;
        rect.y = (float)outputPresent.h / 2 - h / 2;
        rect.w = w;
        rect.h = h;

        if (_uiLayer) {
            _presentUiLayer(rect);
            return;
        }

        GLenum filter = config->graphics.RenderFilter.value() == 1 ? GL_LINEAR : GL_NEAREST;
        Sizei uiSize = uiTargetSize();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _uiTarget.framebuffer());
        glBlitFramebuffer(0, 0, uiSize.w, uiSize.h, rect.x, rect.y, rect.w + rect.x, rect.h + rect.y, GL_COLOR_BUFFER_BIT, filter);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
}

void OpenGLRenderer::_presentUiLayer(const Recti &rect) {
    float scale = static_cast<float>(rect.w) / outputRender.w;
    if (_mirrorPresentPending) {
        int bottom = outputRender.h - (pViewport.y + pViewport.h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _mirrorTarget.framebuffer());
        glBlitFramebuffer(0, 0, _mirrorTarget.size().w, _mirrorTarget.size().h,
                          rect.x + pViewport.x * scale, rect.y + bottom * scale,
                          rect.x + (pViewport.x + pViewport.w) * scale, rect.y + (bottom + pViewport.h) * scale, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        _mirrorPresentPending = false;
    }

    // Window coordinates with y up, the way the UI target stores its rows.
    std::array<Vec2f, 4> corners = {{Vec2f(rect.x, rect.y), Vec2f(rect.x + rect.w, rect.y), Vec2f(rect.x + rect.w, rect.y + rect.h), Vec2f(rect.x, rect.y + rect.h)}};
    std::array<Vec2f, 4> texcoords = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (int i : {0, 1, 2, 0, 2, 3}) {
        TwoDVertex &vertex = _twodVertices.emplace_back();
        vertex.pos = Vec3f(corners[i].x, corners[i].y, 0);
        vertex.texuv = texcoords[i];
        vertex.color = colorTable.White.toColorf();
        vertex.texid = _uiTarget.colorTexture();
        vertex.paletteid = 0;
    }

    projmat = glm::ortho(0.0f, static_cast<float>(outputPresent.w), 0.0f, static_cast<float>(outputPresent.h), -1.0f, 1.0f);
    _set_ortho_modelview();
    _drawTwodVerts(true);
}

void OpenGLRenderer::swapBuffers() {
    if (_usesUiTarget()) {
        glEnable(GL_SCISSOR_TEST);
        _setUiViewport();
    }

    openGLContext->swapBuffers();

    if (!_texturesForDeletion.empty()) {
        glDeleteTextures(_texturesForDeletion.size(), _texturesForDeletion.data());
        for (GLuint id : _texturesForDeletion)
            _mipmappedTextures.erase(id);
        _texturesForDeletion.clear();
    }

    if (!_externalFramePacing && config->graphics.FPSLimit.value() > 0)
        _frameLimiter.tick(config->graphics.FPSLimit.value());
}

void OpenGLRenderer::Present() {
    flushAndScale();
    swapBuffers();
}

void OpenGLRenderer::DrawOutdoorBuildings(const LightsSnapshot &lights) {
    _initWaterTiles();
    // shader
    // verts are streamed to gpu as required
    // textures can be different sizes

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    _set_3d_projection_matrix();
    _set_3d_modelview_matrix();

    if (!_outbuildBuffers[0]) {
        // initialize vertex vectors
        for (int i = 0; i < 16; i++) {
            _outbuildVertices[i].clear();
        }

        // reserve first 7 layers for water tiles in unit 0
        auto wtrtexture = this->hd_water_tile_anim[0];
        //terraintexmap.insert(std::make_pair("wtrtyl", terraintexmap.size()));
        //numterraintexloaded[0]++;
        outbuildtexturewidths[0] = wtrtexture->width();
        outbuildtextureheights[0] = wtrtexture->height();

        for (int buff = 0; buff < 7; buff++) {
            std::string container_name = fmt::format("HDWTR{:03}", buff);
            outbuildtexmap.insert(std::make_pair(container_name, outbuildtexmap.size()));
            numoutbuildtexloaded[0]++;
        }





        for (BSPModel &model : pOutdoor->pBModels) {
            //int reachable;
            //if (IsBModelVisible(&model, &reachable)) {
            if (!model.faces.empty()) {
                for (int faceId : model.faces) {
                    BLVFace &face = pOutdoor->faces[faceId];
                    if (!face.Invisible()) {
                        // TODO(pskelton): Same as indoors. When ODM and BLV face is combined - seperate out function

                        // TODO(yoctozepto, pskelton): we should probably try to handle these faces as they are otherwise marked as visible (see also BSPRenderer)
                        if (!face.GetTexture()) continue;
                        GraphicsImage *tex = face.GetTexture();

                        std::string texname = tex->name();

                        Duration animLength;
                        Duration frame;
                        if (face.IsAnimated()) {
                            tex = pTextureFrameTable->animationFrame(face.animationId, frame);
                            animLength = pTextureFrameTable->animationLength(face.animationId);
                            texname = tex->name();
                        }
                        // gather up all texture and shaderverts data

                        //auto tile = pOutdoor->getTileDescByGrid(x, y);

                        int texunit = 0;
                        int texlayer = 0;
                        int attribflags = 0;

                        if (face.attributes & FACE_IsFluid)
                            attribflags |= 2;
                        if (face.attributes & FACE_INDOOR_SKY)
                            attribflags |= 0x400;

                        if (face.attributes & FACE_FlowDown)
                            attribflags |= 0x400;
                        else if (face.attributes & FACE_FlowUp)
                            attribflags |= 0x800;

                        if (face.attributes & FACE_FlowRight)
                            attribflags |= 0x2000;
                        else if (face.attributes & FACE_FlowLeft)
                            attribflags |= 0x1000;

                        if (face.attributes & FACE_IsLava)
                            attribflags |= 0x4000;

                        // loop while running down animlength with frame animtimes
                        do {
                            // check if tile->name is already in list
                            auto mapiter = outbuildtexmap.find(texname);
                            if (mapiter != outbuildtexmap.end()) {
                                // if so, extract unit and layer
                                int unitlayer = mapiter->second;
                                texlayer = unitlayer & 0xFF;
                                texunit = (unitlayer & 0xFF00) >> 8;
                            } else if (texname == "wtrtyl") {
                                // water tile
                                texunit = 0;
                                texlayer = 0;
                            } else {
                                // else need to add it
                                auto thistexture = assets->getBitmap(texname);
                                int width = thistexture->width();
                                int height = thistexture->height();
                                // check size to see what unit it needs
                                int i;
                                for (i = 0; i < 16; i++) {
                                    if ((outbuildtexturewidths[i] == width && outbuildtextureheights[i] == height) || outbuildtexturewidths[i] == 0) break;
                                }

                                if (i == 16) {
                                    MM_WARNING("Texture unit full - draw building!");
                                    texunit = 0;
                                    texlayer = 0;
                                } else {
                                    if (outbuildtexturewidths[i] == 0) {
                                        outbuildtexturewidths[i] = width;
                                        outbuildtextureheights[i] = height;
                                    }

                                    texunit = i;
                                    texlayer = numoutbuildtexloaded[i];

                                    // encode unit and layer together
                                    int encode = (texunit << 8) | texlayer;

                                    if (numoutbuildtexloaded[i] < 256) {
                                        // intsert into tex map
                                        outbuildtexmap.insert(std::make_pair(texname, encode));
                                        numoutbuildtexloaded[i]++;
                                    } else {
                                        MM_WARNING("Texture layer full - draw building!");
                                        texunit = 0;
                                        texlayer = 0;
                                    }
                                }
                            }

                            if (face.IsAnimated()) {
                                // TODO(pskelton): any instances where animTime is not consistent would need checking
                                frame += pTextureFrameTable->animationFrameLength(face.animationId);
                                tex = pTextureFrameTable->animationFrame(face.animationId, frame);
                                if (!tex) break;
                                texname = tex->name();
                            }
                        } while (animLength > frame);

                        face.texunit = texunit;
                        face.texlayer = texlayer;
                    }
                }
            }
            //}
        }

        for (int l = 0; l < 16; l++) {
            _outbuildBuffers[l].reset(GL_DYNAMIC_DRAW,
                &ShaderVertex::pos, &ShaderVertex::texuv, &ShaderVertex::texturelayer,
                &ShaderVertex::normal, &ShaderVertex::attribs);
        }

        // texture set up

        // loop over all units
        loadPackReplacements(outbuildtexmap);
        for (int unit = 0; unit < 16; unit++) {
            assert(numoutbuildtexloaded[unit] <= 256);
            // skip if textures are empty
            if (numoutbuildtexloaded[unit] == 0) continue;

            glGenTextures(1, &outbuildtextures[unit]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, outbuildtextures[unit]);

            int scale = 1;
            for (const auto &[name, comb] : outbuildtexmap)
                if (((comb & 0xFF00) >> 8) == unit)
                    scale = std::max(scale, assets->getBitmap(name)->textureScale());

            // create blank memory for later texture submission
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, _colorArrayFormat(), outbuildtexturewidths[unit] * scale, outbuildtextureheights[unit] * scale, numoutbuildtexloaded[unit],
                         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            std::vector<std::pair<GraphicsImage *, int>> layers;
            std::map<std::string, int>::iterator it = outbuildtexmap.begin();
            while (it != outbuildtexmap.end()) {
                // skip if wtrtyl
                //if ((it->first).substr(0, 6) == "wtrtyl") {
                //    std::cout << "skipped  " << it->first << std::endl;
                 //   it++;
                 //   continue;
                //}

                int comb = it->second;
                int tlayer = comb & 0xFF;
                int tunit = (comb & 0xFF00) >> 8;

                if (tunit == unit) {
                    // get texture
                    auto texture = assets->getBitmap(it->first);

                    uploadArrayLayer(texture, tlayer, outbuildtexturewidths[unit], outbuildtextureheights[unit], scale);
                    layers.emplace_back(texture, tlayer);
                }

                it++;
            }

            //iterate through terrain tex map
            //ignore wtrtyl
            //laod in

            //auto tile = pOutdoor->getTileDescByGrid(0, 0);
            //bool border = tile->IsWaterBorderTile();


            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            if (scale > 1)
                setMipmappedFilter(GL_TEXTURE_2D_ARRAY);

            outbuildmaterials[unit] = createMaterialArray(layers, numoutbuildtexloaded[unit], outbuildtexturewidths[unit], outbuildtextureheights[unit], scale, GL_REPEAT);
        }
    }

        // else update verts - blank store
        for (int i = 0; i < 16; i++) {
            _outbuildVertices[i].clear();
        }

        for (BSPModel &model : pOutdoor->pBModels) {
            bool reachable;
            if (IsBModelVisible(&model, 256, &reachable)) {
                //if (model.index == 35) continue;
                if (!model.faces.empty()) {
                    for (int faceId : model.faces) {
                        BLVFace &face = pOutdoor->faces[faceId];
                        if (face.vertexIds.size() < 3)
                            continue; // Collapsed to a line or a point by dropDuplicateFaceVertices, e.g. the Harmondale well.
                        if (!face.Invisible()) {
                            if (pCamera3D->is_face_faced_to_camera(&face)) {
                                int texunit = 0;
                                int texlayer = 0;

                                if (face.IsAnimated()) {
                                    texlayer = -1;
                                    texunit = -1;
                                } else {
                                    texlayer = face.texlayer;
                                    texunit = face.texunit;
                                }

                                if (texlayer == -1) { // texture has been reset - see if its in the map
                                    GraphicsImage *tex = face.GetTexture();
                                    std::string texname = tex->name();
                                    auto mapiter = bsptexmap.find(texname);
                                    if (mapiter != bsptexmap.end()) {
                                        // if so, extract unit and layer
                                        int unitlayer = mapiter->second;
                                        face.texlayer = texlayer = unitlayer & 0xFF;
                                        face.texunit = texunit = (unitlayer & 0xFF00) >> 8;
                                    } else {
                                        MM_WARNING("Texture not found in map!");
                                        // TODO(pskelton): set to water for now - fountains in walls of mist
                                        texunit = face.texlayer = 0;
                                        texlayer = face.texunit = 0;
                                    }
                                }

                                int attribflags = 0;

                                if (face.attributes & FACE_IsFluid)
                                    attribflags |= 2;
                                if (face.attributes & FACE_INDOOR_SKY)
                                    attribflags |= 0x400;

                                if (face.attributes & FACE_FlowDown)
                                    attribflags |= 0x400;
                                else if (face.attributes & FACE_FlowUp)
                                    attribflags |= 0x800;

                                if (face.attributes & FACE_FlowRight)
                                    attribflags |= 0x2000;
                                else if (face.attributes & FACE_FlowLeft)
                                    attribflags |= 0x1000;

                                if (face.attributes & FACE_IsLava)
                                    attribflags |= 0x4000;

                                if (face.attributes & FACE_OUTLINED || (face.attributes & FACE_IsSecret) && engine->is_saturate_faces)
                                    attribflags |= 0x00010000;

                                // load up verts here
                                for (int z = 0; z < (face.vertexIds.size() - 2); z++) {
                                    // 123, 134, 145, 156..

                                    // copy first
                                    ShaderVertex &v0 = _outbuildVertices[texunit].emplace_back();
                                    v0.pos = pOutdoor->vertices[face.vertexIds[0]];
                                    v0.texuv = Vec2f(face.textureUs[0] + face.textureDeltaU,
                                                     face.textureVs[0] + face.textureDeltaV);
                                    v0.texturelayer = texlayer;
                                    v0.normal = face.facePlane.normal;
                                    v0.attribs = attribflags;

                                    // copy other two (z+1)(z+2)
                                    for (unsigned i = 1; i < 3; ++i) {
                                        ShaderVertex &v = _outbuildVertices[texunit].emplace_back();
                                        v.pos = pOutdoor->vertices[face.vertexIds[z + i]];
                                        v.texuv = Vec2f(face.textureUs[z + i] + face.textureDeltaU,
                                                        face.textureVs[z + i] + face.textureDeltaV);
                                        v.texturelayer = texlayer;
                                        v.normal = face.facePlane.normal;
                                        v.attribs = attribflags;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        for (int l = 0; l < 16; l++) {
            if (!_outbuildVertices[l].empty()) {
                _outbuildBuffers[l].update(_outbuildVertices[l]);
            }
        }

    // terrain debug
    if (config->debug.Terrain.value())
        // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
        if (!OpenGLES)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    outbuildshader.use();

    // set base uniforms
    OutBuildUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.cameraPos = _view.position;
    uniforms.fog = fog;
    uniforms.gamma = gamma;
    uniforms.waterframe = waterAnimationFrame();
    uniforms.flowtimer = animTimer->time().realtimeMilliseconds() >> 4;
    uniforms.flowtimerms = animTimer->time().realtimeMilliseconds();

    // sun lighting stuff
    float ambient = pParty->uCurrentMinute + pParty->uCurrentHour * 60.0f;  // 0 - > 1439
    ambient = 0.15 + (sinf(((ambient - 360.0f) * 2 * M_PI) / 1440) + 1) * 0.27f;
    float diffuseon = pWeather->bNight ? 0.0f : 1.0f;

    uniforms.sun.direction = pOutdoor->vSunlight;
    uniforms.sun.ambient = Colorf(ambient, ambient, ambient);
    uniforms.sun.diffuse = Colorf(diffuseon * (ambient + 0.3f), diffuseon * (ambient + 0.3f), diffuseon * (ambient + 0.3f));
    uniforms.sun.specular = Colorf(diffuseon * 0.35f * ambient, diffuseon * 0.28f * ambient, 0.0f);

    if (pParty->armageddon_timer) {
        uniforms.sun.ambient = Colorf(1.0f, 0.0f, 0.0f);
        uniforms.sun.diffuse = Colorf(1.0f, 0.0f, 0.0f);
        uniforms.sun.specular = Colorf(0.0f, 0.0f, 0.0f);
    }

    // TODO(pskelton): this should be a separate function
    // rest of lights stacking
    int num_lights = 0;

    // get party torchlight as priority - can be radius == 0
    if (lights.mobile.uNumLightsActive >= 1) {
        const MobileLight &test = lights.mobile.pLights[0];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 2.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = Colorf(0.0f, 0.0f, 0.0f);
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    for (int i = 0; i < lights.stationary.uNumLightsActive; ++i) {
        // maximum 20 lights sent to shader at the moment
        // TODO(pskelton): make this configurable - also lights should be sorted by distance so nearest are used first
        if (num_lights >= 20) break;

        const StationaryLight &test = lights.stationary.pLights[i];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 1.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = color;
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    // mobile
    for (int i = 1; i < lights.mobile.uNumLightsActive; ++i) {
        // maximum 20 lights sent to shader at the moment
        // TODO(pskelton): make this configurable - also lights should be sorted by distance so nearest are used first
        if (num_lights >= 20) break;

        const MobileLight &test = lights.mobile.pLights[i];
        Colorf color = test.uLightColor.toColorf();

        uniforms.pointLights[num_lights].type = 2.0f;
        uniforms.pointLights[num_lights].position = test.vPosition;
        uniforms.pointLights[num_lights].ambient = color;
        uniforms.pointLights[num_lights].diffuse = color;
        uniforms.pointLights[num_lights].specular = color;
        uniforms.pointLights[num_lights].radius = test.uRadius;
        num_lights++;
    }

    // remaining lights are default-initialized (type = 0)
    uniforms.submit(outbuildshader);
    _spriteLights.sun = uniforms.sun;
    std::copy_n(uniforms.pointLights.begin(), _spriteLights.pointLights.size(), _spriteLights.pointLights.begin());
    _submitSunShadow(outbuildshader);
    _submitHeightFog(outbuildshader, true);
    _submitAmbientOcclusion(outbuildshader);
    _submitGlobalIllumination(outbuildshader);
    _submitWater(outbuildshader, nullptr);
    bool hasMaterials = std::ranges::any_of(outbuildmaterials, [](GLuint id) { return id != 0; });
    if (hasMaterials) {
        outbuildmaterialshader.use();
        uniforms.submit(outbuildmaterialshader);
        _submitSunShadow(outbuildmaterialshader);
        _submitHeightFog(outbuildmaterialshader, true);
        _submitAmbientOcclusion(outbuildmaterialshader);
        _submitGlobalIllumination(outbuildmaterialshader);
        _submitWater(outbuildmaterialshader, nullptr);
        glUniform1i(outbuildmaterialshader.uniformLocation("materialArray"), MATERIAL_TEXTURE_UNIT);
    }

    glActiveTexture(GL_TEXTURE0);

    for (int unit = 0; unit < 16; unit++) {
        // skip if textures are empty
        //if (numoutbuildtexloaded[unit] > 0) {
            OpenGLShader &shader = outbuildmaterials[unit] ? outbuildmaterialshader : outbuildshader;
            shader.use();
            glUniform1i(shader.uniformLocation("watertiles"), GLint(unit == 0));
            if (outbuildmaterials[unit]) {
                glActiveTexture(GL_TEXTURE0 + MATERIAL_TEXTURE_UNIT);
                glBindTexture(GL_TEXTURE_2D_ARRAY, outbuildmaterials[unit]);
                glActiveTexture(GL_TEXTURE0);
            }

            // draw each set of triangles
            glBindTexture(GL_TEXTURE_2D_ARRAY, outbuildtextures[unit]);
            glUniform2i(shader.uniformLocation("logicaltexsize"), outbuildtexturewidths[unit], outbuildtextureheights[unit]);
            _outbuildBuffers[unit].bind();
            glDrawArrays(GL_TRIANGLES, 0, _outbuildVertices[unit].size());
            drawcalls++;
        //}
    }

    // unload
    outbuildshader.unuse();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    //end terrain debug
    if (config->debug.Terrain.value())
        // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
        if (!OpenGLES)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OpenGLRenderer::DrawIndoorFaces() {
    _initWaterTiles();
    _updateGlobalIllumination();
    // void RenderOpenGL::DrawIndoorBSP() {

        glEnable(GL_CULL_FACE);

        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        _set_ortho_projection(1);
        _set_ortho_modelview();

        _set_3d_projection_matrix();
        _set_3d_modelview_matrix();

        if (!_bspBuffers[0]) {
            // initialize vertex vectors
            for (int i = 0; i < 16; i++) {
                _bspVertices[i].clear();
            }


            // reserve first 7 layers for water tiles in unit 0
            auto wtrtexture = this->hd_water_tile_anim[0];
            //terraintexmap.insert(std::make_pair("wtrtyl", terraintexmap.size()));
            //numterraintexloaded[0]++;
            bsptexturewidths[0] = wtrtexture->width();
            bsptextureheights[0] = wtrtexture->height();

            for (int buff = 0; buff < 7; buff++) {
                std::string container_name = fmt::format("HDWTR{:03}", buff);
                bsptexmap.insert(std::make_pair(container_name, bsptexmap.size()));
                bsptexloaded[0]++;
            }


            for (int test = 0; test < pIndoor->faces.size(); test++) {
                BLVFace *face = &pIndoor->faces[test];

                if (face->isPortal())
                    continue;
                // TODO(yoctozepto, pskelton): we should probably try to handle these faces as they are otherwise marked as visible (see also BSPRenderer)
                if (!face->GetTexture())
                    continue;

                // TODO(pskelton): Same as outdoors. When ODM and BLV face is combined - seperate out function
                GraphicsImage *tex = face->GetTexture();
                std::string texname = tex->name();

                Duration animLength;
                Duration frame;
                if (face->IsAnimated()) {
                    tex = pTextureFrameTable->animationFrame(face->animationId, frame);
                    animLength = pTextureFrameTable->animationLength(face->animationId);
                    texname = tex->name();
                }

                int texunit = 0;
                int texlayer = 0;

                // loop while running down animlength with frame animtimes
                do {
                    // check if tile->name is already in list
                    auto mapiter = bsptexmap.find(texname);
                    if (mapiter != bsptexmap.end()) {
                        // if so, extract unit and layer
                        int unitlayer = mapiter->second;
                        // TODO(pskelton): make this a pair/struct rather than encoding
                        texlayer = unitlayer & 0xFF;
                        texunit = (unitlayer & 0xFF00) >> 8;
                    } else if (texname == "wtrtyl") {
                        // water tile
                        texunit = 0;
                        texlayer = 0;
                    } else {
                        // else need to add it
                        auto thistexture = assets->getBitmap(texname);
                        int width = thistexture->width();
                        int height = thistexture->height();
                        // check size to see what unit it needs
                        int i;
                        for (i = 0; i < 16; i++) {
                            if ((bsptexturewidths[i] == width && bsptextureheights[i] == height) || bsptexturewidths[i] == 0) break;
                        }

                        if (i == 16) {
                            MM_WARNING("Texture unit full - draw Indoor faces!");
                            texunit = 0;
                            texlayer = 0;
                        } else {
                            if (bsptexturewidths[i] == 0) {
                                bsptexturewidths[i] = width;
                                bsptextureheights[i] = height;
                            }

                            texunit = i;
                            texlayer = bsptexloaded[i];

                            // encode unit and layer together
                            int encode = (texunit << 8) | texlayer;

                            if (bsptexloaded[i] < 256) {
                                // intsert into tex map
                                bsptexmap.insert(std::make_pair(texname, encode));
                                bsptexloaded[i]++;
                            } else {
                                MM_WARNING("Texture layer full - draw indoor faces!");
                                texunit = 0;
                                texlayer = 0;
                            }
                        }
                    }

                    if (face->IsAnimated()) {
                        // TODO(pskelton): any instances where animTime is not consistent would need checking
                        frame += pTextureFrameTable->animationFrameLength(face->animationId);
                        tex = pTextureFrameTable->animationFrame(face->animationId, frame);
                        if (!tex) break;
                        texname = tex->name();
                    }
                } while (animLength > frame);

                face->texunit = texunit;
                face->texlayer = texlayer;
            }

            for (int l = 0; l < 16; l++) {
                _bspBuffers[l].reset(GL_DYNAMIC_DRAW,
                    &ShaderVertex::pos, &ShaderVertex::texuv, &ShaderVertex::texturelayer,
                    &ShaderVertex::normal, &ShaderVertex::attribs);
            }

            // texture set up

            // loop over all units
            loadPackReplacements(bsptexmap);
            for (int unit = 0; unit < 16; unit++) {
                assert(bsptexloaded[unit] <= 256);
                // skip if textures are empty
                if (bsptexloaded[unit] == 0) continue;

                glGenTextures(1, &bsptextures[unit]);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, bsptextures[unit]);

                int scale = 1;
                for (const auto &[name, comb] : bsptexmap)
                    if (((comb & 0xFF00) >> 8) == unit)
                        scale = std::max(scale, assets->getBitmap(name)->textureScale());

                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, _colorArrayFormat(), bsptexturewidths[unit] * scale, bsptextureheights[unit] * scale, bsptexloaded[unit],
                             0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                std::vector<std::pair<GraphicsImage *, int>> layers;
                std::map<std::string, int>::iterator it = bsptexmap.begin();
                while (it != bsptexmap.end()) {
                    int comb = it->second;
                    int tlayer = comb & 0xFF;
                    int tunit = (comb & 0xFF00) >> 8;

                    if (tunit == unit) {
                        // get texture
                        auto texture = assets->getBitmap(it->first);

                        uploadArrayLayer(texture, tlayer, bsptexturewidths[unit], bsptextureheights[unit], scale);
                        layers.emplace_back(texture, tlayer);

                        //numterraintexloaded[0]++;
                    }

                    it++;
                }




                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

                glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
                if (scale > 1)
                    setMipmappedFilter(GL_TEXTURE_2D_ARRAY);

                bspmaterials[unit] = createMaterialArray(layers, bsptexloaded[unit], bsptexturewidths[unit], bsptextureheights[unit], scale, GL_REPEAT);
            }
        }


            // update verts - blank store
            for (int i = 0; i < 16; i++) {
                _bspVertices[i].clear();
            }

            bool drawnsky = false;

            for (unsigned i = 0; i < pBspRenderer->num_faces; ++i) {
                int uFaceID = pBspRenderer->faces[i].uFaceID;
                BLVFace *face = &pIndoor->faces[uFaceID];

                float skymodtimex = 0;
                float skymodtimey = 0;
                if (face->Indoor_sky()) {
                    if (face->polygonType != POLYGON_InBetweenFloorAndWall && face->polygonType != POLYGON_Floor) {
                        // draw forced perspective sky
                        DrawIndoorSky(face->vertexIds.size(), uFaceID);
                        continue;
                    } else {
                        // TODO(pskelton): check tickcount usage here
                        skymodtimex = (platform->tickCount() / 32.0f) - pCamera3D->vCameraPos.x;
                        skymodtimey = (platform->tickCount() / 32.0f) + pCamera3D->vCameraPos.y;
                    }
                }

                ++pBLVRenderParams->uNumFacesRenderedThisFrame;
                // load up verts here
                int texlayer = 0;
                int texunit = 0;
                int attribflags = 0;

                if (face->attributes & FACE_IsFluid)
                    attribflags |= 2;

                if (face->attributes & FACE_FlowDown)
                    attribflags |= 0x400;
                else if (face->attributes & FACE_FlowUp)
                    attribflags |= 0x800;

                if (face->attributes & FACE_FlowRight)
                    attribflags |= 0x2000;
                else if (face->attributes & FACE_FlowLeft)
                    attribflags |= 0x1000;

                if (face->attributes & FACE_IsLava)
                    attribflags |= 0x4000;

                if (face->attributes & FACE_OUTLINED || (face->attributes & FACE_IsSecret) && engine->is_saturate_faces)
                    attribflags |= 0x00010000;

                if (face->IsAnimated()) {
                    texlayer = -1;
                    texunit = -1;
                } else {
                    texlayer = face->texlayer;
                    texunit = face->texunit;
                }

                if (texlayer == -1) { // texture has been reset - see if its in the map
                    GraphicsImage *tex = face->GetTexture();
                    std::string texname = tex->name();
                    auto mapiter = bsptexmap.find(texname);
                    if (mapiter != bsptexmap.end()) {
                        // if so, extract unit and layer
                        int unitlayer = mapiter->second;
                        face->texlayer = texlayer = unitlayer & 0xFF;
                        face->texunit = texunit = (unitlayer & 0xFF00) >> 8;
                    } else {
                        MM_WARNING("Texture not found in map!");
                        // TODO(pskelton): set to water for now - fountains in walls of mist
                        texlayer = face->texlayer = 0;
                        texunit = face->texunit = 0;
                    }
                }


                for (int z = 0; z < (face->vertexIds.size() - 2); z++) {
                    // 123, 134, 145, 156..

                    // copy first
                    ShaderVertex &v0 = _bspVertices[texunit].emplace_back();
                    v0.pos = pIndoor->vertices[face->vertexIds[0]];
                    v0.texuv = Vec2f(face->textureUs[0] + face->textureDeltaU,
                                     face->textureVs[0] + face->textureDeltaV);
                    if (face->Indoor_sky()) {
                        v0.texuv.x = (skymodtimex + v0.texuv.x) * 0.25f;
                        v0.texuv.y = (skymodtimey + v0.texuv.y) * 0.25f;
                    }
                    v0.texturelayer = texlayer;
                    v0.normal = face->facePlane.normal;
                    v0.attribs = attribflags;

                    // copy other two (z+1)(z+2)
                    for (unsigned i = 1; i < 3; ++i) {
                        ShaderVertex &v = _bspVertices[texunit].emplace_back();
                        v.pos = pIndoor->vertices[face->vertexIds[z + i]];
                        v.texuv = Vec2f(face->textureUs[z + i] + face->textureDeltaU,
                                        face->textureVs[z + i] + face->textureDeltaV);
                        if (face->Indoor_sky()) {
                            v.texuv.x = (skymodtimex + v.texuv.x) * 0.25f;
                            v.texuv.y = (skymodtimey + v.texuv.y) * 0.25f;
                        }
                        v.texturelayer = texlayer;
                        v.normal = face->facePlane.normal;
                        v.attribs = attribflags;
                    }
                }
            }

            for (int l = 0; l < 16; l++) {
                if (!_bspVertices[l].empty()) {
                    _bspBuffers[l].update(_bspVertices[l]);
                }
            }

            glBindBuffer(GL_ARRAY_BUFFER, 0);

        // terrain debug
        if (config->debug.Terrain.value())
            // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
            if (!OpenGLES)
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        ////
        for (int unit = 0; unit < 16; unit++) {
            // skip if textures are empty
            //if (bsptexloaded[unit] > 0) {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D_ARRAY, bsptextures[unit]);
            //}
        }

        //glBindVertexArray(bspVAO);
        //glEnableVertexAttribArray(0);
        //glEnableVertexAttribArray(1);
        //glEnableVertexAttribArray(2);
        //glEnableVertexAttribArray(3);
        //glEnableVertexAttribArray(4);

        bspshader.use();

        // set base uniforms
        BSPUniforms uniforms;
        uniforms.projection = projmat;
        uniforms.view = viewmat;
        uniforms.cameraPos = _view.position;
        uniforms.gamma = gamma;
        uniforms.waterframe = waterAnimationFrame();
        uniforms.flowtimer = animTimer->time().realtimeMilliseconds() >> 4;
        uniforms.flowtimerms = animTimer->time().realtimeMilliseconds();

        // lighting stuff
        int uCurrentAmbientLightLevel = pIndoor->ambientLightLevel();
        float ambient = (248.0f - (uCurrentAmbientLightLevel << 3)) / 255.0f;
        float diffuseon = 0.0f;

        uniforms.sun.direction = Vec3f(0, 0, 0);
        uniforms.sun.ambient = Colorf(ambient, ambient, ambient);
        uniforms.sun.diffuse = Colorf(diffuseon * (ambient + 0.3f), diffuseon * (ambient + 0.3f), diffuseon * (ambient + 0.3f));
        uniforms.sun.specular = Colorf(diffuseon * 1.0f, diffuseon * 0.8f, 0.0f);

        // point lights - fspointlights
        // rest of lights stacking
        int num_lights = 0;

        // get party torchlight as priority
        if (pMobileLightsStack->uNumLightsActive >= 1) {
            MobileLight &test = pMobileLightsStack->pLights[0];
            Colorf color = test.uLightColor.toColorf();

            uniforms.pointLights[num_lights].type = 2.0f;
            uniforms.pointLights[num_lights].position = test.vPosition;
            uniforms.pointLights[num_lights].ambient = color;
            uniforms.pointLights[num_lights].diffuse = color;
            uniforms.pointLights[num_lights].specular = Colorf(0.0f, 0.0f, 0.0f);
            uniforms.pointLights[num_lights].radius = test.uRadius;
            num_lights++;
        }

        // stack the static lights next (wall torches)
        std::array<int, 40> torchShadowLayers;
        torchShadowLayers.fill(-1);
        for (int i = 0; i < pStationaryLightsStack->uNumLightsActive; ++i) {
            if (num_lights >= 40) break;

            StationaryLight &test = pStationaryLightsStack->pLights[i];

            // is this on the sector list
            bool onlist = false;
            for (unsigned j = 0; j < pBspRenderer->uNumVisibleNotEmptySectors; ++j) {
                int listsector = pBspRenderer->pVisibleSectorIDs_toDrawDecorsActorsEtcFrom[j];
                if (test.uSectorID == listsector) {
                    onlist = true;
                    break;
                }
            }

            // does light sphere collide with current sector
            // expanded current sector
            bool fromexpanded = false;
            if (pIndoor->sectors[pBLVRenderParams->uPartySectorID].boundingBox.intersectsCube(test.vPosition, test.uRadius)) {
                onlist = true;
                fromexpanded = true;
            }

            if (!onlist) continue;

            // cull through viewing frustum
            bool visinfrustum = false;
            if (!fromexpanded) {
                for (int j = 0; j < pBspRenderer->num_nodes; ++j) {
                    if (pBspRenderer->nodes[j].uSectorID == test.uSectorID) {
                        if (IsSphereInFrustum(test.vPosition, test.uRadius, pBspRenderer->nodes[j].ViewportNodeFrustum.data()))
                            visinfrustum = true;
                    }
                }
            } else {
                if (IsSphereInFrustum(test.vPosition, test.uRadius)) visinfrustum = true;
            }
            if (!visinfrustum) continue;

            Colorf color = test.uLightColor.toColorf();

            uniforms.pointLights[num_lights].type = 1.0f;
            uniforms.pointLights[num_lights].position = test.vPosition;
            uniforms.pointLights[num_lights].ambient = color;
            uniforms.pointLights[num_lights].diffuse = color;
            uniforms.pointLights[num_lights].specular = Colorf(0.0f, 0.0f, 0.0f);
            uniforms.pointLights[num_lights].radius = test.uRadius;
            auto cube = std::ranges::find(_torchShadowLights, i);
            torchShadowLayers[num_lights] = cube == _torchShadowLights.end() ? -1 : static_cast<int>(cube - _torchShadowLights.begin());
            num_lights++;
        }

        // whatevers left for mobile lights
        for (int i = 1; i < pMobileLightsStack->uNumLightsActive; ++i) {
            if (num_lights >= 40) break;

            // TODO(pskelton): nearest lights should be prioritised
            MobileLight &test = pMobileLightsStack->pLights[i];
            if (!IsSphereInFrustum(test.vPosition, test.uRadius)) continue;

            Colorf color = test.uLightColor.toColorf();

            uniforms.pointLights[num_lights].type = 2.0f;
            uniforms.pointLights[num_lights].position = test.vPosition;
            uniforms.pointLights[num_lights].ambient = color;
            uniforms.pointLights[num_lights].diffuse = color;
            uniforms.pointLights[num_lights].specular = Colorf(0.0f, 0.0f, 0.0f);
            uniforms.pointLights[num_lights].radius = test.uRadius;
            num_lights++;
        }

        // remaining lights are default-initialized (type = 0)
        uniforms.submit(bspshader);
        _spriteLights.sun = uniforms.sun;
        std::copy_n(uniforms.pointLights.begin(), _spriteLights.pointLights.size(), _spriteLights.pointLights.begin());
        _submitTorchShadows(bspshader, torchShadowLayers);
        _submitAmbientOcclusion(bspshader);
        _submitGlobalIllumination(bspshader);
        float wall = ambient * INDOOR_WATER_REFLECTION;
        Colorf room(wall, wall, wall); // Indoor water reflects the room around it.
        _submitWater(bspshader, &room);
        bool hasMaterials = std::ranges::any_of(bspmaterials, [](GLuint id) { return id != 0; });
        if (hasMaterials) {
            bspmaterialshader.use();
            uniforms.submit(bspmaterialshader);
            _submitTorchShadows(bspmaterialshader, torchShadowLayers);
            _submitAmbientOcclusion(bspmaterialshader);
            _submitGlobalIllumination(bspmaterialshader);
            _submitWater(bspmaterialshader, &room);
            glUniform1i(bspmaterialshader.uniformLocation("materialArray"), MATERIAL_TEXTURE_UNIT);
        }

        glActiveTexture(GL_TEXTURE0);

        for (int unit = 0; unit < 16; unit++) {
            // skip if textures are empty
            //if (numoutbuildtexloaded[unit] > 0) {
            OpenGLShader &shader = bspmaterials[unit] ? bspmaterialshader : bspshader;
            shader.use();
            glUniform1i(shader.uniformLocation("watertiles"), GLint(unit == 0)); // toggle for water faces or not
            if (bspmaterials[unit]) {
                glActiveTexture(GL_TEXTURE0 + MATERIAL_TEXTURE_UNIT);
                glBindTexture(GL_TEXTURE_2D_ARRAY, bspmaterials[unit]);
                glActiveTexture(GL_TEXTURE0);
            }

            // draw each set of triangles
            glBindTexture(GL_TEXTURE_2D_ARRAY, bsptextures[unit]);
            glUniform2i(shader.uniformLocation("logicaltexsize"), bsptexturewidths[unit], bsptextureheights[unit]);
            _bspBuffers[unit].bind();
            glDrawArrays(GL_TRIANGLES, 0, _bspVertices[unit].size());
            drawcalls++;
            //}
        }

        bspshader.unuse();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // indoor sky drawing
        if (!_forcePerVertices.empty()) {
            // set forced matrixs
            _set_ortho_projection(1);
            _set_ortho_modelview();

            SkyBillboard.CalcSkyFrustumVec(1, 0, 0, 0, 1, 0);

            DrawForcePerVerts();

            _set_3d_projection_matrix();
            _set_3d_modelview_matrix();
        }
        drawSkyVertices(INDOOR_SKY_PLANE_HEIGHT, false);


        //end terrain debug
        if (config->debug.Terrain.value())
            // TODO: OpenGL ES doesn't provide wireframe functionality so enable it only for classic OpenGL for now
            if (!OpenGLES)
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OpenGLRenderer::Initialize() {
    BaseRenderer::Initialize();

    if (!window)
        throw Exception("Cannot initialize the renderer without a window");

    PlatformOpenGLOptions opts;

    // Set it only on startup as currently we don't support multiple contexts to be able to switch OpenGL<->OpenGLES in the middle of runtime.
    OpenGLES = config->graphics.Renderer.value() == RENDERER_OPENGL_ES;

    if (!OpenGLES) {
        // Use OpenGL 4.1 core
        opts.versionMajor = 4;
        opts.versionMinor = 1;
        opts.profile = GL_PROFILE_CORE;
    } else {
        // Use OpenGL ES 3.2
        opts.versionMajor = 3;
        opts.versionMinor = 2;
        opts.profile = GL_PROFILE_ES;
    }

    // Turn on 24bit Z buffer.
    // You may need to change this to 16 or 32 for your system
    opts.depthBits = 24;
    opts.stencilBits = 8;

    opts.vsyncMode = config->graphics.VSync.value() ? GL_VSYNC_ADAPTIVE : GL_VSYNC_NONE;

    application->initializeOpenGLContext(opts);

    auto gladLoadFunc = [](void *ptr, const char *name) {
        return reinterpret_cast<GLADapiproc>(static_cast<PlatformOpenGLContext *>(ptr)->getProcAddress(name));
    };

    int version;
    if (OpenGLES)
        version = gladLoadGLES2UserPtr(gladLoadFunc, openGLContext);
    else
        version = gladLoadGLUserPtr(gladLoadFunc, openGLContext);

    auto glGetStringSafe = [] (int id) {
        // Need this wrapper b/c glGetString can return nullptr, actually happens under OpenGL 1.1 when called for
        // GL_SHADING_LANGUAGE_VERSION.
        const char *result = reinterpret_cast<const char *>(glGetString(id));
        return result ? result : "???";
    };

    if (!version) {
        MM_ERROR("GLAD: Failed to initialize the OpenGL loader");
    } else {
        MM_INFO("OpenGL version: {}.{}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
        MM_INFO("OpenGL version string: {}", glGetStringSafe(GL_VERSION));
        MM_INFO("GLSL version: {}", glGetStringSafe(GL_SHADING_LANGUAGE_VERSION));
        // TODO(captainurist): this is probably the place to check OpenGL version & exit.
        //                     openenroth requires opengl core 4.1 or opengles 3.2 capable gpu to run.
    }

    if (config->debug.ValidateOpenGL.value()) {
        gladSetGLPostCallback(GL_Check_Errors);
    } else {
        gladUninstallGLDebug();
    }

    _initImGui();

    if (!Reinitialize(true))
        throw Exception("Renderer failed to initialize");
}

void OpenGLRenderer::_initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // Dont allow ImGui to modify mouse cursor

    SDL_Window *sdlWindow = static_cast<SDL_Window *>(window->nativeHandle());
    ImGui_ImplSDL3_InitForOpenGL(sdlWindow, openGLContext->nativeHandle());
    ImGui_ImplOpenGL3_Init();

    // Set ImGui Style
    ImVec4 *colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.47f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.70f, 0.70f, 0.70f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.70f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
}

void OpenGLRenderer::_shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void OpenGLRenderer::_initWaterTiles() {
    if (hd_water_tile_anim[0])
        return;

    for (unsigned i = 0; i < 7; ++i) {
        std::string container_name = fmt::format("HDWTR{:03}", i);
        hd_water_tile_anim[i] = assets->getBitmap(container_name);
    }
}

bool OpenGLRenderer::Reinitialize(bool firstInit) {
    BaseRenderer::Reinitialize(firstInit);

    if (firstInit) {
        _readPipelineSettings();
        _uiScale = config->graphics.UiScale.value();
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);       // Black Background
    glClearDepthf(1.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (firstInit) {
        // clear only on first init as it will introduce brief black artifacts on window resize
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    _uiTarget.reset(uiTargetSize(), RENDER_TARGET_FORMAT_RGBA8, true);

    int worldTargetWidth = config->graphics.WorldTargetWidth.value();
    int worldTargetHeight = config->graphics.WorldTargetHeight.value();
    if (_linearPipeline && (worldTargetWidth == 0 || worldTargetHeight == 0)) {
        worldTargetWidth = pViewport.w * _uiScale;
        worldTargetHeight = pViewport.h * _uiScale;
        if (config->graphics.Enhanced.value()) { // The viewport's size in window pixels, which outputRender can be smaller than.
            worldTargetWidth = pViewport.w * outputPresent.w / outputRender.w;
            worldTargetHeight = pViewport.h * outputPresent.h / outputRender.h;
        }
    }
    if (worldTargetWidth > 0 && worldTargetHeight > 0) {
        RenderTargetFormat format = config->graphics.WorldTargetHdr.value() ? RENDER_TARGET_FORMAT_RGBA16F : RENDER_TARGET_FORMAT_RGBA8;
        if (_linearPipeline)
            format = RENDER_TARGET_FORMAT_R11G11B10F; // Nothing reads the world's alpha.
        _worldTarget.reset(Sizei(worldTargetWidth, worldTargetHeight), format, true);
    } else {
        _worldTarget.reset();
    }

    _setUiViewport();
    ResetUIClipRect();
    glEnable(GL_SCISSOR_TEST);

    // PostInitialization();

    // check gpu gl capability params
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &GPU_MAX_TEX_SIZE);
    assert(GPU_MAX_TEX_SIZE >= 512);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &GPU_MAX_TEX_LAYERS);
    assert(GPU_MAX_TEX_LAYERS >= 256);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &GPU_MAX_TEX_UNITS);
    assert(GPU_MAX_TEX_UNITS >= 16);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &GPU_MAX_UNIFORM_COMP);
    assert(GPU_MAX_UNIFORM_COMP >= 1024);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &GPU_MAX_TOTAL_TEXTURES);
    assert(GPU_MAX_TOTAL_TEXTURES >= 80);

    if (firstInit) {
        // initiate shaders
        if (!ReloadShaders())
            return false;
    } // else {

    if (config->window.ReloadTex.value()) {
        // Added config option for this - may not always be required - #199 no longer replicates on windows??
        // TODO(pskelton): Needs testings on other platforms
        assets->releaseAllTextures();
        ReleaseTerrain();
        ReleaseBSP();
    }

    //     ReloadShaders();
    // }

    return BaseRenderer::Reinitialize(firstInit);
}

bool OpenGLRenderer::ReloadShaders() {
    MM_INFO("Reloading shaders...");

    ReleaseTerrain();
    ReleaseBSP();

    _textBuffer.reset();
    _textVertices.clear();

    _lineBuffer.reset();
    _lineVertices.clear();

    _twodBuffer.reset();
    _twodVertices.clear();

    _billboardBuffer.reset();
    if (paltex2D) {
        glDeleteTextures(1, &paltex2D);
        paltex2D = 0;
    }

    _billboardBuffer.reset();
    _billboardVertices.clear();

    _decalBuffer.reset();
    _decalVertices.clear();

    _forcePerBuffer.reset();
    _forcePerVertices.clear();

    _skyBuffer.reset();
    _skyVertices.clear();

    static constexpr std::string_view bloomUpsampleDefines[] = {"BLOOM_UPSAMPLE"};
    static constexpr std::string_view spriteShadowDefines[] = {"SHADOW_SPRITE"};
    std::vector<std::string_view> worldDefines;
    if (_linearPipeline)
        worldDefines.push_back("LINEAR_PIPELINE");
    std::vector<std::string_view> materialDefines = worldDefines;
    materialDefines.push_back("MATERIAL_MAPS");
    std::vector<std::string_view> outdoorDefines = worldDefines;
    std::vector<std::string_view> outdoorMaterialDefines = materialDefines;
    if (_sunShadows) {
        outdoorDefines.push_back("SUN_SHADOWS");
        outdoorMaterialDefines.push_back("SUN_SHADOWS");
    }
    std::vector<std::string_view> indoorDefines = worldDefines;
    std::vector<std::string_view> indoorMaterialDefines = materialDefines;
    if (_torchShadows) {
        indoorDefines.push_back("TORCH_SHADOWS");
        indoorMaterialDefines.push_back("TORCH_SHADOWS");
    }
    static constexpr std::string_view aoBlurDefines[] = {"AO_BLUR"};
    std::vector<std::string_view> outdoorLitDefines = outdoorDefines;
    std::vector<std::string_view> outdoorLitMaterialDefines = outdoorMaterialDefines;
    if (_ambientOcclusion) {
        for (std::vector<std::string_view> *defines : {&outdoorLitDefines, &outdoorLitMaterialDefines, &indoorDefines, &indoorMaterialDefines})
            defines->push_back("AMBIENT_OCCLUSION");
    }
    if (_globalIllumination) {
        for (std::vector<std::string_view> *defines : {&outdoorLitDefines, &outdoorLitMaterialDefines, &indoorDefines, &indoorMaterialDefines})
            defines->push_back("GLOBAL_ILLUMINATION");
    }
    std::vector<std::string_view> terrainDefines = outdoorLitDefines;
    std::vector<std::string_view> terrainMaterialDefines = outdoorLitMaterialDefines;
    if (_terrainVariation) {
        terrainDefines.push_back("TERRAIN_VARIATION");
        terrainMaterialDefines.push_back("TERRAIN_VARIATION");
    }
    std::vector<std::string_view> billboardDefines = outdoorDefines;
    if (_spriteCrossfade)
        billboardDefines.push_back("SPRITE_CROSSFADE");
    if (_spriteDepth)
        billboardDefines.push_back("SPRITE_DEPTH");
    if (_spriteLighting)
        billboardDefines.push_back("SPRITE_LIGHTING");
    if (_globalIllumination)
        billboardDefines.push_back("GLOBAL_ILLUMINATION");
    const std::initializer_list<std::tuple<OpenGLShader *, std::string_view, std::string_view, std::span<const std::string_view>>> shaders = {
        {&terrainshader,          "glterrain",        "Terrain",                          terrainDefines},
        {&outbuildshader,         "gloutbuild",       "Outdoor buildings",                outdoorLitDefines},
        {&bspshader,              "glbspshader",      "Indoor BSP",                       indoorDefines},
        {&textshader,             "gltextshader",     "Text",                             {}},
        {&lineshader,             "gllinesshader",    "Lines",                            {}},
        {&twodshader,             "gltwodshader",     "2D",                               {}},
        {&billbshader,            "glbillbshader",    "Billboards",                       billboardDefines},
        {&decalshader,            "gldecalshader",    "Decals",                           worldDefines},
        {&forcepershader,         "glforcepershader", "Forced perspective",               worldDefines},
        {&skyshader,              "glskyshader",      "Sky",                              worldDefines},
        {&compositeshader,        "glcomposite",      "Composite",                        {}},
        {&bloomdownshader,        "glbloom",          "Bloom downsample",                 {}},
        {&bloomupshader,          "glbloom",          "Bloom upsample",                   bloomUpsampleDefines},
        {&fxaashader,             "glfxaa",           "FXAA",                             {}},
        {&shadowshader,           "glshadow",         "Shadow map",                       {}},
        {&shadowspriteshader,     "glshadow",         "Sprite shadow map",                spriteShadowDefines},
        {&terrainmaterialshader,  "glterrain",        "Terrain with materials",           terrainMaterialDefines},
        {&outbuildmaterialshader, "gloutbuild",       "Outdoor buildings with materials", outdoorLitMaterialDefines},
        {&bspmaterialshader,      "glbspshader",      "Indoor BSP with materials",        indoorMaterialDefines},
        {&torchshadowshader,      "gltorchshadow",    "Torch shadow map",                 {}},
        {&contactshadowshader,    "glcontactshadow",  "Contact shadow",                   {}},
        {&aodepthshader,          "glaodepth",        "Ambient occlusion depth",          {}},
        {&aoshader,               "glao",             "Ambient occlusion",                {}},
        {&depthhalfshader,        "gldepthhalf",      "Half resolution depth",            {}},
        {&aoblurshader,           "glao",             "Ambient occlusion blur",           aoBlurDefines}
    };

    SubFileSystem shadersFs("shaders", dfs);

    for (const auto &[shader, fileName, readableName, defines] : shaders) {
        if (!shader->load(shadersFs.read(fmt::format("{}.vert", fileName)),
                          shadersFs.read(fmt::format("{}.frag", fileName)), OpenGLES, &shadersFs, defines)) {
            platform->showMessageBox("CRITICAL ERROR: shader compilation failure",
                                     fmt::format("{} shader failed to compile!\nPlease consult the log and consider issuing a bug report!", readableName));
            return false;
        }
    }

    MM_INFO("Shaders reloaded.");
    return true;
}

void OpenGLRenderer::beginOverlays() {
    ImGui_ImplOpenGL3_NewFrame();
    // we assume we're always running with SDL
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void OpenGLRenderer::endOverlays() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void OpenGLRenderer::ReleaseTerrain() {
    _sunShadowTerrainBuffer.reset();
    _sunShadowBuildingBuffer.reset();
    _sunShadowSpriteBuffer.reset();
    _sunShadowModelRanges.clear();

    /*GLuint terrainVBO, terrainVAO;
    GLuint terraintextures[8];
    unsigned numterraintexloaded[8];
    unsigned terraintexturesizes[8];
    std::map<std::string, int> terraintexmap;*/

    terraintexmap.clear();

    for (int i = 0; i < 8; i++) {
        glDeleteTextures(1, &terraintextures[i]);
        terraintextures[i] = 0;
        glDeleteTextures(1, &terrainmaterials[i]);
        terrainmaterials[i] = 0;
        numterraintexloaded[i] = 0;
        terraintexturesizes[i] = 0;
    }

    _terrainBuffer.reset();

    /*GLuint outbuildVBO, outbuildVAO;
    GLuint outbuildtextures[8];
    unsigned numoutbuildtexloaded[8];
    unsigned outbuildtexturewidths[8];
    unsigned outbuildtextureheights[8];
    std::map<std::string, int> outbuildtexmap;*/

    outbuildtexmap.clear();

    for (int i = 0; i < 16; i++) {
        glDeleteTextures(1, &outbuildtextures[i]);
        outbuildtextures[i] = 0;
        glDeleteTextures(1, &outbuildmaterials[i]);
        outbuildmaterials[i] = 0;
        numoutbuildtexloaded[i] = 0;
        outbuildtexturewidths[i] = 0;
        outbuildtextureheights[i] = 0;
        _outbuildBuffers[i].reset();
        _outbuildVertices[i].clear();
    }
}

void OpenGLRenderer::ReleaseBSP() {
    /*GLuint bspVBO, bspVAO;
    GLuint bsptextures[16];
    unsigned bsptexloaded[16];
    unsigned bsptexturewidths[16];
    unsigned bsptextureheights[16];
    std::map<std::string, int> bsptexmap;*/

    bsptexmap.clear();
    _torchShadowDrawnLights.clear();
    _aoIndoorBuffer.reset();

    for (int i = 0; i < 16; i++) {
        glDeleteTextures(1, &bsptextures[i]);
        bsptextures[i] = 0;
        glDeleteTextures(1, &bspmaterials[i]);
        bspmaterials[i] = 0;
        bsptexloaded[i] = 0;
        bsptexturewidths[i] = 0;
        bsptextureheights[i] = 0;
        _bspBuffers[i].reset();
        _bspVertices[i].clear();
    }
}


void OpenGLRenderer::DrawTwodVerts() {
    _drawTwodVerts(false);
}

void OpenGLRenderer::_drawTwodVerts(bool premultiplied) {
    if (_twodVertices.empty()) return;

    Recti savedClipRect = this->clipRect;
    render->ResetUIClipRect();

    if (!_twodBuffer) {
        _twodBuffer.reset(GL_DYNAMIC_DRAW,
            &TwoDVertex::pos,
            &TwoDVertex::texuv,
            &TwoDVertex::color,
            &TwoDVertex::texid,
            &TwoDVertex::paletteid);
    }

    ensurePaletteTexture();

    _twodBuffer.update(_twodVertices);
    _twodBuffer.bind();

    twodshader.use();

    // set sampler to palette
    glActiveTexture(GL_TEXTURE0 + PALETTE_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, paltex2D);

    glActiveTexture(GL_TEXTURE0);

    glEnable(GL_BLEND);
    if (premultiplied) {
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        _setUiBlendFunc();
    }

    TwoDUniforms uniforms;
    uniforms.projection = projmat;
    uniforms.view = viewmat;
    uniforms.paltex2D = PALETTE_TEXTURE_UNIT;
    uniforms.submit(twodshader);

    size_t offset = 0;
    while (offset < _twodVertices.size()) {
        // set texture
        GLfloat thistex = _twodVertices[offset].texid;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_twodVertices[offset].texid));
        bool nearest = _twodVertices[offset].nearest;
        if (_twodVertices[offset].paletteid || nearest) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else if (_mipmappedTextures.contains(static_cast<GLuint>(_twodVertices[offset].texid))) {
            setMipmappedFilter(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        size_t cnt = 0;
        do {
            cnt++;
            if (offset + (6 * cnt) >= _twodVertices.size()) {
                break;
            }
        } while (_twodVertices[offset + (cnt * 6)].texid == thistex && _twodVertices[offset + (cnt * 6)].nearest == nearest);

        glDrawArrays(GL_TRIANGLES, offset, (6*cnt));
        drawcalls++;

        offset += (6*cnt);
    }

    twodshader.unuse();
    _twodBuffer.unbind();

    _twodVertices.clear();
    render->SetUIClipRect(savedClipRect);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}
