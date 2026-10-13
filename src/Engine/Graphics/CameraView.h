#pragma once

#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Graphics/Camera.h"

#include "Library/Geometry/Rect.h"
#include "Library/Geometry/Vec.h"

/**
 * Perspective projection given as the tangents of the angles between the view direction and the sides of the
 * frustum. Signs follow OpenXR's `XrFovf`, so `tanLeft` and `tanDown` are negative when the view direction is inside
 * the frustum.
 */
struct ProjectionDesc {
    float tanLeft = 0;
    float tanRight = 0;
    float tanUp = 0;
    float tanDown = 0;
    float nearClip = 0;
    float farClip = 0;
};

/**
 * Everything the 3D pass needs to know about the camera it's rendering from.
 */
struct CameraView {
    /**
     * @param camera                    Camera to take the view from, at the party's eye or at any pose set with
     *                                  `Camera3D::updateFromPose`.
     * @param viewport                  Part of the render target to draw into.
     * @return                          View of `camera`.
     */
    static CameraView fromCamera3D(const Camera3D &camera, Recti viewport);

    /**
     * @param position                  Eye position in world space.
     * @param orientation               Rotation from view space to world space. View space looks along +x, with +y to
     *                                  the left and +z up, so the identity looks east, level and unrolled.
     * @param projection                Frustum to project with, can be asymmetric.
     * @param viewport                  Part of the render target to draw into.
     * @return                          View for the given pose.
     */
    static CameraView fromPose(Vec3f position, glm::quat orientation, const ProjectionDesc &projection, Recti viewport);

    Vec3f position;
    glm::mat3 worldToView = glm::mat3(1); // Same layout as `Camera3D::ViewMatrix`, apply as `vector * worldToView`.
    glm::mat4 viewMatrix = glm::mat4(1);
    glm::mat4 projectionMatrix = glm::mat4(1);
    ProjectionDesc projection;
    std::array<glm::vec4, FRUSTUM_PLANE_COUNT> frustumPlanes = {{}}; // Same layout as `Camera3D::FrustumPlanes`.
    Recti viewport;
};
