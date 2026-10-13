#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "Library/Image/Image.h"
#include "Library/Color/Color.h"
#include "Library/Color/ColorTable.h"
#include "Library/Geometry/Rect.h"
#include "Engine/HitMap.h"

#include "RendererEnums.h"
#include "TextureRenderId.h"
#include "Engine/Graphics/ParticleEngine.h"
#include "Engine/Graphics/RenderEntities.h"

class Actor;
struct CameraView;
struct LightsSnapshot;
class GraphicsImage;
class GameConfig;
class Sprite;
class SpriteFrame;
struct DecalBuilder;
class ParticleEngine;
struct SpellFxRenderer;
struct SpellFX_Billboard;
class Vis;
struct Decal;
struct nk_context;

bool PauseGameDrawing();

/**
 * Colors for the up to four color channels of a font texture. Unused channels default to transparent black.
 *
 * @see Renderer::DrawTextNew
 */
struct TextColors : std::array<Color, 4> {
    TextColors() = default;
    TextColors(Color color0, Color color1 = Color(), Color color2 = Color(), Color color3 = Color()) // NOLINT: implicit conversion is intended.
        : std::array<Color, 4>({{color0, color1, color2, color3}}) {}
};

class Renderer {
 public:
    Renderer(
        std::shared_ptr<GameConfig> config,
        DecalBuilder *decal_builder,
        SpellFxRenderer *spellfx,
        std::shared_ptr<ParticleEngine> particle_engine,
        Vis *vis
    );
    virtual ~Renderer();

    virtual void Initialize() = 0;

    virtual RgbaImage ReadScreenPixels() = 0;
    virtual void ClearTarget(Color uColor) = 0;
    virtual void Present() = 0;

    virtual void BeginLines2D() = 0;
    virtual void EndLines2D() = 0;
    virtual void RasterLine2D(Pointi a, Pointi b, Color acolor, Color bcolor) = 0;

    void RasterLine2D(Pointi a, Pointi b, Color color) {
        RasterLine2D(a, b, color, color);
    }

    virtual void BeginScene3D(const CameraView &view) = 0;

    /**
     * Ends the 3D pass by drawing the 2D quads queued during it, like the screen fade, into the world target.
     */
    virtual void EndScene3D() = 0;

    /**
     * @return                          Rectangle of the world target the 3D world is drawn into, to use as the
     *                                  `CameraView` viewport. That's the game viewport unless a world target size is
     *                                  configured.
     */
    virtual Recti worldTargetViewport() = 0;

    /**
     * Draws the world target into the game viewport of the UI target. Does nothing when the world is drawn straight
     * into the game viewport.
     */
    virtual void compositeWorldTarget() = 0;

    /**
     * @return                          Colour texture of the UI target, or an empty id when the UI is drawn straight
     *                                  into the window.
     */
    virtual TextureRenderId uiTargetTexture() = 0;

    /**
     * @return                          Size of the UI target in pixels, the render dimensions times
     *                                  `graphics.ui_scale`.
     */
    virtual Sizei uiTargetSize() = 0;

    /**
     * Makes the following 3D passes draw into a colour texture owned by the caller instead of the world target.
     *
     * @param colorTexture              Texture to draw into, or an empty id to go back to the world target.
     * @param size                      Size of the texture.
     */
    virtual void setExternalWorldTarget(TextureRenderId colorTexture, Sizei size) = 0;

    /**
     * Copies a colour texture, cropped to the aspect ratio of the game viewport, for the next `compositeWorldTarget`
     * to draw into the game viewport instead of the world target.
     *
     * @param colorTexture              Texture to copy.
     * @param size                      Size of the texture.
     */
    virtual void setViewportMirror(TextureRenderId colorTexture, Sizei size) = 0;

    /**
     * @param external                  Whether something other than the frame rate limiter, like a VR runtime, paces
     *                                  the frames.
     */
    virtual void setExternalFramePacing(bool external) = 0;

    /**
     * Draws the sun's shadow maps for the 3D passes of this frame. Does nothing unless sun shadows are enabled and an
     * outdoor level is loaded. Call once per frame, between `WorldFrame::prepare` and the first draw. The shadow
     * cascades cover what `pCamera3D` sees, which in VR is the head's culling camera.
     *
     * @param casterModels              Indices into `OutdoorLocation::pBModels` of the building models that can cast
     *                                  shadows into the shadow distance.
     * @param casterSprites             Trees, other decorations and monsters that cast shadows into the shadow
     *                                  distance.
     */
    virtual void drawSunShadows(std::span<const int> casterModels, std::span<const SunShadowSprite> casterSprites) = 0;

    /**
     * Draws the shadow maps of the indoor lights nearest to the camera for the 3D passes of this frame. Does nothing
     * unless torch shadows are enabled and an indoor level is loaded. Call once per frame, between
     * `WorldFrame::prepare` and the first draw.
     */
    virtual void drawTorchShadows() = 0;

