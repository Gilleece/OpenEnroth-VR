#pragma once

#include <span>
#include <string>

#include "BaseRenderer.h"

class NullRenderer : public BaseRenderer {
 public:
    using BaseRenderer::BaseRenderer;
    virtual ~NullRenderer();

    virtual void Initialize() override;
    virtual bool Reinitialize(bool firstInit) override;

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
    virtual void DrawQuad2D(GraphicsImage *texture, Pointi dstPoint, Color color) override;
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

    virtual bool ReloadShaders() override;

    virtual void DoRenderBillboards_D3D() override;
    virtual void buildBloodsplatDecals() override;

    virtual void flushAndScale() override;
    virtual void swapBuffers() override;

    virtual void beginOverlays() override;
    virtual void endOverlays() override;
};
