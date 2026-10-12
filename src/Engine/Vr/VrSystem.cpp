#include "VrSystem.h"

#include <windows.h>

#include <algorithm>
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
#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/CameraView.h"
#include "Engine/Graphics/WorldFrame.h"
#include "Engine/Graphics/Renderer/Renderer.h"

#include "Application/GameConfig.h"

#include "Io/InputEnums.h"
#include "Io/KeyboardActionMapping.h"

#include "Library/Logger/Logger.h"
#include "Library/Platform/Interface/PlatformEvents.h"

#include "Utility/String/Format.h"

static constexpr XrViewConfigurationType VIEW_CONFIGURATION = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
static constexpr float GAME_UNITS_PER_METRE = 100.0f; // The party's eye level of 160 units is a 1.6 m standing eye height.
static constexpr int64_t LOGGED_FRAME_COUNT = 5;
static constexpr int64_t FRAME_SUMMARY_INTERVAL = 900;

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

VrSystem::VrSystem() : PlatformEventFilter({EVENT_KEY_PRESS}) {}

VrSystem::~VrSystem() {
    shutdown();
}

bool VrSystem::initialize() {
    return createInstance() && checkGraphicsRequirements() && createSession() && createSwapchains();
}

bool VrSystem::drawViews(WorldFrame &frame) {
    if (!_frameBegun || !_frameState.shouldRender || !frame.prepared() || _space == XR_NULL_HANDLE)
        return false;

    XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = VIEW_CONFIGURATION;
    locateInfo.displayTime = _frameState.predictedDisplayTime;
    locateInfo.space = _space;
    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    uint32_t viewCount = 0;
    if (!check(xrLocateViews(_session, &locateInfo, &viewState, static_cast<uint32_t>(views.size()), &viewCount, views.data()), "xrLocateViews"))
        return false;
    if (!(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return false;
    clampHeadOffset(views, engine->config->vr.HeadOffsetRadius.value());

    // Both eyes draw the same billboard quads, so that only the eye projections differ.
    XrView head = midView(views);
    render->buildWorldBillboardQuads(eyeView(head, _eyes[0].size));

    std::array<int, 2> billboards = {};
    if (!drawEye(0, views[0], frame))
        return false;
    billboards[0] = render->worldBillboardsDrawn;
    _layerReady = drawEye(1, views[1], frame);
    billboards[1] = render->worldBillboardsDrawn;
    render->setExternalWorldTarget(TextureRenderId(), Sizei());

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
    return true;
}

void VrSystem::swapBuffers() {
    endFrame();
    ProxyOpenGLContext::swapBuffers();
    pollEvents();
    beginFrame();
}

bool VrSystem::keyPressEvent(const PlatformKeyEvent *event) {
    if (!event->isAutoRepeat && keyboardActionMapping && keyboardActionMapping->isBound(INPUT_ACTION_VR_RECENTER, event->key))
        _recenterRequested = true;
    return false;
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
        MM_INFO("VR: headset '{}', vendor {:#x}, swapchains up to {}x{}, orientation tracking {}, position tracking {}.",
                systemProperties.systemName, systemProperties.vendorId,
                systemProperties.graphicsProperties.maxSwapchainImageWidth, systemProperties.graphicsProperties.maxSwapchainImageHeight,
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

bool VrSystem::createSwapchains() {
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

    // The runtime lists its formats in its order of preference.
    auto format = std::ranges::find_if(formats, [](int64_t format) { return format == GL_SRGB8_ALPHA8 || format == GL_RGBA8; });
    if (format == formats.end()) {
        MM_ERROR("VR: the runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8 swapchains.");
        return false;
    }
    MM_INFO("VR: using {} swapchains.", *format == GL_SRGB8_ALPHA8 ? "GL_SRGB8_ALPHA8" : "GL_RGBA8");
    _swapchainFormat = *format;

    for (size_t i = 0; i < _eyes.size(); i++) {
        Swapchain &eye = _eyes[i];
        if (!createSwapchain(&eye, Sizei(views[i].recommendedImageRectWidth, views[i].recommendedImageRectHeight)))
            return false;
        MM_INFO("VR: eye {} swapchain is {}x{} with {} images.", i, eye.size.w, eye.size.h, eye.images.size());
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
    if (target->swapchain != XR_NULL_HANDLE)
        xrDestroySwapchain(target->swapchain);
    *target = Swapchain();
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

    if (_focused && (_space == XR_NULL_HANDLE || _recenterRequested) && captureOrigin(_frameState.predictedDisplayTime))
        _recenterRequested = false;

    if (_frameIndex < LOGGED_FRAME_COUNT) {
        MM_INFO("VR: frame {} begun, display at {} ns, period {} ns, {}.", _frameIndex, _frameState.predictedDisplayTime,
                _frameState.predictedDisplayPeriod, _frameState.shouldRender ? "should render" : "should not render");
    }
}

void VrSystem::endFrame() {
    if (!_frameBegun)
        return;

    std::vector<const XrCompositionLayerBaseHeader *> layers;
    XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    if (_layerReady) {
        projection.space = _space;
        projection.viewCount = static_cast<uint32_t>(_projectionViews.size());
        projection.views = _projectionViews.data();
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection));
    }
    XrCompositionLayerQuad ui = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (_frameState.shouldRender && copyUi(&ui))
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&ui));

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = _frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();
    check(xrEndFrame(_session, &endInfo), "xrEndFrame");

    if (_layerReady)
        _worldFrameCount++;
    if (_frameIndex < LOGGED_FRAME_COUNT || (_layerReady && _worldFrameCount <= LOGGED_FRAME_COUNT))
        MM_INFO("VR: frame {} ended with {} layer(s).", _frameIndex, endInfo.layerCount);

    _frameIndex++;
    if (_frameIndex % FRAME_SUMMARY_INTERVAL == 0)
        MM_INFO("VR: {} frames submitted, {} of them with the world drawn.", _frameIndex, _worldFrameCount);

    _frameBegun = false;
    _layerReady = false;
}

bool VrSystem::copyUi(XrCompositionLayerQuad *layer) {
    if (!render->uiLayer())
        return false;

    Sizei size = render->GetRenderDimensions();
    if (_ui.size != size) {
        destroySwapchain(&_ui);
        if (!createSwapchain(&_ui, size))
            return false;
        MM_INFO("VR: UI swapchain is {}x{} with {} images, shown {:.2f} m wide at {:.2f} m.", size.w, size.h, _ui.images.size(),
                engine->config->vr.UiWidth.value(), engine->config->vr.UiDistance.value());
    }

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(xrAcquireSwapchainImage(_ui.swapchain, &acquireInfo, &imageIndex), "xrAcquireSwapchainImage"))
        return false;
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (!check(xrWaitSwapchainImage(_ui.swapchain, &waitInfo), "xrWaitSwapchainImage"))
        return false;
    render->copyUiLayer(TextureRenderId(_ui.images[imageIndex]), _ui.size);
    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(_ui.swapchain, &releaseInfo), "xrReleaseSwapchainImage");

    float width = engine->config->vr.UiWidth.value();
    layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // The UI target holds premultiplied colour.
    layer->space = _viewSpace;
    layer->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer->subImage.swapchain = _ui.swapchain;
    layer->subImage.imageRect.extent = {_ui.size.w, _ui.size.h};
    layer->pose.orientation.w = 1.0f;
    layer->pose.position.z = -engine->config->vr.UiDistance.value(); // VIEW space looks along -z.
    layer->size = {width, width * _ui.size.h / _ui.size.w};
    return true;
}