    /**
     * Draws the ambient occlusion of the current 3D view, which the world passes after it read. Does nothing unless
     * ambient occlusion is enabled. Call once per view, after `BeginScene3D` and before the level is drawn.
     */
    virtual void drawAmbientOcclusion() = 0;

    /**
     * @return                          Whether the current 3D pass draws sprites, particles, projectiles and the sky in
     *                                  world space through the view and projection matrices. That's the case when
     *                                  `graphics.world_space_billboards` is set, and always for external world targets,
     *                                  which are drawn from views other than the party camera.
     */
    /**
     * @return                          Whether world space sprites are drawn with the depth of their texture pack
     *                                  shapes.
     */
    virtual bool spriteShapes() = 0;

    virtual bool worldSpaceBillboards() = 0;

    /**
     * Draws a projectile beam, like a lightning bolt, between two points in world space. Only valid when
     * `worldSpaceBillboards` is true, `DrawProjectile` draws it otherwise.
     *
     * @param src                       Start of the beam.
     * @param dst                       End of the beam.
     * @param texture                   Beam texture.
     */
    virtual void drawWorldProjectile(Vec3f src, Vec3f dst, GraphicsImage *texture) = 0;

    /**
     * Turns this frame's world space billboards into quads facing a view, for every 3D pass of the frame to draw the
     * same geometry. Without a call, the first pass that draws them builds them facing its own view's plane.
     *
     * @param view                      View for the quads to face, its roll is taken out.
     * @param facing                    What upright sprites turn to face.
     */
    virtual void buildWorldBillboardQuads(const CameraView &view, SpriteFacing facing) = 0;

    /**
     * @param index                     Index into `pBillboardRenderListD3D`.
     * @return                          World space billboard at that index, or null if this frame's quads weren't
     *                                  built.
     */
    virtual const WorldBillboard *builtWorldBillboard(int index) = 0;

    /**
     * Turns the sprites among this frame's built world space billboards to show the side of their frame that faces
     * an eye, for views that see them from beside the camera the frame was prepared with. A sprite keeps the side it
     * showed the same view in the previous frame until the eye is a little past the boundary to the next side, so
     * that a small movement of the eye across the boundary doesn't make it flicker.
     *
     * @param eye                       Position of the eye in world space.
     * @param view                      Which of the frame's views it is, from 0 to 3, for the side kept from the
     *                                  previous frame.
     */
    virtual void turnWorldSpritesTo(const Vec3f &eye, int view) = 0;

    /**
     * Makes the UI a layer of its own for an external compositor, like a VR runtime. The UI is then drawn into the UI
     * target over transparent black, with nothing in the game viewport, and the window gets the viewport mirror under
     * it when the frame is presented.
     *
     * @param enabled                   Whether the UI is a layer of its own.
     */
    virtual void setUiLayer(bool enabled) = 0;

    /**
     * @return                          Whether the UI is a layer of its own, see `setUiLayer`.
     */
    virtual bool uiLayer() = 0;

    /**
     * Copies the UI target into a colour texture owned by the caller. Only valid when `uiLayer` is true.
     *
     * @param colorTexture              Texture to copy into.
     * @param size                      Size of the texture, the UI is scaled to fit it.
     */
    virtual void copyUiLayer(TextureRenderId colorTexture, Sizei size) = 0;

    /**
     * Sends the 2D drawing that follows into a colour texture owned by the caller instead of the UI, until `endUiCard`,
     * with a rectangle of the game screen filling the texture. The texture is cleared to transparent black first. The
     * 2D calls and clip rectangles keep using the game screen's coordinates.
     *
     * @param colorTexture              Texture to draw into.
     * @param size                      Size of the texture.
     * @param screenRect                Rectangle of the game screen that fills the texture.
     */
    virtual void beginUiCard(TextureRenderId colorTexture, Sizei size, const Recti &screenRect) = 0;

    /**
     * Draws what is still queued into the texture given to `beginUiCard`, and sends the 2D drawing back to the UI.
     */
    virtual void endUiCard() = 0;

