#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Renderer.h"

#include "Engine/Graphics/ImageDecodeQueue.h"

class BaseRenderer : public Renderer {
 public:
    inline BaseRenderer(
        std::shared_ptr<GameConfig> config,
        DecalBuilder *decal_builder,
        SpellFxRenderer *spellfx,
        std::shared_ptr<ParticleEngine> particle_engine,
        Vis *vis
    ) : Renderer(config, decal_builder, spellfx, particle_engine, vis) {
    }

    virtual void Initialize() override;

    virtual void TransformBillboards() override;
    virtual void resetBillboardRenderList() override;
    virtual void buildWorldBillboardQuads(const CameraView &view, SpriteFacing facing) override;
    virtual const WorldBillboard *builtWorldBillboard(int index) override;
    virtual void turnWorldSpritesTo(const Vec3f &eye, int view) override;
    virtual void addSpriteSideImages(const SpriteFrame *frame, Vec3f pos, int yaw, Vec3f eye, std::vector<GraphicsImage *> *images) override;
    virtual void installDecodedImages() override;
    virtual void flushImageDecodes() override;
    virtual void beginLevelImageStats() override;
    virtual void sortBillboardRenderList() override;
    virtual void buildBloodsplatDecals() override;
    virtual bool AddBillboardIfVisible(const SpriteFrame *frame, int octant, int yaw, const Vec3f &pos, const Vec2f &scale, BillboardFlags flags,
                                       Pid id, int sector = 0) override;

    virtual void DrawSpriteObjects() override;
    virtual void PrepareDecorationsRenderList_ODM() override;
    virtual void addWorldBillboard(const WorldBillboard &billboard) override;
    virtual void MakeParticleBillboardAndPush(const Particle& p) override;
    virtual float GetGamma() override;

    virtual void BillboardSphereSpellFX(SpellFX_Billboard *a1, Color diffuse) override;
    virtual void DrawMonsterPortrait(const Recti &rc, SpriteFrame *Portrait_Sprite, int Y_Offset) override;
    virtual void DrawSpecialEffectsQuad(GraphicsImage *texture, int palette) override;
    virtual void DrawBillboards_And_MaybeRenderSpecialEffects_And_EndScene() override;

    virtual std::vector<Actor*> getActorsInViewport(int pDepth) override;

    bool Reinitialize(bool firstInit) override;

    virtual Sizei GetRenderDimensions() override;
    virtual Sizei GetPresentDimensions() override;
    virtual Pointi MapToRender(Pointi position) override;
    virtual Pointi MapToPresent(Pointi position) override;

 protected:
    unsigned int NextBillboardIndex();
    void TransformBillboard(const RenderBillboard *pBillboard, int parent);

 protected:
    Sizei outputRender = {0, 0};
    Sizei outputPresent = {0, 0};

    /**
     * World space versions of the billboards in `pBillboardRenderListD3D`, at the same indices. Built along with them,
     * so they're sorted through `pSortedBillboardRenderListD3D` too.
     */
    std::array<WorldBillboard, MAX_BILLBOARDS_D3D> _worldBillboards;
    bool _worldBillboardQuadsBuilt = false; // This frame, by `buildWorldBillboardQuads`.
    Vec3f _worldBillboardEye; // Eye position of the view the quads were built for.
    std::pair<Vec3f, Vec3f> _worldBillboardAxes; // Right and up axes of the quads that face the view they were built for.
    Vec3f _worldSpriteRight; // Level right axis of that view, which upright sprites stand across when they face its plane.
    SpriteFacing _spriteFacing = SPRITE_FACING_VIEW_PLANE; // Of the quads built this frame.

    /**
     * Side of its sprite frame that an object last showed in one of the views, for keeping it near a boundary.
     */
    struct ShownSpriteSide {
        int octant = 0;
        int64_t frame = -1; // Value of `_spriteSideFrame` when it was shown.
    };

    std::unordered_map<uint32_t, ShownSpriteSide> _shownSpriteSides; // By object and view, see `turnWorldSpritesTo`.
    int64_t _spriteSideFrame = 0; // Counts the frames whose quads were built.

    /**
     * Decodes sprite images off the game thread while a headset paces the frames, null otherwise.
     */
    std::unique_ptr<ImageDecodeQueue> _imageDecodes;

