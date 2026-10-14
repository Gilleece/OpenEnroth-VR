#pragma once

#include <vector>

#include "Engine/Graphics/LightsStack.h"
#include "Engine/Graphics/RenderEntities.h"

class GraphicsImage;

/**
 * The 3D world part of a frame, split into a `prepare` step that updates all the engine state the frame needs and a
 * `draw` step that only reads it.
 *
 * A frame is `Renderer::BeginScene3D`, `Renderer::resetBillboardRenderList`, `prepare`, one or more draws, and
 * `finish`.
 */
class WorldFrame {
 public:
    /**
     * Builds the loaded level's light stacks, billboard lists, particles and bloodsplat decals for this frame. Does
     * nothing when no level is loaded.
     */
    void prepare();

    /**
     * Draws the sun's or the indoor lights' shadow maps for the prepared level. Call once per frame after `prepare` and
     * before the draws, all views of the frame share the shadow maps.
     */
    void drawShadows();

    /**
     * Draws the prepared level, its bloodsplat decals, the billboards and the special effects from the view passed to
     * `Renderer::BeginScene3D`. Changes no engine state, so it can run more than once per frame. Without a `prepare`
     * this frame, only the billboards and the special effects are drawn.
     */
    void draw();

    /**
     * Same as `draw`, without the bloodsplat decals.
     */
    void drawWithoutDecals();

    /**
     * Advances the special effects by a frame. Call once, after the last draw.
     */
    void finish();

    [[nodiscard]] bool prepared() const {
        return _prepared;
    }

    /**
     * Picks the texture pack images of the decorations and actors that the first frame drawn in the loaded level will
     * draw, for `preloadSpritesStep` to load: the sprite sides that face its culling camera within that camera's view,
     * with their shapes and crossfade sides, and the sides that face the sun of those that cast sun shadows. Picks none
     * without a texture pack.
     */
    void beginSpritePreload();

    /**
     * Loads the next few of the images that `beginSpritePreload` picked.
     *
     * @return                          Whether there were images left to load.
     */
    bool preloadSpritesStep();

 private:
    void prepareOutdoor();
    void prepareIndoor();
    void prepareSunShadowCasters();
    void drawLevel();
    void drawOutdoor();
    void drawIndoor();

 private:
    LightsSnapshot _staticGeometryLights;
    std::vector<int> _sunShadowModels; // Building models that can cast sun shadows, culled by distance only.
    std::vector<SunShadowSprite> _sunShadowSprites; // Same for decorations and actors.
    std::vector<GraphicsImage *> _spritePreload;
    size_t _spritePreloaded = 0; // How many of `_spritePreload` are loaded.
    bool _prepared = false;
};
