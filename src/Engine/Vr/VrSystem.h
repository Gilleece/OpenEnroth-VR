#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp> // NOLINT: not a C system header.

#include <openxr/openxr.h> // NOLINT: not a C system header.

#include "Engine/Graphics/WorldFrameViews.h"

#include "Library/Geometry/Point.h"
#include "Library/Geometry/Rect.h"
#include "Library/Geometry/Size.h"
#include "Library/Geometry/Vec.h"
#include "Library/Platform/Filters/PlatformEventFilter.h"
#include "Library/Platform/Proxy/ProxyOpenGLContext.h"
#include "Library/Platform/Proxy/ProxyWindow.h"

struct CameraView;

/**
 * Renders the 3D world to an OpenXR headset.
 *
 * Installed as an application component, it ends the headset frame before every buffer swap and waits for and begins
 * the next one after it, so that every path that presents a frame keeps the headset in step. In between, the engine
 * calls `drawViews` with the prepared world frame, and it's drawn once per eye.
 *
 * Eye poses are taken relative to a head origin, the head pose on the first tracked frame once the session is focused,
 * levelled to its yaw. The `vr.recenter` key captures it again, and so does a recenter in the runtime.
 *
 * While the session runs the renderer draws the UI as a layer of its own, which goes to the headset as a quad locked to
 * the head, in front of the world. With the spread HUD the game screen's viewport and its HUD go on quads of their own
 * that hang in the head origin space, so that they turn with the party and the eyes move over them.
 *
 * While the session runs the game window also grabs the mouse, so that a click can't land outside it. The window still
 * reports the grab setting it had without VR, and goes back to it when the session stops.
 */
class VrSystem : private ProxyOpenGLContext, private ProxyWindow, private PlatformEventFilter, public WorldFrameViews {
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

    /**
     * What a frame handed to the runtime carried.
     */
    struct FrameKind {
        bool world = false; // The eyes' projection layer, drawn this frame or the last one drawn while it fades out.
        bool fade = false; // A black quad over the world fading out, after the world stopped being drawn.
        bool spreadHud = false; // The spread HUD instead of the single UI quad.
        uint32_t layers = 0;

        auto operator<=>(const FrameKind &) const = default;
    };

    /**
     * Frames handed to the runtime since the last `resetFrameStats`, for tests.
     */
    struct FrameStats {
        std::map<FrameKind, int64_t> kinds; // Frames the runtime wanted drawn, by what they carried.
        int64_t frames = 0; // All frames ended, drawn or not.
        std::chrono::steady_clock::time_point firstFrameTime;
        std::chrono::steady_clock::time_point lastFrameTime;
    };

    [[nodiscard]] const FrameStats &frameStats() const {
        return _frameStats;
    }

    void resetFrameStats() {
        _frameStats = FrameStats();
    }

    /**
     * @return                          Whether the session is focused, so that the headset shows the game and its
     *                                  input goes to it.
     */
    [[nodiscard]] bool focused() const {
        return _focused;
    }

    /**
     * Turns the head by a fixed angle on top of its tracked pose, for tests that can't move a headset.
     *
     * @param yawDegrees                Turn to the left.
     * @param pitchDegrees              Tilt up.
     */
    void setHeadTurn(float yawDegrees, float pitchDegrees);

    /**
     * Makes the head untracked in position the way a headset that only tracks orientation is, for tests. The head
     * origin is captured again when this changes.
     *
     * @param tracked                   Whether to use the head position the runtime tracks.
     */
    void setPositionTracking(bool tracked);

    /**
     * @param uiPosition                Position in the game's 640x480 layout.
     * @return                          Whether the spread HUD shows it, on the viewport or a card. The cursor is kept
     *                                  on those while the HUD is spread.
     */
    [[nodiscard]] bool hudShows(Pointi uiPosition) const;

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

    /**
     * Quad of the spread HUD, as picking sees it.
     */
    struct HudQuad {
        Recti rect; // Part of the game screen it shows.
        XrPosef pose = {{0, 0, 0, 1}, {0, 0, 0}}; // Of the middle of `rect`.
        float metresPerPixel = 0;
    };

    virtual void swapBuffers() override;
    virtual void setGrabsMouse(bool grabsMouse) override;
    virtual bool grabsMouse() const override;
    virtual bool keyPressEvent(const PlatformKeyEvent *event) override;
    virtual bool mouseMoveEvent(const PlatformMouseEvent *event) override;

    /**
     * @param from                      Cursor position on the spread HUD before the mouse moved, if there was one.
     * @param to                        Where the mouse moved it.
     * @return                          `to` if the spread HUD shows it. Otherwise the first point past it in the
     *                                  direction the mouse moved that the HUD shows, so that the cursor crosses the
     *                                  gaps between the cards. With none before the edge of the screen, the point
     *                                  nearest to `to` on the part the cursor was on, or on any part.
     */
    Pointi hudCursor(std::optional<Pointi> from, Pointi to) const;

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

    /**
     * Acquires the next image of a swapchain and waits until it can be drawn into. A failed wait leaves the image
     * acquired for good, so the swapchain is made again then.
     *
     * @param[in,out] target            Swapchain to take the image from.
     * @param[out] imageIndex           Index of the image in `target->images`.
     * @return                          Whether the image is ready, to be released once drawn into.
     */
    bool acquireImage(Swapchain *target, uint32_t *imageIndex);

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
     * Puts the eyes in `_views` at their offsets from a head that sits at the head origin, for frames without a head
     * position. If the runtime doesn't give the offsets either, both eyes go to the origin.
     */
    void placeUntrackedEyes();