bool VrSystem::captureOrigin(XrTime time) {
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    if (!check(xrLocateSpace(_viewSpace, _localSpace, time, &head), "xrLocateSpace"))
        return false;
    XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((head.locationFlags & tracked) != tracked)
        return false;

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

    const XrVector3f &position = head.pose.position;
    MM_INFO("VR: head origin at ({:.3f}, {:.3f}, {:.3f}) m in the runtime's LOCAL space, turned {:.1f} degrees.",
            position.x, position.y, position.z, yaw * 180 / M_PI);
    return true;
}

bool VrSystem::drawEye(int index, const XrView &view, WorldFrame &frame) {
    Swapchain &eye = _eyes[index];

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(xrAcquireSwapchainImage(eye.swapchain, &acquireInfo, &imageIndex), "xrAcquireSwapchainImage"))
        return false;
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (!check(xrWaitSwapchainImage(eye.swapchain, &waitInfo), "xrWaitSwapchainImage"))
        return false;

    TextureRenderId texture(eye.images[imageIndex]);
    render->setExternalWorldTarget(texture, eye.size);
    render->BeginScene3D(eyeView(view, eye.size));
    frame.draw();
    render->EndScene3D();
    if (index == 0)
        render->setViewportMirror(texture, eye.size);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(eye.swapchain, &releaseInfo), "xrReleaseSwapchainImage");

    XrCompositionLayerProjectionView &projectionView = _projectionViews[index];
    projectionView = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    projectionView.pose = view.pose;
    projectionView.fov = view.fov;
    projectionView.subImage.swapchain = eye.swapchain;
    projectionView.subImage.imageRect.extent = {eye.size.w, eye.size.h};
    return true;
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
        MM_INFO("VR: shut down.");
    }
    _space = XR_NULL_HANDLE;
    _viewSpace = XR_NULL_HANDLE;
    _localSpace = XR_NULL_HANDLE;
    _session = XR_NULL_HANDLE;
    _instance = XR_NULL_HANDLE;
}

CameraView VrSystem::eyeView(const XrView &view, Sizei size) const {
    glm::quat partyYaw = glm::angleAxis(static_cast<float>(2 * M_PI * pParty->_viewYaw / 2048), glm::vec3(0, 0, 1));

    // OpenXR looks along -z with +x to the right and +y up, the game looks along +x with +y to the left and +z up.
    const XrQuaternionf &orientation = view.pose.orientation;
    glm::quat headOrientation(orientation.w, -orientation.z, -orientation.x, orientation.y);
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

    return CameraView::fromPose(eye, partyYaw * headOrientation, projection, Recti(0, 0, size.w, size.h));
}
