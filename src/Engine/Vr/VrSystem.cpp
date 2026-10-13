#include "VrSystem.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glad/gl.h> // NOLINT: not a C system header.
#include <glm/glm.hpp> // NOLINT: not a C system header.
#include <glm/gtc/quaternion.hpp> // NOLINT: not a C system header.
#include <openxr/openxr_platform.h> // NOLINT: not a C system header.

#include "Engine/Engine.h"
#include "Engine/Party.h"
#include "Engine/Graphics/BspRenderer.h"
#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/CameraView.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/Viewport.h"
#include "Engine/Graphics/Vis.h"
#include "Engine/Graphics/WorldFrame.h"
#include "Engine/Graphics/Renderer/Renderer.h"

#include "GUI/GUIWindow.h"

#include "Application/GameConfig.h"

#include "Io/InputEnums.h"
#include "Io/KeyboardActionMapping.h"
#include "Io/Mouse.h"

#include "Library/Geometry/Rect.h"
#include "Library/Logger/Logger.h"
#include "Library/Platform/Interface/PlatformEvents.h"

#include "Utility/String/Format.h"

static constexpr XrViewConfigurationType VIEW_CONFIGURATION = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
static constexpr float UI_DEAD_ZONE_YAW = 15 * M_PI / 180; // The lazy UI quad re-aims when the head turns further.
static constexpr float UI_DEAD_ZONE_PITCH = 10 * M_PI / 180;
static constexpr float UI_DEAD_ZONE_DISTANCE = 0.3f; // Metres.
static constexpr std::chrono::duration<float> UI_EASE_DURATION(0.5f);
static constexpr float GAME_UNITS_PER_METRE = 100.0f; // The party's eye level of 160 units is a 1.6 m standing eye height.
static constexpr int64_t LOGGED_FRAME_COUNT = 5;
static constexpr std::chrono::seconds FRAME_SUMMARY_INTERVAL(5);
static constexpr int64_t CULLING_LOG_INTERVAL = 90;
static constexpr float VIGNETTE_FADE_IN_SECONDS = 0.1f;
static constexpr float VIGNETTE_FADE_OUT_SECONDS = 0.3f;
static constexpr float VIGNETTE_MIN_MOVE = 0.5f; // Game units per frame, below it the party counts as standing still.
static constexpr std::chrono::milliseconds VIGNETTE_MOVE_HOLD(50); // How long the party counts as moving after it last moved.
static constexpr float CULLING_MARGIN_RAD = 5 * M_PI / 180; // Slack for the culling yaw and pitch, rounded to the party's units.
static constexpr float MAX_CULLING_HALF_FOV_RAD = 85 * M_PI / 180;

/**
 * Part of the game screen that the spread HUD shows on a quad of its own, hanging in VIEW space around the head and
 * facing it.
 */
struct HudElement {
    const char *name;
    Recti rect; // In the game's 640x480 layout.
    float yawDegrees; // Of the quad's centre, positive to the right.
    float pitchDegrees; // Positive up.
};

static constexpr std::array<HudElement, 5> HUD_ELEMENTS = {{
    {"status bar", Recti(0, 352, 468, 24), 0, -24},
    {"portraits", Recti(0, 376, 468, 104), 0, -31},
    {"minimap", Recti(468, 0, 172, 140), 30, 20},
    {"right panel", Recti(468, 140, 172, 204), 34, 0},
    {"buttons", Recti(468, 344, 172, 136), 30, -21},
}};
static constexpr Recti HUD_VIEWPORT(8, 8, 460, 344); // Inside the frame around the world view.
static constexpr uint32_t HUD_LAYER_COUNT = static_cast<uint32_t>(2 + HUD_ELEMENTS.size()); // With the viewport and the world's projection layer.

using PfnWglSwapIntervalExt = BOOL (WINAPI *)(int interval);
using PfnWglGetSwapIntervalExt = int (WINAPI *)();

static void setDesktopSwapInterval(int interval) {
    if (auto swapInterval = reinterpret_cast<PfnWglSwapIntervalExt>(wglGetProcAddress("wglSwapIntervalEXT")))
        swapInterval(interval);
}

static int desktopSwapInterval() {
    if (auto getSwapInterval = reinterpret_cast<PfnWglGetSwapIntervalExt>(wglGetProcAddress("wglGetSwapIntervalEXT")))
        return getSwapInterval();
    return 1;
}

static const char *sessionStateName(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_IDLE: return "idle";
    case XR_SESSION_STATE_READY: return "ready";
    case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
    case XR_SESSION_STATE_VISIBLE: return "visible";
    case XR_SESSION_STATE_FOCUSED: return "focused";
    case XR_SESSION_STATE_STOPPING: return "stopping";
    case XR_SESSION_STATE_LOSS_PENDING: return "loss pending";
    case XR_SESSION_STATE_EXITING: return "exiting";
    default: return "unknown";
    }
}

static std::string versionString(XrVersion version) {
    return fmt::format("{}.{}.{}", XR_VERSION_MAJOR(version), XR_VERSION_MINOR(version), XR_VERSION_PATCH(version));
}

/**
 * Moves both eyes by the same amount, so that the point between them stays within a radius of the head origin.
 *
 * @param views                     Eye views located in the head origin space.
 * @param radius                    Radius in metres.
 */
static void clampHeadOffset(std::array<XrView, 2> &views, float radius) {
    const XrVector3f &left = views[0].pose.position;
    const XrVector3f &right = views[1].pose.position;
    glm::vec3 head((left.x + right.x) / 2, (left.y + right.y) / 2, (left.z + right.z) / 2);
    float distance = glm::length(head);
    if (distance <= radius)
        return;

    glm::vec3 shift = head * (radius / distance - 1);
    for (XrView &view : views) {
        view.pose.position.x += shift.x;
        view.pose.position.y += shift.y;
        view.pose.position.z += shift.z;
    }
}

/**
 * @param orientation                   Orientation in an OpenXR space.
 * @return                              Its yaw in radians, the turn of the direction it looks along, -z, around +y.
 */
static float yawOf(const XrQuaternionf &orientation) {
    glm::vec3 forward = glm::quat(orientation.w, orientation.x, orientation.y, orientation.z) * glm::vec3(0, 0, -1);
    return std::atan2(-forward.x, -forward.z);
}

/**
 * @param views                         Eye views.
 * @return                              View from between the eyes, turned halfway between them.
 */
static XrView midView(const std::array<XrView, 2> &views) {
    const XrPosef &left = views[0].pose;
    const XrPosef &right = views[1].pose;
    glm::quat orientation = glm::slerp(glm::quat(left.orientation.w, left.orientation.x, left.orientation.y, left.orientation.z),
                                       glm::quat(right.orientation.w, right.orientation.x, right.orientation.y, right.orientation.z), 0.5f);

    XrView result = views[0];
    result.pose.position = {(left.position.x + right.position.x) / 2, (left.position.y + right.position.y) / 2,
                            (left.position.z + right.position.z) / 2};
    result.pose.orientation = {orientation.x, orientation.y, orientation.z, orientation.w};
    return result;
}