    /**
     * Copies a rectangle of the UI target into a colour texture owned by the caller, pixel for pixel. Only valid when
     * `uiLayer` is true.
     *
     * @param colorTexture              Texture to copy into.
     * @param size                      Size of the texture.
     * @param rect                      Rectangle to copy, in render coordinates.
     * @param position                  Where the copy's bottom left corner goes in the texture, in pixels from its
     *                                  bottom left. The copy takes up `rect` times the UI scale.
     */
    virtual void copyUiLayerRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Pointi position) = 0;

    /**
     * Fills a rectangle of a colour texture owned by the caller with one colour.
     *
     * @param colorTexture              Texture to fill.
     * @param size                      Size of the texture.
     * @param rect                      Rectangle to fill, in pixels from the texture's bottom left.
     * @param color                     Colour to fill it with, alpha included.
     */
    virtual void fillExternalRect(TextureRenderId colorTexture, Sizei size, const Recti &rect, Color color) = 0;

    /**
     * Releases what the renderer keeps for colour textures owned by the caller, before the caller deletes them.
     *
     * @param colorTextures             Textures the caller is about to delete.
     */
    virtual void forgetExternalTextures(std::span<const TextureRenderId> colorTextures) = 0;

    /**
     * Adds a billboard to this frame's list that has no screen space version, for 3D passes that draw world space
     * billboards. A pass that draws screen space billboards draws nothing for it.
     *
     * @param billboard                 Billboard to add, sorted by its distance from the camera.
     */
    virtual void addWorldBillboard(const WorldBillboard &billboard) = 0;

    virtual void MakeParticleBillboardAndPush(const Particle& p) = 0;
    virtual float GetGamma() = 0;

    virtual void DrawBillboards_And_MaybeRenderSpecialEffects_And_EndScene() = 0;
    virtual void BillboardSphereSpellFX(SpellFX_Billboard *a1, Color diffuse) = 0;
    virtual void TransformBillboards() = 0;
    virtual void resetBillboardRenderList() = 0;
    virtual void sortBillboardRenderList() = 0;

    /**
     * Adds decals for this frame's bloodsplats to the decal builder, clipped to the terrain and building faces
     * outdoors or to the faces of the visible sectors indoors. Decals are lit by the light stacks as they are when
     * this is called.
     */
    virtual void buildBloodsplatDecals() = 0;

    /**
     * Adds a sprite to this frame's billboards if the camera sees it.
     *
     * @param frame                     Frame to draw one side of.
     * @param octant                    Index of the side in `frame->sprites` that faces the camera.
     * @param yaw                       Facing of the object, which picks the side for other views.
     * @param pos                       Bottom centre of the sprite in world space.
     * @param scale                     World units per sprite pixel.
     * @param flags                     How to draw the sprite.
     * @param id                        Object the sprite shows.
     * @param sector                    Indoor sector of the object.
     * @return                          Whether the sprite was added.
     */
    virtual bool AddBillboardIfVisible(const SpriteFrame *frame, int octant, int yaw, const Vec3f &pos, const Vec2f &scale, BillboardFlags flags,
                                       Pid id, int sector = 0) = 0;

    virtual void DrawProjectile(float srcX, float srcY, float a3, float a4,
                                float dstX, float dstY, float a7, float a8,
                                GraphicsImage *texture) = 0;

    virtual TextureRenderId CreateTexture(RgbaImageView image, TextureFilter filter) = 0;

    /**
     * Replaces the pixels of a texture made by `CreateTexture`.
     *
     * @param id                        Texture to update.
     * @param image                     New pixels, the same size as the texture.
     */
    virtual void updateTexture(TextureRenderId id, RgbaImageView image) = 0;

    virtual void DeleteTexture(TextureRenderId id) = 0;

    virtual void BeginScene2D() = 0;
    virtual void ScreenFade(Color color, float t) = 0;

    virtual void SetUIClipRect(const Recti &rect) = 0;
    virtual void ResetUIClipRect() = 0;

    /**
     * Core 2D quad drawing function. All other 2D texture drawing functions (except `DrawImage`) delegate to this one.
     *
     * @param texture                   The texture to draw.
     * @param srcRect                   Source rectangle in texture pixel coordinates.
     * @param dstRect                   Destination rectangle in screen pixels.
     * @param color                     Multiplicative color mask applied to the texture. Use `colorTable.White` for no
     *                                  tinting. RGB components scale the texture colors, alpha controls transparency.
     */
    virtual void DrawQuad2D(GraphicsImage *texture, const Recti &srcRect, const Recti &dstRect, Color color = colorTable.White) = 0;
    virtual void DrawQuad2D(GraphicsImage *texture, const Recti &srcRect, Pointi dstPoint, Color color = colorTable.White);
    virtual void DrawQuad2D(GraphicsImage *texture, Pointi dstPoint, Color color = colorTable.White);

    void FillRect(const Recti &rect, Color color);

    // TODO(captainurist): DrawImage uses palette, should be refactored to use a separate palette shader.
    virtual void DrawImage(GraphicsImage *, const Recti &rect, int paletteid = 0, Color colourmask32 = colorTable.White) = 0;

    virtual void BlendTextures(int a2, int a3, GraphicsImage *a4, GraphicsImage *a5, int t, int start_opacity, int end_opacity) = 0;
    virtual void DrawMonsterPortrait(const Recti &rc, SpriteFrame *Portrait_Sprite, int Y_Offset) = 0;

    virtual void BeginTextNew(GraphicsImage *texture) = 0;
    virtual void EndTextNew() = 0;
    virtual void DrawTextNew(const Recti &srcRect, const Recti &dstRect, const TextColors &colors) = 0;

    virtual void DrawOutdoorBuildings(const LightsSnapshot &lights) = 0;

    virtual void DrawIndoorSky(int uNumVertices, int uFaceID = 0) = 0;
    virtual void DrawOutdoorSky() = 0;

    virtual void PrepareDecorationsRenderList_ODM() = 0;
    virtual void DrawSpriteObjects() = 0;

    virtual void DrawOutdoorTerrain(const LightsSnapshot &lights) = 0;

    /**
     * Takes a screenshot of the game viewport, w/o the UI elements.
     *
     * @param width                         Required width
     * @param height                        Required height.
     * @return                              Taken screenshot image.
     */
    virtual RgbaImage MakeViewportScreenshot(int width, int height) = 0;

    virtual RgbaImage MakeFullScreenshot() = 0;

    virtual std::vector<Actor *> getActorsInViewport(int pDepth) = 0;

    virtual void BeginDecals() = 0;
    virtual void EndDecals() = 0;
    virtual void DrawDecal(Decal *pDecal, float z_bias) = 0;

    virtual void DrawSpecialEffectsQuad(GraphicsImage *texture, int palette) = 0;

    virtual void DrawIndoorFaces() = 0;

    virtual void ReleaseTerrain() = 0;
    virtual void ReleaseBSP() = 0;

    virtual void DrawTwodVerts() = 0;

    virtual Sizei GetRenderDimensions() = 0;
    virtual Sizei GetPresentDimensions() = 0;
    virtual Pointi MapToRender(Pointi position) = 0;
    virtual Pointi MapToPresent(Pointi position) = 0;

    virtual bool Reinitialize(bool firstInit = false) = 0;
    virtual bool ReloadShaders() = 0;

    /**
     * Picks up changed linear pipeline, sun shadow and world target settings: recompiles the world shaders, which are
     * built for them, and recreates the render targets.
     */
    virtual void applyPipelineSettings() = 0;
    virtual void DoRenderBillboards_D3D() = 0;

    virtual void flushAndScale() = 0;
    virtual void swapBuffers() = 0;
    virtual void beginOverlays() = 0;
    virtual void endOverlays() = 0;

    std::shared_ptr<GameConfig> config = nullptr;

    Color uFogColor;

    static const int MAX_BILLBOARDS_D3D = 5000;
    RenderBillboardD3D pBillboardRenderListD3D[MAX_BILLBOARDS_D3D];
    std::array<RenderBillboardD3D*, MAX_BILLBOARDS_D3D> pSortedBillboardRenderListD3D;
    unsigned int uNumBillboardsToDraw = 0; // TODO(captainurist): this is not properly cleared if resetBillboardRenderList is not called,
                                       //                     resulting in dangling textures in pBillboardRenderListD3D.

    int drawcalls = 0;
    int worldBillboardsDrawn = 0; // World space billboards drawn since the last `BeginScene3D`.

    /**
     * @returns                         1x1 white texture for solid color fills. Initialized lazily on first use.
     */
    GraphicsImage *solidFillTexture();

 protected:
    void releaseSolidFillTexture();

    DecalBuilder *decal_builder = nullptr;
    SpellFxRenderer *spell_fx_renderer = nullptr;
    std::shared_ptr<ParticleEngine> particle_engine = nullptr;
    Vis *vis = nullptr;

 private:
    std::unique_ptr<GraphicsImage> _solidFillTexture;
};

extern Renderer *render;

extern RenderBillboard pBillboardRenderList[500];
extern int uNumBillboardsToDraw;
extern int uNumDecorationsDrawnThisFrame;
extern int uNumSpritesDrawnThisFrame;

extern RenderVertexSoft VertexRenderList[50];
extern RenderVertexSoft array_73D150[20];


struct SkyBillboardStruct {
    void CalcSkyFrustumVec(int a2, int a3, int a4, int a5, int a6, int a7);

    float field_0_party_dir_x = 0;  // cam view transform
    float field_4_party_dir_y = 0;
    float field_8_party_dir_z = 0;
    float CamVecLeft_Y = 0;
    float CamVecLeft_X = 0;
    float CamVecLeft_Z = 0;
    float CamVecFront_Y = 0;
    float CamVecFront_X = 0;
    float CamVecFront_Z = 0;
    float CamLeftDot = 0;
    float CamFrontDot = 0;
};

extern SkyBillboardStruct SkyBillboard;

Color GetActorTintColor(int max_dim, int min_dim, float distance, int a4, const RenderBillboard *a5);

void UpdateObjects();
