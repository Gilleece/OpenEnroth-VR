#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp> // NOLINT: not a C system header.

#include <openxr/openxr.h> // NOLINT: not a C system header.

#include "Engine/Graphics/WorldFrameViews.h"
#include "Engine/MapEnums.h"

#include "Library/Geometry/Point.h"
#include "Library/Geometry/Size.h"
#include "Library/Geometry/Vec.h"
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
     * @param renderScale               Size of the eye swapchains as a multiple of the size the headset recommends.
     * @return                          Whether all of it was created. On failure the reason is logged, and the game
     *                                  should go on without VR.
     */
    bool initialize(float renderScale);

    virtual bool aimCullingCamera() override;
    virtual bool drawViews(WorldFrame &frame) override;
    virtual bool aimPickCamera() override;
    virtual bool pickRay(Pointi uiPosition, Vec3f *origin, Vec3f *direction) override;
    virtual bool forwardRay(Vec3f *origin, Vec3f *direction) override;
    virtual bool listenerPose(Vec3f *position, Vec3f *forward, Vec3f *up) override;
    virtual void recenter() override;

 private:
    friend class PlatformIntrospection; // Give access to private bases.

    struct CullingCamera {
        Vec3f position;
        int yaw = 0;
        int pitch = 0;
        float fovRad = 0;
        float nearClip = 0;
    };

    /**
     * Head pose that the lazy UI quad is placed in front of, in the head origin space.
     */
    struct UiAim {
        glm::vec3 head = glm::vec3(0); // Position in metres.
        float yaw = 0; // Radians around +y, of the direction the head looks along.
        float pitch = 0; // Radians, positive looks up.
    };

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
    bool createSwapchains(float renderScale);

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
     * Places the UI quad for this frame, fixed in front of the eyes or following the head lazily as `vr.ui_follow`
     * says, and keeps its pose in the head origin space for picking.
     *
     * @param[out] layer                Quad layer to set the space and pose of.
     * @param distance                  Distance of the quad from the head in metres.
     */
    void placeUi(XrCompositionLayerQuad *layer, float distance);

    /**
     * @param time                      Time to ease to.
     * @return                          Head pose the lazy UI quad is aimed at, partway from `_uiAimFrom` to `_uiAim`.
     */
    UiAim easedUiAim(std::chrono::steady_clock::time_point time) const;

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

    /**
     * Puts the last eye images drawn back into the eye swapchains, with the poses they were drawn at, so that the
     * world stays where it was around the head on frames that don't draw it.
     *
     * @return                          Whether both eyes were put back.
     */
    bool restoreHeldWorld();

    /**
     * @param index                     Eye index, 0 is the left eye.
     * @param view                      Eye view the image was drawn from.
     * @param eye                       Swapchain the image is in.
     */
    void fillProjectionView(int index, const XrView &view, const Swapchain &eye);

    /**
     * Fades the comfort vignette in while the party moves, turns or changes level, and out once it stops. Call once
     * per frame drawn.
     */
    void updateVignette();

    void shutdown();

    /**
     * @param view                      Eye view located in the reference space.
     * @param size                      Size of the eye swapchain images.
     * @return                          Camera view of the eye in the game world, with the party's facing added to
     *                                  the headset's.
     */
    CameraView eyeView(const XrView &view, Sizei size) const;

    /**
     * @param orientation               Orientation in the head origin space.
     * @return                          The same orientation in the game world, from the game's view axes, with the
     *                                  party's facing added.
     */
    glm::quat worldOrientation(const XrQuaternionf &orientation) const;

 private:
    XrInstance _instance = XR_NULL_HANDLE;
    XrSystemId _systemId = XR_NULL_SYSTEM_ID;
    XrSession _session = XR_NULL_HANDLE;
    XrSpace _localSpace = XR_NULL_HANDLE;
    XrSpace _viewSpace = XR_NULL_HANDLE;
    XrSpace _space = XR_NULL_HANDLE; // Head origin, null until it's captured.
    std::array<Swapchain, 2> _eyes;
    Swapchain _ui;
    std::optional<Vec2f> _uiPlacement; // Width and distance of the UI quad last logged.
    std::optional<UiAim> _uiAim; // Where the lazy UI quad goes, null until it's placed.
    UiAim _uiAimFrom; // Where the lazy UI quad was when it started easing to `_uiAim`.
    std::chrono::steady_clock::time_point _uiAimTime; // When it started.
    std::optional<XrPosef> _uiPose; // Of the UI quad in the last frame, in the head origin space.
    glm::vec3 _uiHead = glm::vec3(0); // Head position in the head origin space in that frame.
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
    std::array<XrView, 2> _heldViews = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}}; // Of the last eye images drawn, kept by the renderer.
    bool _heldValid = false; // Whether the renderer holds both eyes of one frame drawn from the current head origin.
    MapId _heldMap = MAP_INVALID;
    std::optional<CullingCamera> _lastCullingCamera; // Of the last frame drawn, for picking.
    Vec3f _lastPickOrigin; // Mid-eye position of the last frame drawn.
    glm::quat _lastPickOrientation = glm::quat(1, 0, 0, 0); // VIEW space orientation of the last frame drawn.
    int64_t _frameIndex = 0;
    int64_t _worldFrameCount = 0;
    std::chrono::steady_clock::time_point _summaryTime; // Of the last frame summary in the log.
    int64_t _summaryFrameIndex = 0;
    int64_t _summaryWorldFrameCount = 0;
    std::chrono::steady_clock::time_point _lastFrameTime;

    struct PartyPose {
        Vec3f position;
        int yaw = 0;
        int pitch = 0;
        MapId map = MAP_INVALID;
    };

    std::optional<PartyPose> _vignettePose; // Of the last frame drawn.
    std::chrono::steady_clock::time_point _vignetteTime; // Of the last frame drawn.
    std::chrono::steady_clock::time_point _vignetteMoveTime; // When the party last moved, turned or changed level.
    float _vignetteLevel = 0; // How far the vignette has faded in, from 0 to 1.
};