static glm::quat toGlm(const XrQuaternionf &orientation) {
    return glm::quat(orientation.w, orientation.x, orientation.y, orientation.z);
}

/**
 * @param views                         Eye views.
 * @param head                          Mid-eye view the culling camera looks along.
 * @param roll                          Roll of the head in radians, which the culling camera doesn't have.
 * @param nearClip                      Near clip distance in game units.
 * @param aspect                        Width over height of the culling camera's frustum.
 * @return                              Horizontal field of view in radians for a culling camera that looks along
 *                                      `head` without its roll and still covers both eye frustums.
 */
static float cullingFovRad(const std::array<XrView, 2> &views, const XrView &head, float roll, float nearClip, float aspect) {
    glm::quat toHead = glm::inverse(toGlm(head.pose.orientation));
    float tanX = 0;
    float tanY = 0;
    float eyeOffset = 0;
    for (const XrView &view : views) {
        glm::quat eyeInHead = toHead * toGlm(view.pose.orientation);
        for (float tanH : {std::tan(view.fov.angleLeft), std::tan(view.fov.angleRight)}) {
            for (float tanV : {std::tan(view.fov.angleDown), std::tan(view.fov.angleUp)}) {
                glm::vec3 corner = eyeInHead * glm::vec3(tanH, tanV, -1);
                float depth = std::max(-corner.z, 0.01f);
                tanX = std::max(tanX, std::abs(corner.x) / depth);
                tanY = std::max(tanY, std::abs(corner.y) / depth);
            }
        }
        const XrVector3f &eye = view.pose.position;
        const XrVector3f &centre = head.pose.position;
        eyeOffset = std::max(eyeOffset, glm::length(glm::vec3(eye.x - centre.x, eye.y - centre.y, eye.z - centre.z)));
    }

    // The eyes sit to the sides of the head, so up close they see past its frustum, by the most at the near clip.
    float widening = std::atan(eyeOffset * GAME_UNITS_PER_METRE / nearClip) + CULLING_MARGIN_RAD;
    tanX = std::tan(std::min(std::atan(tanX) + widening, MAX_CULLING_HALF_FOV_RAD));
    tanY = std::tan(std::min(std::atan(tanY) + widening, MAX_CULLING_HALF_FOV_RAD));

    // The bounds of the eye frustums turned by the head's roll.
    float rolledX = tanX * std::abs(std::cos(roll)) + tanY * std::abs(std::sin(roll));
    float rolledY = tanX * std::abs(std::sin(roll)) + tanY * std::abs(std::cos(roll));
    return 2 * std::min(std::atan(std::max(rolledX, rolledY * aspect)), MAX_CULLING_HALF_FOV_RAD);
}

/**
 * @return                              Number of outdoor models the culling camera sees, or 0 indoors.
 */
static int visibleModelCount() {
    if (uCurrentlyLoadedLevelType != LEVEL_OUTDOOR)
        return 0;
    int result = 0;
    for (BSPModel &model : pOutdoor->pBModels) {
        bool reachable = false;
        result += IsBModelVisible(&model, 256, &reachable);
    }
    return result;
}

VrSystem::VrSystem() : PlatformEventFilter({EVENT_KEY_PRESS}) {}

VrSystem::~VrSystem() {
    shutdown();
}

bool VrSystem::initialize(float renderScale) {
    return createInstance() && checkGraphicsRequirements() && createSession() && createSwapchains(renderScale);
}