    /**
     * @param async                     Whether sprite images are decoded off the game thread, with the sprites whose
     *                                  images aren't ready drawn from their last side or the stock sprite meanwhile.
     */
    void setAsyncImages(bool async);

 private:
    /**
     * Last side of its sprite frame that an object showed with its images ready, and how long it has waited since.
     */
    struct HeldSpriteSide {
        const Sprite *sprite = nullptr;
        bool mirrored = false;
        int64_t frame = -1; // Value of `_spriteSideFrame` when it was last seen.
        int64_t waitStart = -1; // When the side it should show started waiting for its images, -1 when it isn't waiting.
        bool holding = false; // Whether it keeps `sprite` while waiting, which it does if it showed it just before.
        bool stock = false; // Whether it showed the stock sprite while waiting.
    };

    /**
     * How the sprite images kept up since the last level load.
     */
    struct SpriteImageStats {
        int waits = 0; // Sides that waited for their images.
        int held = 0; // Of them, those that kept the object's last side meanwhile.
        int stock = 0; // Those drawn from the stock sprite meanwhile.
        int64_t longestHold = 0; // In frames.
        int64_t longestWait = 0; // In frames, until the images were ready.
        int decoded = 0; // Images decoded off the game thread.
        float decodeMs = 0; // Worker time they took.
        int64_t startFrame = 0; // Value of `_spriteSideFrame` at the load.
        bool logged = false;
    };

    std::unordered_map<uint32_t, HeldSpriteSide> _heldSpriteSides; // By object.
    SpriteImageStats _imageStats;

    /**
     * @param texture                   Sprite image a billboard shows.
     * @param paletteId                 Palette it's drawn with.
     * @param shape                     Shape the billboard is drawn with, if any.
     * @return                          Whether the texture pack variant and the shape that go with it have their
     *                                  textures. The ones that don't are queued for decoding.
     */
    bool spriteImagesReady(GraphicsImage *texture, int paletteId, GraphicsImage *shape);

    /**
     * Makes a sprite billboard show only images that are ready, while sprite images are decoded off the game thread.
     * A side whose images aren't ready is replaced by the object's last side for a few frames, and then by its stock
     * sprite without a shape. A crossfade side that isn't ready isn't blended in.
     *
     * @param billboard                 Billboard with its side shown for the view.
     */
    void useReadySpriteImages(WorldBillboard *billboard);

    /**
     * Shows a side of a sprite on its own, without the neighbouring side blended in.
     *
     * @param billboard                 Sprite billboard.
     * @param sprite                    Side to show.
     * @param mirrored                  Whether to mirror it.
     * @param withShape                 Whether to give it its shape. Without one it's drawn flat.
     */
    void showSpriteSideAlone(WorldBillboard *billboard, const Sprite *sprite, bool mirrored, bool withShape);

    /**
     * Queues the images of the next frame of a sprite billboard's animation, on the same side, for decoding once
     * everything else is.
     *
     * @param billboard                 Sprite billboard.
     */
    void prefetchNextSpriteFrame(const WorldBillboard &billboard);

    void logImageStats();

 private:
    void updateRenderDimensions();
    void buildWorldBillboardQuad(WorldBillboard *billboard);

    /**
     * Sets up the blend of a sprite billboard with its neighbouring side when `eye` sees it near the boundary between
     * the two, or clears it. Does nothing to other billboards.
     *
     * @param billboard                     Billboard showing its side, with `texture`, `facing` and `octant` set.
     * @param eye                           Position the billboard is seen from.
     */
    void crossfadeSpriteSides(WorldBillboard *billboard, const Vec3f &eye);

    /**
     * Grows a sprite billboard's quad to cover a second side and the parallax of its shape, and maps the sides onto
     * it. Does nothing to a billboard with neither.
     *
     * @param billboard                     Billboard showing its side, with `sideExtent` and `shape` set.
     * @param otherExtent                   Half width and height of the second side, zero for none.
     */
    void fitSpriteQuad(WorldBillboard *billboard, Vec2f otherExtent);
    void buildTerrainBloodsplatDecals();
    void buildBuildingBloodsplatDecals();
    void buildIndoorBloodsplatDecals();
};
