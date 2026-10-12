#include <array>
#include <cmath>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Testing/Game/GameTest.h"

#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/CameraView.h"
#include "Engine/Graphics/Viewport.h"
#include "Engine/Party.h"

static void expectNear(float actual, float expected) {
    EXPECT_NEAR(actual, expected, 1e-4f * std::max(1.0f, std::abs(expected)));
}

template<glm::length_t length>
static void expectNear(const glm::vec<length, float> &actual, const glm::vec<length, float> &expected) {
    for (int i = 0; i < length; i++)
        expectNear(actual[i], expected[i]);
}

template<glm::length_t columns, glm::length_t rows>
static void expectNear(const glm::mat<columns, rows, float> &actual, const glm::mat<columns, rows, float> &expected) {
    for (int column = 0; column < columns; column++)
        expectNear(actual[column], expected[column]);
}

static void expectNear(const std::array<glm::vec4, FRUSTUM_PLANE_COUNT> &actual, const std::array<glm::vec4, FRUSTUM_PLANE_COUNT> &expected) {
    for (int plane = 0; plane < FRUSTUM_PLANE_COUNT; plane++)
        expectNear(actual[plane], expected[plane]);
}

static glm::vec3 toGlm(Vec3f v) {
    return glm::vec3(v.x, v.y, v.z);
}

GAME_TEST(CameraView, FromPoseMatchesFromCamera3D) {
    // An unrolled pose with a symmetric frustum gives the same view as the party camera with the same yaw and pitch.
    game.startNewGame();

    for (int yaw : {0, 300, 1024, 1700}) {
        for (int pitch : {-128, 0, 100}) {
            pParty->pos = Vec3f(0, 0, -pParty->eyeLevel); // Puts the eye next to the origin, the legacy view matrix loses precision away from it.
            pParty->_viewYaw = yaw;
            pParty->_viewPitch = pitch;
            pCamera3D->updateFromParty();
            CameraView legacy = CameraView::fromCamera3D(*pCamera3D, pViewport);

            float yawRadians = 2 * M_PI * yaw / 2048;
            float pitchRadians = 2 * M_PI * pitch / 2048;
            glm::quat orientation = glm::angleAxis(yawRadians, glm::vec3(0, 0, 1)) * glm::angleAxis(-pitchRadians, glm::vec3(0, 1, 0));
            CameraView pose = CameraView::fromPose(legacy.position, orientation, legacy.projection, pViewport);

            expectNear(pose.worldToView, legacy.worldToView);
            expectNear(pose.viewMatrix, legacy.viewMatrix);
            expectNear(pose.projectionMatrix, legacy.projectionMatrix);
            expectNear(pose.frustumPlanes, legacy.frustumPlanes);
        }
    }
}

GAME_TEST(CameraView, FromPoseRolledAsymmetric) {
    // The edges of a rolled, asymmetric frustum land on the edges of clip space and on the matching frustum planes.
    ProjectionDesc projection = {.tanLeft = -1.2f, .tanRight = 0.8f, .tanUp = 0.9f, .tanDown = -0.6f, .nearClip = 4.0f, .farClip = 4000.0f};
    glm::quat orientation = glm::angleAxis(0.7f, glm::vec3(0, 0, 1)) * glm::angleAxis(-0.3f, glm::vec3(0, 1, 0)) * glm::angleAxis(0.4f, glm::vec3(1, 0, 0));
    Vec3f position(1000, -500, 300);
    CameraView view = CameraView::fromPose(position, orientation, projection, Recti(0, 0, 640, 480));

    glm::vec3 eye = toGlm(position);
    glm::vec3 forward = orientation * glm::vec3(1, 0, 0);
    glm::vec3 left = orientation * glm::vec3(0, 1, 0);
    glm::vec3 up = orientation * glm::vec3(0, 0, 1);

    expectNear(forward * view.worldToView, glm::vec3(1, 0, 0));
    expectNear(left * view.worldToView, glm::vec3(0, 1, 0));
    expectNear(up * view.worldToView, glm::vec3(0, 0, 1));

    auto project = [&](glm::vec3 point) {
        glm::vec4 clip = view.projectionMatrix * view.viewMatrix * glm::vec4(point, 1);
        return glm::vec3(clip) / clip.w;
    };
    auto distance = [&](int plane, glm::vec3 point) {
        return glm::dot(glm::vec3(view.frustumPlanes[plane]), point) - view.frustumPlanes[plane].w;
    };

    float depth = 100;
    glm::vec3 topLeft = eye + depth * (forward - projection.tanLeft * left + projection.tanUp * up);
    glm::vec3 bottomRight = eye + depth * (forward - projection.tanRight * left + projection.tanDown * up);
    expectNear(glm::vec2(project(topLeft)), glm::vec2(-1, 1));
    expectNear(glm::vec2(project(bottomRight)), glm::vec2(1, -1));
    EXPECT_NEAR(project(eye + projection.nearClip * forward).z, -1, 1e-4f);
    EXPECT_NEAR(project(eye + projection.farClip * forward).z, 1, 1e-4f);

    EXPECT_NEAR(distance(0, topLeft), 0, 1e-2f);
    EXPECT_NEAR(distance(1, bottomRight), 0, 1e-2f);
    EXPECT_NEAR(distance(2, topLeft), 0, 1e-2f);
    EXPECT_NEAR(distance(3, bottomRight), 0, 1e-2f);
    for (int plane = 0; plane < FRUSTUM_PLANE_COUNT; plane++)
        EXPECT_GT(distance(plane, eye + depth * forward), 1);
}