bool VrSystem::aimCullingCamera() {
    if (!locateViews())
        return false;

    CameraView head = eyeView(midView(_views), _eyes[0].size);
    glm::vec3 forward = head.worldToView[0];
    glm::vec3 left = head.worldToView[1];
    glm::vec3 up = head.worldToView[2];
    float yaw = std::atan2(forward.y, forward.x);
    float pitch = std::asin(std::clamp(forward.z, -1.0f, 1.0f));
    float roll = std::atan2(left.z, up.z);

    float nearClip = engine->config->vr.NearClip.value();
    float fov = cullingFovRad(_views, midView(_views), roll, nearClip, static_cast<float>(pViewport.w) / pViewport.h);
    int cameraYaw = static_cast<int>(std::lround(yaw * 1024 / M_PI)) & 2047;
    int cameraPitch = static_cast<int>(std::lround(pitch * 1024 / M_PI));
    pCamera3D->updateFromPose(head.position, cameraYaw, cameraPitch, fov, nearClip);
    _lastCullingCamera = CullingCamera{head.position, cameraYaw, cameraPitch, fov, nearClip};

    // The UI quad hangs in VIEW space, which the eyes can be turned from, like the PICO's that look 4 degrees down.
    _lastPickOrigin = head.position;
    _lastPickOrientation = worldOrientation(midView(_views).pose.orientation);
    XrSpaceLocation viewLocation = {XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(_viewSpace, _space, _frameState.predictedDisplayTime, &viewLocation)) &&
        (viewLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        _lastPickOrientation = worldOrientation(viewLocation.pose.orientation);

    if (_worldFrameCount < LOGGED_FRAME_COUNT || _worldFrameCount % CULLING_LOG_INTERVAL == 0) {
        MM_INFO("VR: frame {} culls from yaw {:.1f}, pitch {:.1f} and roll {:.1f} degrees with a {:.1f} degree field of view, the party faces yaw {:.1f}.",
                _frameIndex, cameraYaw * 360.0 / 2048, cameraPitch * 360.0 / 2048, roll * 180 / M_PI, fov * 180 / M_PI,
                (pParty->_viewYaw & 2047) * 360.0 / 2048);
    }
    return true;
}

bool VrSystem::aimPickCamera() {
    if (!_running || !_lastCullingCamera)
        return false;

    const CullingCamera &camera = *_lastCullingCamera;
    pCamera3D->updateFromPose(camera.position, camera.yaw, camera.pitch, camera.fovRad, camera.nearClip);
    return true;
}

bool VrSystem::pickRay(Pointi uiPosition, Vec3f *origin, Vec3f *direction) {
    if (!_running || !_lastCullingCamera)
        return false;

    if (_hudSpread) {
        for (size_t i = 0; i < HUD_ELEMENTS.size(); i++) {
            const Recti &rect = HUD_ELEMENTS[i].rect;
            if (!rect.contains(uiPosition))
                continue;

            // The point under the cursor on the element's quad in VIEW space, then in the game's view axes, forward, left and up.
            const XrPosef &pose = _hudPoses[i];
            glm::quat facing(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
            glm::vec3 local((uiPosition.x + 0.5f - rect.x - rect.w / 2.0f) * _hudMetresPerPixel,
                            (rect.y + rect.h / 2.0f - uiPosition.y - 0.5f) * _hudMetresPerPixel, 0);
            glm::vec3 point = glm::vec3(pose.position.x, pose.position.y, pose.position.z) + facing * local;
            glm::vec3 world = _lastPickOrientation * glm::normalize(glm::vec3(-point.z, -point.x, point.y));
            *origin = _lastPickOrigin;
            *direction = Vec3f(world.x, world.y, world.z);
            return true;
        }
    }

    Sizei size = render->GetRenderDimensions();
    float width = engine->config->vr.UiWidth.value();
    float height = width * size.h / size.w;
    float right = ((uiPosition.x + 0.5f) / size.w - 0.5f) * width;
    float up = (0.5f - (uiPosition.y + 0.5f) / size.h) * height;

    glm::vec3 world;
    if (_uiPose) {
        // The point under the cursor on the UI quad, relative to the head, in the head origin space.
        const XrPosef &pose = *_uiPose;
        glm::quat facing(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
        glm::vec3 point = glm::vec3(pose.position.x, pose.position.y, pose.position.z) + facing * glm::vec3(right, up, 0) - _uiHead;
        glm::quat partyYaw = glm::angleAxis(static_cast<float>(2 * M_PI * pParty->_viewYaw / 2048), glm::vec3(0, 0, 1));
        world = partyYaw * glm::normalize(glm::vec3(-point.z, -point.x, point.y));
    } else {
        // Without a located head the quad hangs in VIEW space, the same point in the game's view axes, forward, left and up.
        glm::vec3 point(engine->config->vr.UiDistance.value(), -right, up);
        world = _lastPickOrientation * glm::normalize(point);
    }

    *origin = _lastPickOrigin;
    *direction = Vec3f(world.x, world.y, world.z);
    return true;
}

bool VrSystem::listenerPose(Vec3f *position, Vec3f *forward, Vec3f *up) {
    if (!_running || (!_viewsLocated && !locateViews()))
        return false;

    // The ears turn with the head, which the eyes can be turned from, like the PICO's that look 4 degrees down.
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(_viewSpace, _space, _frameState.predictedDisplayTime, &head)) ||
        !(head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return false;
    glm::quat orientation = worldOrientation(head.pose.orientation);
    glm::vec3 headForward = orientation * glm::vec3(1, 0, 0);
    glm::vec3 headUp = orientation * glm::vec3(0, 0, 1);

    *position = eyeView(midView(_views), _eyes[0].size).position;
    *forward = Vec3f(headForward.x, headForward.y, headForward.z);
    *up = Vec3f(headUp.x, headUp.y, headUp.z);
    return true;
}

bool VrSystem::forwardRay(Vec3f *origin, Vec3f *direction) {
    if (!_running || !_lastCullingCamera)
        return false;

    glm::vec3 forward = _lastPickOrientation * glm::vec3(1, 0, 0);
    *origin = _lastPickOrigin;
    *direction = Vec3f(forward.x, forward.y, forward.z);
    return true;
}

bool VrSystem::locateViews() {
    _viewsLocated = false;
    if (!_frameBegun || !_frameState.shouldRender || _space == XR_NULL_HANDLE)
        return false;

    XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = VIEW_CONFIGURATION;
    locateInfo.displayTime = _frameState.predictedDisplayTime;
    locateInfo.space = _space;
    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    _views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    uint32_t viewCount = 0;
    if (!check(xrLocateViews(_session, &locateInfo, &viewState, static_cast<uint32_t>(_views.size()), &viewCount, _views.data()), "xrLocateViews"))
        return false;
    if (!(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return false;
    if (!_positionTracking || !(viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
        placeUntrackedEyes();
    clampHeadOffset(_views, engine->config->vr.HeadOffsetRadius.value());
    _viewsLocated = true;
    return true;
}

void VrSystem::placeUntrackedEyes() {
    XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = VIEW_CONFIGURATION;
    locateInfo.displayTime = _frameState.predictedDisplayTime;
    locateInfo.space = _viewSpace;
    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> offsets = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    uint32_t viewCount = 0;
    XrViewStateFlags valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    bool known = XR_SUCCEEDED(xrLocateViews(_session, &locateInfo, &viewState, static_cast<uint32_t>(offsets.size()), &viewCount, offsets.data())) &&
                 (viewState.viewStateFlags & valid) == valid;

    for (size_t i = 0; i < _views.size(); i++) {
        glm::vec3 position(0);
        if (known) {
            glm::quat head = toGlm(_views[i].pose.orientation) * glm::inverse(toGlm(offsets[i].pose.orientation));
            const XrVector3f &offset = offsets[i].pose.position;
            position = head * glm::vec3(offset.x, offset.y, offset.z);
        }
        _views[i].pose.position = {position.x, position.y, position.z};
    }
}

bool VrSystem::drawViews(WorldFrame &frame) {
    if (!_viewsLocated || !frame.prepared())
        return false;
    const std::array<XrView, 2> &views = _views;

    updateVignette();

    // Both eyes draw the same billboard quads, so that only the eye projections differ.
    XrView head = midView(views);
    render->buildWorldBillboardQuads(eyeView(head, _eyes[0].size));

    std::array<int, 2> billboards = {};
    if (!drawEye(0, views[0], frame))
        return false;
    billboards[0] = render->worldBillboardsDrawn;
    _layerReady = drawEye(1, views[1], frame);
    _heldValid = _layerReady;
    _heldMap = engine->_currentLoadedMapId;
    billboards[1] = render->worldBillboardsDrawn;
    render->setExternalWorldTarget(TextureRenderId(), Sizei());
    render->turnWorldSpritesTo(eyeView(head, _eyes[0].size).position); // Back to the culling camera that picking reads.

    if (_worldFrameCount < LOGGED_FRAME_COUNT) {
        MM_INFO("VR: frame {} billboards face {:.2f} degrees of yaw, the eyes look along {:.2f} and {:.2f}.", _frameIndex,
                yawOf(head.pose.orientation) * 180 / M_PI, yawOf(views[0].pose.orientation) * 180 / M_PI,
                yawOf(views[1].pose.orientation) * 180 / M_PI);
        for (int i = 0; i < 2; i++) {
            const XrPosef &pose = views[i].pose;
            const XrFovf &fov = views[i].fov;
            MM_INFO("VR: frame {} eye {} at ({:.3f}, {:.3f}, {:.3f}) m, fov left {:.3f} right {:.3f} up {:.3f} down {:.3f} rad, {} world billboards.",
                    _frameIndex, i, pose.position.x, pose.position.y, pose.position.z, fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown,
                    billboards[i]);
        }
    }
    if (_worldFrameCount < LOGGED_FRAME_COUNT || _worldFrameCount % CULLING_LOG_INTERVAL == 0) {
        MM_INFO("VR: frame {} drew {} world billboards per eye, with {} outdoor models and {} indoor faces in the culling view.", _frameIndex,
                billboards[0], visibleModelCount(), uCurrentlyLoadedLevelType == LEVEL_INDOOR ? pBspRenderer->num_faces : 0);
    }
    return true;
}

void VrSystem::swapBuffers() {
    if (mouse)
        mouse->drawLayerCursor();
    endFrame();
    ProxyOpenGLContext::swapBuffers();
    pollEvents();
    beginFrame();
}

bool VrSystem::keyPressEvent(const PlatformKeyEvent *event) {
    if (!event->isAutoRepeat && keyboardActionMapping && keyboardActionMapping->isBound(INPUT_ACTION_VR_RECENTER, event->key))
        recenter();
    return false;
}

void VrSystem::recenter() {
    _recenterRequested = true;
}

bool VrSystem::check(XrResult result, const char *call) const {
    if (XR_SUCCEEDED(result))
        return true;

    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (_instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(_instance, result, name)))
        std::snprintf(name, sizeof(name), "%d", static_cast<int>(result));
    MM_ERROR("VR: {} failed with {}.", call, name);
    return false;
}

bool VrSystem::createInstance() {
    uint32_t extensionCount = 0;
    if (!check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr), "xrEnumerateInstanceExtensionProperties"))
        return false;
    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!check(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()), "xrEnumerateInstanceExtensionProperties"))
        return false;
    if (!std::ranges::any_of(extensions, [](const XrExtensionProperties &extension) {
        return std::strcmp(extension.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0;
    })) {
        MM_ERROR("VR: the OpenXR runtime doesn't support {}.", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
        return false;
    }

    const char *enabledExtensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(createInfo.applicationInfo.applicationName, "OpenEnroth", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(createInfo.applicationInfo.engineName, "OpenEnroth", XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;
    if (!check(xrCreateInstance(&createInfo, &_instance), "xrCreateInstance"))
        return false;

    XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
    if (check(xrGetInstanceProperties(_instance, &instanceProperties), "xrGetInstanceProperties"))
        MM_INFO("VR: OpenXR runtime '{}' version {}.", instanceProperties.runtimeName, versionString(instanceProperties.runtimeVersion));

    XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!check(xrGetSystem(_instance, &systemInfo, &_systemId), "xrGetSystem"))
        return false;

    XrSystemProperties systemProperties = {XR_TYPE_SYSTEM_PROPERTIES};
    if (check(xrGetSystemProperties(_instance, _systemId, &systemProperties), "xrGetSystemProperties")) {
        _maxLayerCount = systemProperties.graphicsProperties.maxLayerCount;
        MM_INFO("VR: headset '{}', vendor {:#x}, swapchains up to {}x{}, up to {} layers, orientation tracking {}, position tracking {}.",
                systemProperties.systemName, systemProperties.vendorId,
                systemProperties.graphicsProperties.maxSwapchainImageWidth, systemProperties.graphicsProperties.maxSwapchainImageHeight,
                _maxLayerCount,
                systemProperties.trackingProperties.orientationTracking ? "on" : "off",
                systemProperties.trackingProperties.positionTracking ? "on" : "off");
    }
    return true;
}

bool VrSystem::checkGraphicsRequirements() {
    PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;
    if (!check(xrGetInstanceProcAddr(_instance, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction *>(&getRequirements)),
               "xrGetInstanceProcAddr"))
        return false;

    XrGraphicsRequirementsOpenGLKHR requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!check(getRequirements(_instance, _systemId, &requirements), "xrGetOpenGLGraphicsRequirementsKHR"))
        return false;

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    XrVersion contextVersion = XR_MAKE_VERSION(major, minor, 0);
    MM_INFO("VR: OpenGL context {}.{}, the runtime wants {} up to {}.", major, minor,
            versionString(requirements.minApiVersionSupported), versionString(requirements.maxApiVersionSupported));

    if (contextVersion < requirements.minApiVersionSupported) {
        MM_ERROR("VR: the OpenGL context is older than the runtime supports.");
        return false;
    }
    if (contextVersion > requirements.maxApiVersionSupported)
        MM_WARNING("VR: the OpenGL context is newer than the runtime was tested with.");
    return true;
}

bool VrSystem::createSession() {
    // OpenEnroth renders on one context, the one current on this thread since the renderer was initialized.
    XrGraphicsBindingOpenGLWin32KHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();

    XrSessionCreateInfo sessionInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = _systemId;
    if (!check(xrCreateSession(_instance, &sessionInfo, &_session), "xrCreateSession"))
        return false;

    XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (!check(xrCreateReferenceSpace(_session, &spaceInfo, &_localSpace), "xrCreateReferenceSpace"))
        return false;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    return check(xrCreateReferenceSpace(_session, &spaceInfo, &_viewSpace), "xrCreateReferenceSpace");
}

bool VrSystem::createSwapchains(float renderScale) {
    uint32_t viewCount = 0;
    if (!check(xrEnumerateViewConfigurationViews(_instance, _systemId, VIEW_CONFIGURATION, 0, &viewCount, nullptr), "xrEnumerateViewConfigurationViews"))
        return false;
    if (viewCount != _eyes.size()) {
        MM_ERROR("VR: the headset has {} views instead of 2.", viewCount);
        return false;
    }
    std::array<XrViewConfigurationView, 2> views = {{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
    if (!check(xrEnumerateViewConfigurationViews(_instance, _systemId, VIEW_CONFIGURATION, viewCount, &viewCount, views.data()), "xrEnumerateViewConfigurationViews"))
        return false;

    uint32_t formatCount = 0;
    if (!check(xrEnumerateSwapchainFormats(_session, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats"))
        return false;
    std::vector<int64_t> formats(formatCount);
    if (!check(xrEnumerateSwapchainFormats(_session, formatCount, &formatCount, formats.data()), "xrEnumerateSwapchainFormats"))
        return false;

    // What is drawn is display-encoded with framebuffer sRGB off, and only an sRGB format tells the runtime so.
    auto format = std::ranges::find(formats, GL_SRGB8_ALPHA8);
    if (format == formats.end())
        format = std::ranges::find(formats, GL_RGBA8);
    if (format == formats.end()) {
        MM_ERROR("VR: the runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8 swapchains.");
        return false;
    }
    MM_INFO("VR: using {} swapchains.", *format == GL_SRGB8_ALPHA8 ? "GL_SRGB8_ALPHA8" : "GL_RGBA8");
    _swapchainFormat = *format;

    for (size_t i = 0; i < _eyes.size(); i++) {
        Swapchain &eye = _eyes[i];
        const XrViewConfigurationView &view = views[i];
        Sizei size(std::min(static_cast<int>(std::lround(view.recommendedImageRectWidth * renderScale)), static_cast<int>(view.maxImageRectWidth)),
                   std::min(static_cast<int>(std::lround(view.recommendedImageRectHeight * renderScale)), static_cast<int>(view.maxImageRectHeight)));
        if (!createSwapchain(&eye, size))
            return false;
        MM_INFO("VR: eye {} swapchain is {}x{} with {} images, {:.2f} times the recommended {}x{}.", i, eye.size.w, eye.size.h,
                eye.images.size(), renderScale, view.recommendedImageRectWidth, view.recommendedImageRectHeight);
    }
    return true;
}

bool VrSystem::createSwapchain(Swapchain *target, Sizei size) {
    target->size = size;

    XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = _swapchainFormat;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = size.w;
    swapchainInfo.height = size.h;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = 1;
    swapchainInfo.mipCount = 1;
    if (!check(xrCreateSwapchain(_session, &swapchainInfo, &target->swapchain), "xrCreateSwapchain"))
        return false;

    uint32_t imageCount = 0;
    if (!check(xrEnumerateSwapchainImages(target->swapchain, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages"))
        return false;
    std::vector<XrSwapchainImageOpenGLKHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    if (!check(xrEnumerateSwapchainImages(target->swapchain, imageCount, &imageCount,
                                          reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())), "xrEnumerateSwapchainImages"))
        return false;
    for (const XrSwapchainImageOpenGLKHR &image : images)
        target->images.push_back(image.image);
    return true;
}

void VrSystem::destroySwapchain(Swapchain *target) {
    if (render)
        render->forgetExternalTextures(std::vector<TextureRenderId>(target->images.begin(), target->images.end()));
    if (target->swapchain != XR_NULL_HANDLE)
        xrDestroySwapchain(target->swapchain);
    *target = Swapchain();
}

bool VrSystem::acquireImage(Swapchain *target, uint32_t *imageIndex) {
    if (target->swapchain == XR_NULL_HANDLE)
        return false;

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(xrAcquireSwapchainImage(target->swapchain, &acquireInfo, imageIndex), "xrAcquireSwapchainImage"))
        return false;
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (check(xrWaitSwapchainImage(target->swapchain, &waitInfo), "xrWaitSwapchainImage"))
        return true;

    Sizei size = target->size;
    destroySwapchain(target);
    if (createSwapchain(target, size)) {
        MM_INFO("VR: made the {}x{} swapchain again after the failed wait, with {} images.", size.w, size.h, target->images.size());
    } else {
        destroySwapchain(target);
    }
    return false;
}

void VrSystem::pollEvents() {
    if (_instance == XR_NULL_HANDLE)
        return;

    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(_instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            handleSessionState(reinterpret_cast<const XrEventDataSessionStateChanged &>(event).state);
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            MM_WARNING("VR: the OpenXR runtime is going away, continuing without VR.");
            setRunning(false);
        } else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto &change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending &>(event);
            if (change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                MM_INFO("VR: the runtime moves its LOCAL space, the head origin will be captured again.");
                _localChangeTime = change.changeTime;
                recenter();
            }
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void VrSystem::handleSessionState(XrSessionState state) {
    MM_INFO("VR: session state is {}.", sessionStateName(state));
    _focused = state == XR_SESSION_STATE_FOCUSED;

    if (state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = VIEW_CONFIGURATION;
        if (check(xrBeginSession(_session, &beginInfo), "xrBeginSession"))
            setRunning(true);
    } else if (state == XR_SESSION_STATE_STOPPING) {
        setRunning(false);
        check(xrEndSession(_session), "xrEndSession");
    } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
        setRunning(false);
        MM_INFO("VR: the session is over, continuing without VR.");
    }
}

void VrSystem::setRunning(bool running) {
    if (_running == running)
        return;

    _running = running;
    if (running) {
        _desktopSwapInterval = desktopSwapInterval();
        setDesktopSwapInterval(0);
    } else {
        setDesktopSwapInterval(_desktopSwapInterval);
    }
    if (render) {
        render->setExternalFramePacing(running);
        render->setUiLayer(running);
    }
    _lastCullingCamera.reset();
    _uiPlacement.reset();
}

void VrSystem::beginFrame() {
    if (!_running)
        return;

    _frameState = {XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo waitInfo = {XR_TYPE_FRAME_WAIT_INFO};
    if (!check(xrWaitFrame(_session, &waitInfo, &_frameState), "xrWaitFrame"))
        return;

    XrFrameBeginInfo beginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    if (!check(xrBeginFrame(_session, &beginInfo), "xrBeginFrame"))
        return;
    _frameBegun = true;

    if (_focused && (_space == XR_NULL_HANDLE || _recenterRequested) && _frameState.predictedDisplayTime >= _localChangeTime &&
        captureOrigin(_frameState.predictedDisplayTime))
        _recenterRequested = false;

    if (_frameIndex < LOGGED_FRAME_COUNT) {
        MM_INFO("VR: frame {} begun, display at {} ns, period {} ns, {}.", _frameIndex, _frameState.predictedDisplayTime,
                _frameState.predictedDisplayPeriod, _frameState.shouldRender ? "should render" : "should not render");
    }
}

void VrSystem::endFrame() {
    if (!_frameBegun)
        return;

    bool worldDrawn = _layerReady;
    if (!_layerReady && _frameState.shouldRender && window_SpeakInHouse && _heldValid && _heldMap == engine->_currentLoadedMapId)
        _layerReady = restoreHeldWorld(); // House screens stop the world being drawn.

    std::vector<const XrCompositionLayerBaseHeader *> layers;
    XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    if (_layerReady) {
        projection.space = _space;
        projection.viewCount = static_cast<uint32_t>(_projectionViews.size());
        projection.views = _projectionViews.data();
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection));
    }
    std::vector<XrCompositionLayerQuad> ui;
    if (_frameState.shouldRender && copyUi(&ui))
        for (const XrCompositionLayerQuad &quad : ui)
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = _frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();
    check(xrEndFrame(_session, &endInfo), "xrEndFrame");

    auto endTime = std::chrono::steady_clock::now();
    if (_frameStats.frames++ == 0)
        _frameStats.firstFrameTime = endTime;
    _frameStats.lastFrameTime = endTime;
    if (_frameState.shouldRender)
        _frameStats.kinds[FrameKind{_layerReady, _hudSpread, endInfo.layerCount}]++;

    if (worldDrawn)
        _worldFrameCount++;
    if (_frameIndex < LOGGED_FRAME_COUNT || (_layerReady && _worldFrameCount <= LOGGED_FRAME_COUNT))
        MM_INFO("VR: frame {} ended with {} layer(s).", _frameIndex, endInfo.layerCount);

    _frameIndex++;
    auto now = std::chrono::steady_clock::now();
    _lastFrameTime = now;
    if (_frameIndex == 1) {
        _summaryTime = now;
    } else if (now - _summaryTime >= FRAME_SUMMARY_INTERVAL) {
        double seconds = std::chrono::duration<double>(now - _summaryTime).count();
        MM_INFO("VR: {} frames submitted, {} in the last {:.1f} s at {:.1f} per second, {} of them with the world drawn.", _frameIndex,
                _frameIndex - _summaryFrameIndex, seconds, (_frameIndex - _summaryFrameIndex) / seconds, _worldFrameCount - _summaryWorldFrameCount);
        _summaryTime = now;
        _summaryFrameIndex = _frameIndex;
        _summaryWorldFrameCount = _worldFrameCount;
    }

    _frameBegun = false;
    _layerReady = false;
    _viewsLocated = false;
}

bool VrSystem::copyUi(std::vector<XrCompositionLayerQuad> *layers) {
    if (!render->uiLayer())
        return false;

    Sizei size = render->uiTargetSize();
    if (_ui.size != size) {
        destroySwapchain(&_ui);
        if (!createSwapchain(&_ui, size)) {
            destroySwapchain(&_ui);
            return false;
        }
        MM_INFO("VR: UI swapchain is {}x{} with {} images.", size.w, size.h, _ui.images.size());
    }

    uint32_t imageIndex = 0;
    if (!acquireImage(&_ui, &imageIndex))
        return false;
    render->copyUiLayer(TextureRenderId(_ui.images[imageIndex]), _ui.size);
    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(_ui.swapchain, &releaseInfo), "xrReleaseSwapchainImage");

    float width = engine->config->vr.UiWidth.value();
    float distance = engine->config->vr.UiDistance.value();
    if (_uiPlacement != Vec2f(width, distance)) {
        MM_INFO("VR: UI shown {:.2f} m wide at {:.2f} m.", width, distance);
        _uiPlacement = Vec2f(width, distance);
    }
    XrCompositionLayerQuad &layer = layers->emplace_back(XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD});
    layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // The UI target holds premultiplied colour.
    layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer.subImage.swapchain = _ui.swapchain;
    layer.subImage.imageRect.extent = {_ui.size.w, _ui.size.h};
    layer.size = {width, width * _ui.size.h / _ui.size.w};
    placeUi(&layer, distance);

    // The HUD only spreads around the world, which the main menu, also the game screen, doesn't have.
    bool wanted = engine->config->vr.HudLayout.value() == VR_HUD_LAYOUT_SPREAD && current_screen_type == SCREEN_GAME && _layerReady;
    bool spread = wanted && _maxLayerCount >= HUD_LAYER_COUNT;
    if (spread != _hudSpread || wanted != _hudWanted) {
        if (wanted && !spread) {
            MM_INFO("VR: frame {} shows the HUD on one quad, the runtime takes {} layers and the spread HUD needs {}.", _frameIndex, _maxLayerCount,
                    HUD_LAYER_COUNT);
        } else {
            MM_INFO("VR: frame {} shows the HUD {}.", _frameIndex, spread ? "spread around the view" : "on one quad");
        }
    }
    _hudSpread = spread;
    _hudWanted = wanted;
    if (spread)
        spreadHud(layers);
    return true;
}

/**
 * @param rect                          Rectangle in the game's layout.
 * @param screen                        Size of that layout.
 * @param image                         Size of the UI swapchain images.
 * @return                              The same rectangle in the swapchain images, which have their origin at the
 *                                      bottom left in OpenGL.
 */
static XrRect2Di imageRect(const Recti &rect, Sizei screen, Sizei image) {
    float scaleX = static_cast<float>(image.w) / screen.w;
    float scaleY = static_cast<float>(image.h) / screen.h;
    XrRect2Di result;
    result.offset = {static_cast<int32_t>(std::lround(rect.x * scaleX)), static_cast<int32_t>(std::lround((screen.h - rect.y - rect.h) * scaleY))};
    result.extent = {static_cast<int32_t>(std::lround(rect.w * scaleX)), static_cast<int32_t>(std::lround(rect.h * scaleY))};
    return result;
}

void VrSystem::spreadHud(std::vector<XrCompositionLayerQuad> *layers) {
    const GameConfig::Vr &config = engine->config->vr;
    Sizei screen = render->GetRenderDimensions();

    // The viewport stays where it is on the UI quad, so that it picks the same way.
    XrCompositionLayerQuad &viewport = layers->front();
    float quadMetresPerPixel = viewport.size.width / screen.w;
    glm::quat quadFacing(viewport.pose.orientation.w, viewport.pose.orientation.x, viewport.pose.orientation.y, viewport.pose.orientation.z);
    glm::vec3 offset((HUD_VIEWPORT.x + HUD_VIEWPORT.w / 2.0f - screen.w / 2.0f) * quadMetresPerPixel,
                     (screen.h / 2.0f - HUD_VIEWPORT.y - HUD_VIEWPORT.h / 2.0f) * quadMetresPerPixel, 0);
    glm::vec3 centre = glm::vec3(viewport.pose.position.x, viewport.pose.position.y, viewport.pose.position.z) + quadFacing * offset;
    viewport.pose.position = {centre.x, centre.y, centre.z};
    viewport.size = {HUD_VIEWPORT.w * quadMetresPerPixel, HUD_VIEWPORT.h * quadMetresPerPixel};
    viewport.subImage.imageRect = imageRect(HUD_VIEWPORT, screen, _ui.size);
    XrCompositionLayerQuad quad = viewport;

    float distance = config.HudDistance.value();
    float spread = config.HudSpread.value();
    _hudMetresPerPixel = config.UiWidth.value() / screen.w * distance / config.UiDistance.value() * config.HudScale.value();
    std::array<float, 5> placement = {distance, config.HudScale.value(), spread, config.UiWidth.value(), config.UiDistance.value()};
    bool log = _hudPlacement != placement;
    _hudPlacement = placement;

    _hudPoses.clear();
    for (const HudElement &element : HUD_ELEMENTS) {
        float yaw = element.yawDegrees * spread * static_cast<float>(M_PI) / 180;
        float pitch = element.pitchDegrees * spread * static_cast<float>(M_PI) / 180;
        glm::quat facing = glm::angleAxis(-yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
        glm::vec3 position = facing * glm::vec3(0, 0, -distance); // VIEW space looks along -z.
        const XrPosef &pose = _hudPoses.emplace_back(XrPosef{{facing.x, facing.y, facing.z, facing.w}, {position.x, position.y, position.z}});

        XrCompositionLayerQuad &layer = layers->emplace_back(quad);
        layer.space = _viewSpace;
        layer.pose = pose;
        layer.size = {element.rect.w * _hudMetresPerPixel, element.rect.h * _hudMetresPerPixel};
        layer.subImage.imageRect = imageRect(element.rect, screen, _ui.size);
        if (log) {
            MM_INFO("VR: HUD {} at ({:.3f}, {:.3f}, {:.3f}) m in VIEW space, {:.3f}x{:.3f} m, from image rectangle {}x{} at {}, {}.", element.name,
                    position.x, position.y, position.z, layer.size.width, layer.size.height, layer.subImage.imageRect.extent.width,
                    layer.subImage.imageRect.extent.height, layer.subImage.imageRect.offset.x, layer.subImage.imageRect.offset.y);
        }
    }
}

void VrSystem::placeUi(XrCompositionLayerQuad *layer, float distance) {
    layer->space = _viewSpace;
    layer->pose = {{0, 0, 0, 1}, {0, 0, -distance}}; // VIEW space looks along -z.
    _uiPose.reset();

    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (_space == XR_NULL_HANDLE || !_positionTracking || XR_FAILED(xrLocateSpace(_viewSpace, _space, _frameState.predictedDisplayTime, &head)) ||
        (head.locationFlags & tracked) != tracked) {
        _uiAim.reset();
        return;
    }
    const XrQuaternionf &orientation = head.pose.orientation;
    glm::quat headOrientation(orientation.w, orientation.x, orientation.y, orientation.z);
    _uiHead = glm::vec3(head.pose.position.x, head.pose.position.y, head.pose.position.z);

    glm::vec3 position;
    glm::quat facing;
    if (engine->config->vr.UiFollow.value() == VR_UI_FOLLOW_LOCKED) {
        _uiAim.reset();
        position = _uiHead + headOrientation * glm::vec3(0, 0, -distance);
        facing = headOrientation;
    } else {
        glm::vec3 forward = headOrientation * glm::vec3(0, 0, -1);
        UiAim now = {_uiHead, std::atan2(-forward.x, -forward.z), std::asin(std::clamp(forward.y, -1.0f, 1.0f))};
        auto time = std::chrono::steady_clock::now();
        if (!_uiAim) {
            _uiAim = now;
            _uiAimFrom = now;
            _uiAimTime = time - std::chrono::duration_cast<std::chrono::steady_clock::duration>(UI_EASE_DURATION);
        } else {
            float turn = std::abs(std::remainder(now.yaw - _uiAim->yaw, 2 * static_cast<float>(M_PI)));
            float tilt = std::abs(now.pitch - _uiAim->pitch);
            float move = glm::length(now.head - _uiAim->head);
            if (turn > UI_DEAD_ZONE_YAW || tilt > UI_DEAD_ZONE_PITCH || move > UI_DEAD_ZONE_DISTANCE) {
                _uiAimFrom = easedUiAim(time);
                MM_INFO("VR: UI re-aims after the head turned {:.1f} and tilted {:.1f} degrees and moved {:.2f} m, from yaw {:.1f} and pitch "
                        "{:.1f} to yaw {:.1f} and pitch {:.1f} degrees.", turn * 180 / M_PI, tilt * 180 / M_PI, move,
                        _uiAimFrom.yaw * 180 / M_PI, _uiAimFrom.pitch * 180 / M_PI, now.yaw * 180 / M_PI, now.pitch * 180 / M_PI);
                _uiAim = now;
                _uiAimTime = time;
            }
        }

        UiAim aim = easedUiAim(time);
        facing = glm::angleAxis(aim.yaw, glm::vec3(0, 1, 0)); // Upright, only turned to face the head.
        position = aim.head + facing * glm::angleAxis(aim.pitch, glm::vec3(1, 0, 0)) * glm::vec3(0, 0, -distance);
        layer->space = _space;
        layer->pose = {{facing.x, facing.y, facing.z, facing.w}, {position.x, position.y, position.z}};
    }
    _uiPose = XrPosef{{facing.x, facing.y, facing.z, facing.w}, {position.x, position.y, position.z}};
}

VrSystem::UiAim VrSystem::easedUiAim(std::chrono::steady_clock::time_point time) const {
    float t = std::clamp(std::chrono::duration<float>(time - _uiAimTime) / UI_EASE_DURATION, 0.0f, 1.0f);
    float eased = t * t * (3 - 2 * t);
    UiAim result;
    result.head = glm::mix(_uiAimFrom.head, _uiAim->head, eased);
    result.yaw = _uiAimFrom.yaw + std::remainder(_uiAim->yaw - _uiAimFrom.yaw, 2 * static_cast<float>(M_PI)) * eased;
    result.pitch = _uiAimFrom.pitch + (_uiAim->pitch - _uiAimFrom.pitch) * eased;
    return result;
}

bool VrSystem::captureOrigin(XrTime time) {
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    if (!check(xrLocateSpace(_viewSpace, _localSpace, time, &head), "xrLocateSpace"))
        return false;
    if (!(head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return false;
    bool positioned = _positionTracking && (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
    if (!positioned)
        head.pose.position = {0, 0, 0};

    float yaw = yawOf(head.pose.orientation);
    glm::quat level = glm::angleAxis(yaw, glm::vec3(0, 1, 0));

    XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation = {level.x, level.y, level.z, level.w};
    spaceInfo.poseInReferenceSpace.position = head.pose.position;
    XrSpace space = XR_NULL_HANDLE;
    if (!check(xrCreateReferenceSpace(_session, &spaceInfo, &space), "xrCreateReferenceSpace"))
        return false;

    if (_space != XR_NULL_HANDLE)
        xrDestroySpace(_space);
    _space = space;
    _heldValid = false;
    _uiAim.reset();
    _uiPose.reset();

    const XrVector3f &position = head.pose.position;
    if (positioned) {
        MM_INFO("VR: head origin at ({:.3f}, {:.3f}, {:.3f}) m in the runtime's LOCAL space, turned {:.1f} degrees.",
                position.x, position.y, position.z, yaw * 180 / M_PI);
    } else {
        MM_INFO("VR: head origin at the runtime's LOCAL origin, turned {:.1f} degrees, without position tracking.", yaw * 180 / M_PI);
    }
    return true;
}

void VrSystem::updateVignette() {
    auto now = std::chrono::steady_clock::now();
    float seconds = _vignettePose ? std::min(std::chrono::duration<float>(now - _vignetteTime).count(), 0.1f) : 0;
    _vignetteTime = now;

    PartyPose pose{pParty->pos, pParty->_viewYaw, pParty->_viewPitch, engine->_currentLoadedMapId};
    bool changedLevel = _vignettePose && pose.map != _vignettePose->map;
    if (_vignettePose && (changedLevel || (pose.position - _vignettePose->position).length() > VIGNETTE_MIN_MOVE ||
                          pose.yaw != _vignettePose->yaw || pose.pitch != _vignettePose->pitch))
        _vignetteMoveTime = now;
    bool moving = now - _vignetteMoveTime < VIGNETTE_MOVE_HOLD; // The game can tick slower than the headset draws.
    _vignettePose = pose;

    float level = _vignetteLevel;
    if (changedLevel) {
        level = 1; // The view cuts to the new level at once.
    } else if (moving) {
        level = std::min(1.0f, level + seconds / VIGNETTE_FADE_IN_SECONDS);
    } else {
        level = std::max(0.0f, level - seconds / VIGNETTE_FADE_OUT_SECONDS);
    }

    float strength = engine->config->vr.VignetteStrength.value();
    if (level > _vignetteLevel && _vignetteLevel == 0) {
        MM_INFO("VR: frame {} vignette fading in towards strength {:.2f}.", _frameIndex, strength);
    } else if (level == 1 && _vignetteLevel < 1) {
        MM_INFO("VR: frame {} vignette at strength {:.2f}.", _frameIndex, strength);
    } else if (level < _vignetteLevel && _vignetteLevel == 1) {
        MM_INFO("VR: frame {} vignette fading out from strength {:.2f}.", _frameIndex, strength);
    } else if (level == 0 && _vignetteLevel > 0) {
        MM_INFO("VR: frame {} vignette off.", _frameIndex);
    }
    _vignetteLevel = level;
}

bool VrSystem::drawEye(int index, const XrView &view, WorldFrame &frame) {
    Swapchain &eye = _eyes[index];

    uint32_t imageIndex = 0;
    if (!acquireImage(&eye, &imageIndex))
        return false;

    TextureRenderId texture(eye.images[imageIndex]);
    CameraView camera = eyeView(view, eye.size);
    render->setExternalWorldTarget(texture, eye.size);
    render->turnWorldSpritesTo(camera.position);
    render->BeginScene3D(camera);
    frame.draw();
    render->EndScene3D();
    if (index == 0)
        render->setViewportMirror(texture, eye.size);

    render->holdTexture(index, texture, eye.size);
    _heldViews[index] = view;

    float strength = engine->config->vr.Vignette.value() ? _vignetteLevel * engine->config->vr.VignetteStrength.value() : 0;
    const XrFovf &fov = view.fov;
    float left = std::tan(fov.angleLeft), right = std::tan(fov.angleRight), up = std::tan(fov.angleUp), down = std::tan(fov.angleDown);
    Vec2f center(-(right + left) / (right - left), -(up + down) / (up - down)); // Where the eye looks straight ahead.
    render->drawVignette(texture, eye.size, strength, center); // Drawn after the hold, so a held world isn't darkened.

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(eye.swapchain, &releaseInfo), "xrReleaseSwapchainImage");

    fillProjectionView(index, view, eye);
    return true;
}

bool VrSystem::restoreHeldWorld() {
    for (size_t i = 0; i < _eyes.size(); i++) {
        Swapchain &eye = _eyes[i];
        uint32_t imageIndex = 0;
        if (!acquireImage(&eye, &imageIndex))
            return false;
        render->restoreHeldTexture(static_cast<int>(i), TextureRenderId(eye.images[imageIndex]));
        XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        check(xrReleaseSwapchainImage(eye.swapchain, &releaseInfo), "xrReleaseSwapchainImage");

        fillProjectionView(static_cast<int>(i), _heldViews[i], eye);
    }
    return true;
}

void VrSystem::fillProjectionView(int index, const XrView &view, const Swapchain &eye) {
    XrCompositionLayerProjectionView &projectionView = _projectionViews[index];
    projectionView = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    projectionView.pose = view.pose;
    projectionView.fov = view.fov;
    projectionView.subImage.swapchain = eye.swapchain;
    projectionView.subImage.imageRect.extent = {eye.size.w, eye.size.h};
}

void VrSystem::shutdown() {
    if (_frameBegun) {
        _layerReady = false;
        endFrame();
    }
    setRunning(false);

    for (Swapchain &eye : _eyes)
        destroySwapchain(&eye);
    destroySwapchain(&_ui);
    for (XrSpace space : {_space, _viewSpace, _localSpace})
        if (space != XR_NULL_HANDLE)
            xrDestroySpace(space);
    if (_session != XR_NULL_HANDLE)
        xrDestroySession(_session);
    if (_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(_instance);
        double sinceLastFrame = std::chrono::duration<double>(std::chrono::steady_clock::now() - _lastFrameTime).count();
        MM_INFO("VR: shut down after {} frames, the last one {:.1f} s ago.", _frameIndex, _frameIndex ? sinceLastFrame : 0.0);
    }
    _space = XR_NULL_HANDLE;
    _viewSpace = XR_NULL_HANDLE;
    _localSpace = XR_NULL_HANDLE;
    _session = XR_NULL_HANDLE;
    _instance = XR_NULL_HANDLE;
}

void VrSystem::setHeadTurn(float yawDegrees, float pitchDegrees) {
    // The game's view axes have +y to the left and +z up, so tilting up turns about -y.
    _headTurn = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::radians(-pitchDegrees), glm::vec3(0, 1, 0));
}

void VrSystem::setPositionTracking(bool tracked) {
    if (_positionTracking == tracked)
        return;
    _positionTracking = tracked;
    if (_space != XR_NULL_HANDLE)
        xrDestroySpace(_space);
    _space = XR_NULL_HANDLE;
    _heldValid = false;
}

glm::quat VrSystem::worldOrientation(const XrQuaternionf &orientation) const {
    glm::quat partyYaw = glm::angleAxis(static_cast<float>(2 * M_PI * pParty->_viewYaw / 2048), glm::vec3(0, 0, 1));

    // OpenXR looks along -z with +x to the right and +y up, the game looks along +x with +y to the left and +z up.
    return partyYaw * _headTurn * glm::quat(orientation.w, -orientation.z, -orientation.x, orientation.y);
}

CameraView VrSystem::eyeView(const XrView &view, Sizei size) const {
    glm::quat partyYaw = glm::angleAxis(static_cast<float>(2 * M_PI * pParty->_viewYaw / 2048), glm::vec3(0, 0, 1));
    const XrVector3f &position = view.pose.position;
    glm::vec3 headOffset = partyYaw * glm::vec3(-position.z, -position.x, position.y) * GAME_UNITS_PER_METRE;

    Vec3f eye = pParty->pos + Vec3f(headOffset.x, headOffset.y, headOffset.z + pParty->eyeLevel);

    ProjectionDesc projection;
    projection.tanLeft = std::tan(view.fov.angleLeft);
    projection.tanRight = std::tan(view.fov.angleRight);
    projection.tanUp = std::tan(view.fov.angleUp);
    projection.tanDown = std::tan(view.fov.angleDown);
    projection.nearClip = pCamera3D->GetNearClip();
    projection.farClip = pCamera3D->GetFarClip();

    return CameraView::fromPose(eye, worldOrientation(view.pose.orientation), projection, Recti(0, 0, size.w, size.h));
}
