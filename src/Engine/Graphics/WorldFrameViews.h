#pragma once

class WorldFrame;

/**
 * Draws a `WorldFrame` from views of its own instead of the party camera, like the two eyes of a headset.
 */
class WorldFrameViews {
 public:
    virtual ~WorldFrameViews() = default;

    /**
     * Called by `Engine::drawWorld` after `WorldFrame::prepare`, in place of the draw from the party camera.
     *
     * @param frame                     Frame to draw.
     * @return                          Whether the frame was drawn. When it wasn't, it's drawn from the party camera
     *                                  as usual.
     */
    virtual bool drawViews(WorldFrame &frame) = 0;
};
