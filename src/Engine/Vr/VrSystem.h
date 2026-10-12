#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <openxr/openxr.h> // NOLINT: not a C system header.

#include "Engine/Graphics/WorldFrameViews.h"

#include "Library/Geometry/Size.h"
#include "Library/Platform/Filters/PlatformEventFilter.h"
#include "Library/Platform/Proxy/ProxyOpenGLContext.h"

struct CameraView;

/**
 * Renders the 3D world to an OpenXR headset.
 *
 * Installed as an application component, it ends the headset frame before every buffer swap and waits for and begins
 * the next one after it, so that every path that presents a frame keeps the headset in step. In between, the engine
 * calls `drawViews` with the prepared world frame, and it's drawn once per eye.
 *
 * Eye poses are taken relative to a head origin, the head pose on the first tracked frame once the session is focused,
 * levelled to its yaw. The `vr.recenter` key captures it again.
 *
 * While the session runs the renderer draws the UI as a layer of its own, which goes to the headset as a quad locked to
 * the head, in front of the world.
 */
class VrSystem : private ProxyOpenGLContext, private PlatformEventFilter, public WorldFrameViews {
 public:
    VrSystem();
    virtual ~VrSystem();

    /**
     * Creates the OpenXR instance, session, reference space and eye swapchains on the current OpenGL context.
     *
     * @return                          Whether all of it was created. On failure the reason is logged, and the game
     *                                  should go on without VR.
     */
    bool initialize();

    virtual bool aimCullingCamera() override;
    virtual bool drawViews(WorldFrame &frame) override;

 private:
    friend class PlatformIntrospection; // Give access to private bases.

    struct Swapchain {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        Sizei size;
        std::vector<uint32_t> images; // OpenGL texture names.
    };

    virtual void swapBuffers() override;
    virtual bool keyPressEvent(const PlatformKeyEvent *event) override;

    /**
     * @param result                    Result of an OpenXR call.
     * @param call                      Name of the call, for the log.
     * @return                          Whether the call succeeded. A failure is logged.
     */
    bool check(XrResult result, const char *call) const;

    bool createInstance();
    bool checkGraphicsRequirements();
    bool createSession();
    bool createSwapchains();

    /**
     * @param[out] target               Swapchain to create.
     * @param size                      Size of the swapchain images.
     * @return                          Whether the swapchain was created.
     */
    bool createSwapchain(Swapchain *target, Sizei size);

    void destroySwapchain(Swapchain *target);
    void pollEvents();
    void handleSessionState(XrSessionState state);

    /**
     * Hands the frame pacing to the runtime while the session runs, and gives it back to the desktop when it stops.
     *
     * @param running                   Whether the session is running.
     */
    void setRunning(bool running);

    void beginFrame();
    void endFrame();

    /**
     * Locates the eye views for this frame's display time into `_views`, with the head offset clamped.
     *
     * @return                          Whether the views were located.
     */
    bool locateViews();

    /**
     * Copies the UI layer into the UI swapchain, recreating the swapchain if the UI changed size.
     *
     * @param[out] layer                Quad layer that shows the UI in front of the head.
     * @return                          Whether there was a UI layer to copy.
     */
    bool copyUi(XrCompositionLayerQuad *layer);

    /**
     * Locates the head in the runtime's LOCAL space and makes a reference space with its origin there, turned to the
     * head's yaw. Eye views are located in that space from then on.
     *
     * @param time                      Time to locate the head at.
     * @return                          Whether the head was tracked and the space was created.
     */
    bool captureOrigin(XrTime time);

    /**
     * Draws the prepared world frame into the next image of an eye swapchain, and fills that eye's projection view.
     *
     * @param index                     Eye index, 0 is the left eye.
     * @param view                      Eye view located for this frame.
     * @param frame                     Prepared world frame.
     * @return                          Whether the eye was drawn.
     */
    bool drawEye(int index, const XrView &view, WorldFrame &frame);

    void shutdown();

    /**
     * @param view                      Eye view located in the reference space.
     * @param size                      Size of the eye swapchain images.
     * @return                          Camera view of the eye in the game world, with the party's facing added to
     *                                  the headset's.
     */
    CameraView eyeView(const XrView &view, Sizei size) const;

 private:
    XrInstance _instance = XR_NULL_HANDLE;
    XrSystemId _systemId = XR_NULL_SYSTEM_ID;
    XrSession _session = XR_NULL_HANDLE;
    XrSpace _localSpace = XR_NULL_HANDLE;
    XrSpace _viewSpace = XR_NULL_HANDLE;
    XrSpace _space = XR_NULL_HANDLE; // Head origin, null until it's captured.
    std::array<Swapchain, 2> _eyes;
    Swapchain _ui;
    int64_t _swapchainFormat = 0;
    bool _running = false;
    bool _focused = false;
    bool _recenterRequested = false;
    int _desktopSwapInterval = 1;
    XrFrameState _frameState = {XR_TYPE_FRAME_STATE};
    bool _frameBegun = false;
    bool _layerReady = false;
    std::array<XrCompositionLayerProjectionView, 2> _projectionViews = {};
    std::array<XrView, 2> _views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    bool _viewsLocated = false; // This frame, by `locateViews`.
    int64_t _frameIndex = 0;
    int64_t _worldFrameCount = 0;
};
