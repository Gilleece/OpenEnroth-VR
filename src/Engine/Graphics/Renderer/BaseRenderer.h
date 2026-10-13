#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Renderer.h"

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
