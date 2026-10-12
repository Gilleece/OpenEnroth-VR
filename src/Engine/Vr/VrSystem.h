#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <openxr/openxr.h> // NOLINT: not a C system header.

#include "Engine/Graphics/WorldFrameViews.h"

#include "Library/Geometry/Size.h"
#include "Library/Platform/Proxy/ProxyOpenGLContext.h"

struct CameraView;

/**
 * Renders the 3D world to an OpenXR headset.
 *
 * Installed as an application component, it ends the headset frame before every buffer swap and waits for and begins
 * the next one after it, so that every path that presents a frame keeps the headset in step. In between, the engine
 * calls `drawViews` with the prepared world frame, and it's drawn once per eye.
 */
class VrSystem : private ProxyOpenGLContext, public WorldFrameViews {
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

    virtual bool drawViews(WorldFrame &frame) override;

 private:
    friend class PlatformIntrospection; // Give access to private bases.

    struct Eye {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        Sizei size;
        std::vector<uint32_t> images; // OpenGL texture names.
    };

    virtual void swapBuffers() override;

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
    XrSpace _space = XR_NULL_HANDLE;
    std::array<Eye, 2> _eyes;
    bool _running = false;
    int _desktopSwapInterval = 1;
    XrFrameState _frameState = {XR_TYPE_FRAME_STATE};
    bool _frameBegun = false;
    bool _layerReady = false;
    std::array<XrCompositionLayerProjectionView, 2> _projectionViews = {};
    int64_t _frameIndex = 0;
    int64_t _worldFrameCount = 0;
};