    /**
     * Copies the UI layer into the UI swapchain, recreating the swapchain if the UI changed size.
     *
     * @param[out] layers               Quad layers that show the UI, one in front of the head, or one per element of
     *                                  the spread HUD.
     * @return                          Whether there was a UI layer to copy.
     */
    bool copyUi(std::vector<XrCompositionLayerQuad> *layers);

    /**
     * Cuts the world viewport out of the UI quad and hangs it straight ahead, and adds a quad for each card of the
     * spread HUD around it.
     *
     * @param[in,out] layers            The UI quad, followed by the cards' quads.
     * @return                          Whether the cards were copied into their swapchain. If not, nothing was added.
     */
    bool spreadHud(std::vector<XrCompositionLayerQuad> *layers);

    /**
     * Copies the parts of the UI layer that the spread HUD's cards show into the HUD swapchain, recreating it if the
     * UI changed size, and frames each of them with a border.
     *
     * @param screen                    Size of the game's layout.
     * @param[out] rects                Rectangle of each card in the swapchain image, border included.
     * @return                          Whether the cards were copied.
     */
    bool copyHudCards(Sizei screen, std::vector<XrRect2Di> *rects);

    /**
     * Fills the fade swapchain with black at an opacity, for the quad that darkens the last world frame after the
     * world stops being drawn.
     *
     * @param level                     Opacity from 0 to 1.
     * @param[out] layer                Quad in front of the eyes, past the edges of their view.
     * @return                          Whether the swapchain was filled.
     */
    bool fadeWorld(float level, XrCompositionLayerQuad *layer);

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
     * head's yaw. Without a head position the origin is LOCAL's own. Eye views are located in that space from then on.
     *
     * @param time                      Time to locate the head at.
     * @return                          Whether the head's orientation was tracked and the space was created.
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
     * @param index                     Eye index, 0 is the left eye.
     * @param view                      Eye view the image was drawn from.
     * @param eye                       Swapchain the image is in.
     */
    void fillProjectionView(int index, const XrView &view, const Swapchain &eye);

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
    Swapchain _hud; // Cards of the spread HUD, one above the other.
    Swapchain _fade; // Black at the opacity of the world's fade.
    std::optional<Vec2f> _uiPlacement; // Width and distance of the UI quad last logged.
    std::optional<UiAim> _uiAim; // Where the lazy UI quad goes, null until it's placed.
    UiAim _uiAimFrom; // Where the lazy UI quad was when it started easing to `_uiAim`.
    std::chrono::steady_clock::time_point _uiAimTime; // When it started.
    std::optional<XrPosef> _uiPose; // Of the UI quad in the last frame, in the head origin space.
    glm::vec3 _uiHead = glm::vec3(0); // Head position in the head origin space in that frame.
    uint32_t _maxLayerCount = 0; // Composition layers the runtime takes in a frame.
    bool _hudSpread = false; // Whether the last frame showed the spread HUD.
    bool _hudWanted = false; // Whether the settings and the screen asked for it in that frame.
    std::vector<HudQuad> _hudCards; // Of the spread HUD in that frame.
    HudQuad _hudScreen; // Floating screen that the spread HUD's viewport was cut from in that frame.
    bool _hudInRoom = false; // Whether those are in the head origin space, rather than VIEW space.
    glm::vec3 _hudHead = glm::vec3(0); // Head position in the head origin space in that frame.
    std::optional<Pointi> _hudCursor; // Last cursor position while the HUD is spread.
    std::optional<std::array<float, 6>> _hudPlacement; // Settings the spread HUD was last logged with.
    int64_t _swapchainFormat = 0;
    bool _running = false;
    bool _grabsMouse = false; // Window's grab setting without VR, while the session runs.
    bool _focused = false;
    bool _recenterRequested = false;
    XrTime _localChangeTime = 0; // From when the runtime's last move of its LOCAL space shows in the poses it gives.
    int _desktopSwapInterval = 1;
    XrFrameState _frameState = {XR_TYPE_FRAME_STATE};
    bool _frameBegun = false;
    bool _layerReady = false;
    std::array<XrCompositionLayerProjectionView, 2> _projectionViews = {};
    std::array<XrView, 2> _views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    bool _viewsLocated = false; // This frame, by `locateViews`.
    bool _worldReleased = false; // Whether the eye swapchains last released the world drawn at `_projectionViews` from the current head origin.
    XrTime _worldTime = 0; // Display time of the last frame that drew the world.
    std::optional<CullingCamera> _lastCullingCamera; // Of the last frame drawn, for picking.
    Vec3f _lastPickOrigin; // Mid-eye position of the last frame drawn.
    glm::quat _lastPickOrientation = glm::quat(1, 0, 0, 0); // VIEW space orientation of the last frame drawn.
    int64_t _frameIndex = 0;
    FrameStats _frameStats;
    glm::quat _headTurn = glm::quat(1, 0, 0, 0); // Set by `setHeadTurn`, in the game's view axes.
    bool _positionTracking = true; // Set by `setPositionTracking`.
    int64_t _worldFrameCount = 0;
    std::chrono::steady_clock::time_point _summaryTime; // Of the last frame summary in the log.
    int64_t _summaryFrameIndex = 0;
    int64_t _summaryWorldFrameCount = 0;
    std::chrono::steady_clock::time_point _lastFrameTime;
};
