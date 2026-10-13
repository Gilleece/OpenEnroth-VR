#pragma once

#include <array>
#include <memory>
#include <string>
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
    virtual void buildWorldBillboardQuads(const CameraView &view) override;
    virtual const WorldBillboard *builtWorldBillboard(int index) override;
    virtual void sortBillboardRenderList() override;
    virtual void buildBloodsplatDecals() override;
    virtual bool AddBillboardIfVisible(Sprite* spr, int palette, const Vec3f& pos, const Vec2f& scale, BillboardFlags flags, Pid id, int sector = 0) override;

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

 private:
    void updateRenderDimensions();
    void buildTerrainBloodsplatDecals();
    void buildBuildingBloodsplatDecals();
    void buildIndoorBloodsplatDecals();
};
