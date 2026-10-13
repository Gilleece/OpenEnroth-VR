#pragma once

#include "Library/Geometry/Point.h"
#include "Library/Geometry/Vec.h"

class WorldFrame;

/**
 * Draws a `WorldFrame` from views of its own instead of the party camera, like the two eyes of a headset.
 */
class WorldFrameViews {
 public:
    virtual ~WorldFrameViews() = default;

    /**
     * Called by `Engine::drawWorld` before `WorldFrame::prepare`, to point `pCamera3D`, which prepare culls with, at
     * the views about to be drawn.
     *
     * @return                          Whether the camera was moved. If so, `drawWorld` points it back at the party once
     *                                  the frame is drawn.
     */
    virtual bool aimCullingCamera() = 0;

    /**
     * Called by `Engine::drawWorld` after `WorldFrame::prepare`, in place of the draw from the party camera.
     *
     * @param frame                     Frame to draw.
     * @return                          Whether the frame was drawn. When it wasn't, it's drawn from the party camera
     *                                  as usual.
     */
    virtual bool drawViews(WorldFrame &frame) = 0;

    /**
     * Points `pCamera3D` at the culling camera of the last frame drawn, which built the billboard and face lists that
     * picking reads.
     *
     * @return                          Whether the camera was moved. If so, the caller points it back at the party with
     *                                  `Camera3D::updateFromParty` once it's done picking.
     */
    virtual bool aimPickCamera() = 0;

    /**
     * @param uiPosition                Mouse position in the UI.
     * @param[out] origin               Start of the pick ray in world space.
     * @param[out] direction            Direction of the pick ray in world space, normalized.
     * @return                          Whether the views pick with a ray of their own, from the last frame drawn. If
     *                                  not, picking goes through the party camera as usual.
     */
    virtual bool pickRay(Pointi uiPosition, Vec3f *origin, Vec3f *direction) = 0;

    /**
     * @param[out] origin               Start of the ray in world space.
     * @param[out] direction            Direction the head looks along in world space, normalized.
     * @return                          Whether keyboard picking looks around this ray, from the last frame drawn,
     *                                  instead of through the party camera.
     */
    virtual bool forwardRay(Vec3f *origin, Vec3f *direction) = 0;

    /**
     * Makes the views' current pose their new starting pose, from the next frame on.
     */
    virtual void recenter() = 0;
};
