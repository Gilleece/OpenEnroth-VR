#pragma once

#include <array>
#include <memory>
#include <string>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glad/gl.h> // NOLINT: this is not a C system include.
#include <glm/glm.hpp>

#include "Engine/Graphics/CameraView.h"
#include "Engine/Graphics/FrameLimiter.h"
#include "Engine/MapEnums.h"
#include "Engine/Resources/GiProbes.h"
#include "BaseRenderer.h"

#include "Library/Color/Colorf.h"
#include "Library/Geometry/Vec.h"

#include "OpenGLRenderTarget.h"
#include "OpenGLVertexBuffer.h"
#include "OpenGLShader.h"
#include "OpenGLShaderParams.h"

class PlatformOpenGLContext;
struct BLVFace;

class OpenGLRenderer : public BaseRenderer {
 public:
    OpenGLRenderer(
        std::shared_ptr<GameConfig> config,
        DecalBuilder *decal_builder,
        SpellFxRenderer *spellfx,
        std::shared_ptr<ParticleEngine> particle_engine,
        Vis *vis
    );
    virtual ~OpenGLRenderer();

    virtual void Initialize() override;

    virtual RgbaImage ReadScreenPixels() override;
    virtual void ClearTarget(Color uColor) override;
    virtual void Present() override;

    virtual void BeginLines2D() override;
    virtual void EndLines2D() override;
    virtual void RasterLine2D(Pointi a, Pointi b, Color acolor, Color bcolor) override;

