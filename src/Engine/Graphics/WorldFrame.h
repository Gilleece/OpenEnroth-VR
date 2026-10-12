#pragma once

#include "Engine/Graphics/LightsStack.h"

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

 private:
    void prepareOutdoor();
    void prepareIndoor();
    void drawLevel();
    void drawOutdoor();
    void drawIndoor();

 private:
    LightsSnapshot _staticGeometryLights;
    bool _prepared = false;
};