    virtual void BeginScene3D(const CameraView &view) override;
    virtual void EndScene3D() override;
    virtual Recti worldTargetViewport() override;
    virtual void compositeWorldTarget() override;
    virtual TextureRenderId uiTargetTexture() override;
    virtual Sizei uiTargetSize() override;
    virtual void setExternalWorldTarget(TextureRenderId colorTexture, Sizei size) override;
    virtual void setViewportMirror(TextureRenderId colorTexture, Sizei size) override;
    virtual void setExternalFramePacing(bool external) override;
    virtual bool worldSpaceBillboards() override;
    virtual bool spriteShapes() override;
    virtual void applyPipelineSettings() override;
    virtual void drawSunShadows(std::span<const int> casterModels, std::span<const SunShadowSprite> casterSprites) override;
    virtual void drawTorchShadows() override;
    virtual void drawAmbientOcclusion() override;
    virtual void drawWorldProjectile(Vec3f src, Vec3f dst, GraphicsImage *texture) override;
    virtual void setUiLayer(bool enabled) override;
    virtual bool uiLayer() override;
    virtual void copyUiLayer(TextureRenderId colorTexture, Sizei size) override;
    virtual void beginUiCard(TextureRenderId colorTexture, Sizei size, const Recti &screenRect) override;
    virtual void endUiCard() override;
    virtual void copyUiLayerRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Pointi position) override;
    virtual void fillExternalRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Color color) override;
    virtual void forgetExternalTextures(std::span<const TextureRenderId> colorTextures) override;

    virtual void DrawProjectile(float srcX, float srcY, float a3, float a4,
                                float dstX, float dstY, float a7, float a8,
                                GraphicsImage *texture) override;

    virtual TextureRenderId CreateTexture(RgbaImageView image, TextureFilter filter) override;
    virtual void updateTexture(TextureRenderId id, RgbaImageView image) override;
    virtual void DeleteTexture(TextureRenderId id) override;

    virtual void BeginScene2D() override;
    virtual void ScreenFade(Color color, float t) override;

    virtual void SetUIClipRect(const Recti &rect) override;
    virtual void ResetUIClipRect() override;

    virtual void DrawQuad2D(GraphicsImage *texture, const Recti &srcRect, const Recti &dstRect, Color color) override;
    virtual void DrawImage(GraphicsImage *, const Recti &rect, int paletteid = 0, Color colourmask = colorTable.White) override;

    virtual void BlendTextures(int a2, int a3, GraphicsImage *a4, GraphicsImage *a5, int t,
                               int start_opacity, int end_opacity) override;

    virtual void BeginTextNew(GraphicsImage *texture) override;
    virtual void EndTextNew() override;
    virtual void DrawTextNew(const Recti &srcRect, const Recti &dstRect, const TextColors &colors) override;

    virtual void DrawOutdoorBuildings(const LightsSnapshot &lights) override;

    virtual void DrawIndoorSky(int uNumVertices, int uFaceID) override;
    virtual void DrawOutdoorSky() override;
    virtual void DrawOutdoorTerrain(const LightsSnapshot &lights) override;

    virtual RgbaImage MakeViewportScreenshot(const int width, const int height) override;
    virtual RgbaImage MakeFullScreenshot() override;

    virtual void BeginDecals() override;
    virtual void EndDecals() override;
    virtual void DrawDecal(Decal *pDecal, float z_bias) override;

    virtual void DrawIndoorFaces() override;

    virtual void ReleaseTerrain() override;
    virtual void ReleaseBSP() override;

    virtual void DrawTwodVerts() override;
    void DrawBillboards();

    virtual bool Reinitialize(bool firstInit) override;
    virtual bool ReloadShaders() override;

    virtual void flushAndScale() override;
    virtual void swapBuffers() override;

    virtual void beginOverlays() override;
    virtual void endOverlays() override;

 protected:
    virtual void DoRenderBillboards_D3D() override;
    void SetBillboardBlendOptions(RenderBillboardD3D::OpacityType a1);

    /**
     * @param texture                   Billboard texture, null for the solid particle texture.
     * @param[in,out] paletteId         Palette of the billboard, set to 0 when the texture pack has a sprite with the
     *                                  palette already applied.
     * @return                          OpenGL texture to draw the billboard with.
     */
    float billboardTextureId(GraphicsImage *texture, int *paletteId);

    /**
     * Draws `_worldBillboards` back to front, with depth test and without depth writes. Builds their quads facing the
     * current view first, unless they were built for this frame already.
     */
    void drawWorldBillboards();

    /**
     * Draws the outdoor sky as a quad covering the view at the far plane. The sky shader finds where each pixel's ray
     * hits the sky plane.
     */
    void drawWorldOutdoorSky();

    /**
     * Queues an indoor sky face for `drawSkyVertices`, in world space.
     *
     * @param face                      Sky face.
     */
    void queueWorldIndoorSky(BLVFace *face);

    /**
     * Draws `_skyVertices` through the sky shader with the current view and projection matrices.
     *
     * @param planeHeight               Height of the sky plane above the eye, in the units the sky texture tiles in.
     * @param fade                      Whether to fade the sky into its tint towards the horizon and fill with the
     *                                  tint below it. Without it, pixels below the horizon are left alone.
     */
    void drawSkyVertices(float planeHeight, bool fade);

    void DrawOutdoorSkyPolygon(int numVertices, GraphicsImage *texture, int dimmingLevel);
    void DrawIndoorSkyPolygon(int uNumVertices, GraphicsImage *texture, int dimmingLevel);
    void DrawForcePerVerts();

    /**
     * @param normalWeatherGrey         Fog colour outdoors in normal weather, where the fog never shows.
     * @return                          Fog for the forced perspective sky and projectiles, which keeps the sky foggy in
     *                                  foggy weather.
     */
    FogUniforms forcedPerspectiveFog(float normalWeatherGrey);

    void SetFogParametersGL();

    void ensurePaletteTexture();

    void _initImGui();
    void _shutdownImGui();
    void _initWaterTiles();

    FrameLimiter _frameLimiter;

    // these are the view and projection matrices for submission to shaders
    glm::mat4 projmat = glm::mat4x4(1);
    glm::mat4 viewmat = glm::mat4x4(1);
    void _set_3d_projection_matrix();
    void _set_3d_modelview_matrix();
    void _set_ortho_projection(bool gameviewport = false);
    void _set_ortho_modelview();
    void _setWorldViewport();
    void _setUiViewport();

    /**
     * @param rect                      Rectangle in UI coordinates, with y down.
     * @return                          The same rectangle in pixels of the UI target, with y up the way viewports and
     *                                  scissor rectangles take it.
     */
    Recti _uiPixels(const Recti &rect) const;

    OpenGLRenderTarget *_currentWorldTarget();

    /**
     * @param colorTexture              Colour texture owned by the caller.
     * @param size                      Size of the texture.
     * @param withDepth                 Whether to add a depth texture, when the target is made.
     * @return                          Render target around the texture, made on first use and kept until
     *                                  `forgetExternalTextures`.
     */
    OpenGLRenderTarget *_externalTarget(TextureRenderId colorTexture, Sizei size, bool withDepth);

    /**
     * @return                          Whether the targets around external textures need a depth buffer, which they only
     *                                  do when the world is drawn into them directly.
     */
    [[nodiscard]] bool _externalTargetsHaveDepth() const;

    void _bindUiTarget();

    /**
     * @return                          Whether the UI is drawn into the UI target rather than straight into the window.
     */
    bool _usesUiTarget() const;

    /**
     * Sets the blend function for drawing the UI. As a layer of its own, the UI target also has to end up with the
     * right alpha.
     */
    void _setUiBlendFunc();

    /**
     * Draws the viewport mirror and then the UI target over it into the window.
     *
     * @param rect                      Rectangle of the window the UI target is scaled into.
     */
    void _presentUiLayer(const Recti &rect);

    /**
     * @param premultiplied             Whether the queued quads have their colour premultiplied by their alpha.
     */
    void _drawTwodVerts(bool premultiplied);

    /**
     * @param width                     Width of the screenshot.
     * @param height                    Height of the screenshot.
     * @return                          The world target scaled to the given size.
     */
    RgbaImage makeWorldTargetScreenshot(int width, int height);

    /**
     * Tonemaps a linear colour texture into the bound draw framebuffer: exposure, a shoulder that rolls the
     * highlights off towards white, sRGB encoding and the user gamma, then FXAA when it's enabled. Leaves blending, depth test and scissor test
     * disabled, and changes the viewport and the texture bound to unit 0.
     *
     * @param source                    Target with linear colour, as the world shaders draw it with the linear
     *                                  pipeline. Bloom, when enabled, is drawn from it first.
     * @param viewport                  Rectangle to fill in the bound framebuffer, in OpenGL window coordinates.
     * @param filter                    `GL_LINEAR` or `GL_NEAREST`, how the source is sampled.
     */
    void drawComposite(const OpenGLRenderTarget &source, const Recti &viewport, GLenum filter);

    /**
     * Draws the bloom of a linear colour target into `_bloomTargets`. Leaves the last of them bound as the draw
     * framebuffer.
     *
     * @param source                    Target with linear colour.
     * @return                          Texture with the bloom at half the size of `source`, or 0 if `source` is too
     *                                  small to bloom.
     */
    GLuint _drawBloom(const OpenGLRenderTarget &source);

    /**
     * Compares the average display brightness of a linear colour target under the shoulder and the filmic curve, and
     * moves `_adaptationTargets` towards the exposure scale at which they match, once a frame, so that both eyes of a
     * headset share the head's adaptation.
     *
     * @param source                    Target with linear colour.
     */
    void _updateEyeAdaptation(const OpenGLRenderTarget &source);

    void _ensureFullscreenTriangle();

    /**
     * Reads the settings the world shaders are compiled for into `_linearPipeline` and `_sunShadows`.
     */
    void _readPipelineSettings();

    /**
     * Creates the sun shadow map and the depth-only vertex buffers of the loaded outdoor level, if they don't exist yet.
     */
    void _ensureSunShadowResources();

    /**
     * Creates the depth-only vertex buffers of the loaded outdoor level's terrain and buildings, if they don't exist
     * yet.
     */
    void _ensureOutdoorDepthGeometry();

    /**
     * Binds the ambient occlusion of the current view and sets the uniforms `ao.glsl` reads. Does nothing for
     * programs compiled without `AMBIENT_OCCLUSION`.
     *
     * @param shader                    Program in use.
     */
    void _submitAmbientOcclusion(const OpenGLShader &shader);

    /**
     * Loads the baked indirect light of the current map when the map changes, and blends its time keys for the time of
     * day when the game minute changes. Does nothing unless global illumination is enabled.
     */
    void _updateGlobalIllumination();

    /**
     * Binds the baked indirect light of the current map and sets the uniforms `gi.glsl` reads. Does nothing for
     * programs compiled without `GLOBAL_ILLUMINATION`.
     *
     * @param shader                    Program in use.
     */
    void _submitGlobalIllumination(const OpenGLShader &shader);

    /**
     * Sets the colours of the sky and the ground that `lighting.glsl` scales the sun's ambient light by, from the sky
     * at the current time and the baked indirect light of the map. Does nothing for programs compiled without
     * `SKY_AMBIENT`.
     *
     * @param shader                    Program in use.
     */
    void _submitSkyAmbient(const OpenGLShader &shader);

    /**
     * Binds the sun shadow map and sets the uniforms `shadow.glsl` reads. Does nothing for programs compiled without
     * `SUN_SHADOWS`.
     *
     * @param shader                    Program in use.
     */
    void _submitSunShadow(const OpenGLShader &shader);

    /**
     * Sets the uniforms `heightfog.glsl` reads. Does nothing for programs compiled without `LINEAR_PIPELINE`.
     *
     * @param shader                    Program in use.
     * @param worldPositions            Whether the program has world positions to fog. Without them it gets the stock
     *                                  fog.
     */
    void _submitHeightFog(const OpenGLShader &shader, bool worldPositions);

    /**
     * Sky and sun for the linear pipeline's sky and water, in linear colour.
     */
    struct SkyState {
        Vec3f sunDirection; // Towards the sun.
        float daylight = 0; // 0 at night and indoors, 1 with the sun clear of the horizon.
        float sunVisibility = 0; // Same, but also 0 in foggy weather.
        Colorf zenith;
        Colorf horizon;
    };

    /**
     * @return                          The sky and the sun at the current time, weather and sky texture.
     */
    SkyState _skyState();

    /**
     * Binds the water normals and sets the uniforms `water.glsl` and `sky.glsl` read. Does nothing for programs
     * compiled without `LINEAR_PIPELINE`.
     *
     * @param shader                    Program in use.
     * @param reflection                Colour the water reflects instead of the sky, in display colour. Null for the
     *                                  sky.
     */
    void _submitWater(const OpenGLShader &shader, const Colorf *reflection);

    /**
     * Copies the depth buffer of the world viewport into `_sceneDepth`, for the soft edges of additive billboards.
     */
    void _copySceneDepth();

    /**
     * Darkens the ground under the actors and decorations that stand on it, with a soft ellipse under each. Linear
     * pipeline only.
     */
    void _drawContactShadows();

    /**
     * Creates `_waterNormals` if it doesn't exist yet.
     */
    void _ensureWaterNormals();

    /**
     * Binds the torch shadow maps and sets the uniforms `torchshadow.glsl` reads. Does nothing for programs compiled
     * without `TORCH_SHADOWS`.
     *
     * @param shader                    Program in use.
     * @param layers                    Shadow map layer of each light in the program's light list, -1 for lights
     *                                  without one.
     */
    void _submitTorchShadows(const OpenGLShader &shader, std::span<const int> layers);

    /**
     * @return                          Internal format for the colour texture arrays of the world, sRGB with the
     *                                  linear pipeline so that sampling them decodes to linear.
     */
    GLint _colorArrayFormat() const;

    /**
     * @param color                     sRGB encoded colour, as the stock code computes it.
     * @return                          The colour to hand to the world shaders, decoded to linear with the linear
     *                                  pipeline, unchanged without it.
     */
    Colorf _worldColor(Colorf color) const;

    Recti clipRect;

    int GL_lastboundtex = 0;

    int GPU_MAX_TEX_SIZE = 0;
    int GPU_MAX_TEX_LAYERS = 0;
    int GPU_MAX_TEX_UNITS = 0;
    int GPU_MAX_UNIFORM_COMP = 0;
    int GPU_MAX_TOTAL_TEXTURES = 0;

    OpenGLShader terrainshader;
    OpenGLShader outbuildshader;
    OpenGLShader bspshader;
    OpenGLShader textshader;
    OpenGLShader lineshader;
    OpenGLShader twodshader;
    OpenGLShader billbshader;
    OpenGLShader decalshader;
    OpenGLShader forcepershader;
    OpenGLShader skyshader;
    OpenGLShader compositeshader;
    OpenGLShader bloomdownshader;
    OpenGLShader bloomupshader;
    OpenGLShader fxaashader;
    OpenGLShader shadowshader;
    OpenGLShader shadowspriteshader;
    OpenGLShader torchshadowshader;
    OpenGLShader contactshadowshader;
    OpenGLShader aodepthshader;
    OpenGLShader aoshader;
    OpenGLShader depthhalfshader;
    OpenGLShader aoblurshader;
    OpenGLShader luminanceshader;
    OpenGLShader adaptshader;
    OpenGLShader terrainmaterialshader; // Variants of the world shaders that sample material maps.
    OpenGLShader outbuildmaterialshader;
    OpenGLShader bspmaterialshader;

    // terrain shader
    OpenGLVertexBuffer<ShaderVertex> _terrainBuffer;
    std::array<ShaderVertex, 127 * 127 * 6> _terrainVertices;
    // all terrain textures are square
    GLuint terraintextures[8]{};
    GLuint terrainmaterials[8]{}; // Material arrays next to terraintextures, 0 for units without materials.
    unsigned int numterraintexloaded[8]{};
    unsigned int terraintexturesizes[8]{};
    std::map<std::string, int> terraintexmap;

    // outside building shader
    std::array<OpenGLVertexBuffer<ShaderVertex>, 16> _outbuildBuffers;
    std::array<std::vector<ShaderVertex>, 16> _outbuildVertices;
    GLuint outbuildtextures[16]{};
    GLuint outbuildmaterials[16]{}; // Material arrays next to outbuildtextures, 0 for units without materials.
    unsigned int numoutbuildtexloaded[16]{};
    unsigned int outbuildtexturewidths[16]{};
    unsigned int outbuildtextureheights[16]{};
    std::map<std::string, int> outbuildtexmap;

    // indoors bsp shader
    std::array<OpenGLVertexBuffer<ShaderVertex>, 16> _bspBuffers;
    std::array<std::vector<ShaderVertex>, 16> _bspVertices;
    GLuint bsptextures[16]{};
    GLuint bspmaterials[16]{}; // Material arrays next to bsptextures, 0 for units without materials.
    unsigned int bsptexloaded[16]{};
    unsigned int bsptexturewidths[16]{};
    unsigned int bsptextureheights[16]{};
    std::map<std::string, int> bsptexmap;

    // TODO(captainurist): reserve the buffers here, then adjust the flush checks to use reserved size?

    // line shader
    OpenGLVertexBuffer<LineVertex> _lineBuffer;
    std::vector<LineVertex> _lineVertices;

    // forced perspective shader
    OpenGLVertexBuffer<ForcePerVertex> _forcePerBuffer;
    std::vector<ForcePerVertex> _forcePerVertices;

    // sky shader
    OpenGLVertexBuffer<SkyVertex> _skyBuffer;
    OpenGLVertexBuffer<CompositeVertex> _compositeBuffer;

    // sun shadows
    static constexpr int SUN_SHADOW_CASCADES = 4;
    GLuint _sunShadowMap = 0; // Depth texture array, a layer per cascade.
    int _sunShadowMapSize = 0;
    std::array<GLuint, SUN_SHADOW_CASCADES> _sunShadowFramebuffers = {};
    OpenGLVertexBuffer<ShadowVertex> _sunShadowTerrainBuffer;
    OpenGLVertexBuffer<ShadowVertex> _sunShadowBuildingBuffer;
    OpenGLVertexBuffer<ShadowSpriteVertex> _sunShadowSpriteBuffer;
    std::vector<ShadowSpriteVertex> _sunShadowSpriteVertices;
    Vec3f _sunShadowDirection; // Towards the sun.
    std::vector<std::pair<int, int>> _sunShadowModelRanges; // First vertex and vertex count of each building model.
    std::array<glm::mat4, SUN_SHADOW_CASCADES> _sunShadowMatrices = {};
    std::array<float, SUN_SHADOW_CASCADES> _sunShadowTexels = {};
    float _sunShadowFade = 0;

    // ambient occlusion
    OpenGLRenderTarget _aoDepthTarget; // Half resolution, the distance along the view axis in r.
    OpenGLRenderTarget _aoTarget; // Occlusion in r and the distance in g.
    OpenGLRenderTarget _aoBlurTarget; // Same, blurred.
    OpenGLVertexBuffer<ShadowVertex> _aoIndoorBuffer; // Every visible face of the loaded indoor level.
    int _aoIndoorVertexCount = 0;
    int _aoIndoorVersion = 0; // `IndoorLocation::geometryVersion` of `_aoIndoorBuffer`.
    Recti _aoViewport; // World viewport the occlusion was drawn for.

    // soft particles
    GLuint _sceneDepth = 0; // Copy of the world's depth buffer, taken before the billboards.
    OpenGLRenderTarget _sceneDepthHalf; // The world's depth at half resolution, taken instead from a world target.
    GLuint _sceneDepthTexture = 0; // Whichever of the two the billboards sample.
    GLuint _sceneDepthFramebuffer = 0;
    Sizei _sceneDepthSize;
    Recti _sceneDepthViewport; // World viewport `_sceneDepth` was copied from.
    bool _sceneDepthCopied = false; // Whether `_sceneDepth` holds the current view.

    // contact shadows
    OpenGLVertexBuffer<ContactShadowVertex> _contactShadowBuffer;
    std::vector<ContactShadowVertex> _contactShadowVertices;

    // linear pipeline sky and water
    GLuint _waterNormals = 0; // Tiling normal map of small waves.
    GraphicsImage *_skyAverageTexture = nullptr;
    Colorf _skyAverage; // Average colour of `_skyAverageTexture`.

    // torch shadows
    GLuint _torchShadowMaps = 0; // Depth cube map array, a cube per light.
    int _torchShadowCount = 0; // Cubes in `_torchShadowMaps`.
    std::vector<GLuint> _torchShadowFramebuffers; // One per cube face.
    OpenGLVertexBuffer<TorchShadowVertex> _torchShadowBuffer;
    std::vector<TorchShadowVertex> _torchShadowVertices;
    std::vector<int> _torchShadowLights; // Indices into the stationary light stack, the cube of each is its position here.
    std::vector<std::pair<Vec3f, int>> _torchShadowDrawnLights; // Position and radius of the lights the cubes hold.
    int _torchShadowDrawnVersion = 0; // `IndoorLocation::geometryVersion` the cubes were drawn at.

    std::vector<std::unique_ptr<OpenGLRenderTarget>> _bloomTargets; // Half the world target's size, then halving.
    OpenGLRenderTarget _luminanceTarget; // Display brightness of the world under both curves, with mipmaps that average it.
    std::array<OpenGLRenderTarget, 2> _adaptationTargets; // 1x1 exposure scales, the current one and the previous.
    int _adaptationIndex = 0; // Which of `_adaptationTargets` is current.
    uint64_t _frameNumber = 0; // Frames swapped so far.
    uint64_t _adaptedFrame = UINT64_MAX; // Frame the adaptation was last updated in.
    int64_t _adaptedTime = 0; // Platform tick count of that update, in milliseconds.
    OpenGLRenderTarget _antialiasingTarget; // Tonemapped world at the size it's composited at, the input to FXAA.
    std::vector<SkyVertex> _skyVertices;

    // two d shader
    OpenGLVertexBuffer<TwoDVertex> _twodBuffer;
    std::vector<TwoDVertex> _twodVertices;

    // text shader
    OpenGLVertexBuffer<TextVertex> _textBuffer;
    std::vector<TextVertex> _textVertices;
    GLuint _textTexture{};
    Sizei _textAtlasSize;

    // billboards shader
    OpenGLVertexBuffer<BillboardVertex> _billboardBuffer;
    std::vector<BillboardVertex> _billboardVertices;
    GLuint paltex2D{};

    // decal shader
    OpenGLVertexBuffer<DecalVertex> _decalBuffer;
    std::vector<DecalVertex> _decalVertices;

    FogUniforms fog;

    float gamma = 0;

    std::array<GraphicsImage *, 7> hd_water_tile_anim = {{}}; // Water animation textures.

    std::vector<GLuint> _texturesForDeletion;
    std::unordered_set<GLuint> _mipmappedTextures; // Created with `TEXTURE_FILTER_MIPMAPPED`.

 private:
    CameraView _view;
    OpenGLRenderTarget _uiTarget;
    OpenGLRenderTarget _worldTarget;
    std::map<GLuint, std::unique_ptr<OpenGLRenderTarget>> _externalTargets; // By colour texture.
    OpenGLRenderTarget *_externalWorldTarget = nullptr;
    OpenGLRenderTarget *_externalOutput = nullptr; // Wraps the colour texture given to `setExternalWorldTarget`.
    OpenGLRenderTarget _externalLinearTarget; // What the world is drawn into for an external target with the linear pipeline.
    OpenGLRenderTarget _mirrorTarget;

    /**
     * Texture that the 2D drawing goes into instead of the UI, see `beginUiCard`.
     */
    struct UiCard {
        OpenGLRenderTarget *target = nullptr;
        Recti screenRect; // Rectangle of the game screen that fills the target.
    };

    std::optional<UiCard> _uiCard;
    bool _mirrorPending = false;
    bool _mirrorPresentPending = false; // The UI layer leaves the mirror for `flushAndScale`.
    bool _uiLayer = false;
    bool _linearPipeline = false; // Decided once at startup, the world shaders are compiled for it.
    bool _sunShadows = false; // Decided once at startup, the outdoor shaders are compiled for it.
    bool _torchShadows = false; // Same for the indoor shaders.
    bool _ambientOcclusion = false; // Same for the lit world shaders.
    bool _globalIllumination = false; // Same for the lit world shaders and the billboards.
    bool _skyAmbient = false; // Same for the outdoor world shaders and the billboards.
    MapId _giMap = MAP_INVALID; // Map that `_giProbes` were looked up for.
    std::optional<GiProbes> _giProbes; // Baked indirect light of `_giMap`, empty if the texture pack has none.
    std::vector<float> _giKeys; // Coefficients of each time key as floats, laid out as `_giTexture` holds them.
    int _giBlend = -1; // Pair of time keys and step between them that `_giTexture` holds, -1 if none.
    Vec3f _giAverage; // Mean irradiance where the party walks, at that step.
    GLuint _giTexture = 0;
    GLuint _giTerrainHeights = 0;
    bool _terrainVariation = false; // Same for the terrain shaders.
    bool _spriteCrossfade = false; // Same for the billboard shader.
    bool _spriteDepth = false; // Same.
    bool _spriteLighting = false; // Same.
    bool _filmicTonemap = false; // Same for the composite shader.
    bool _eyeAdaptation = false; // Same.
    SpriteLightUniforms _spriteLights; // Lights of the level pass in the current view.
    bool _billboardDepthPrepass = false; // `DrawBillboards` only writes the depth of the sprites' opaque parts.
    bool _billboardShapeColourPass = false; // `DrawBillboards` draws the colour of the sprites of the depth prepass.
    int _uiScale = 1; // UI target pixels per UI coordinate, decided once at startup.
    bool _externalFramePacing = false;
};


