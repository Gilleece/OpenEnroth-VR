#include "CameraView.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

static glm::vec4 frustumPlane(glm::vec3 inwardNormal, glm::vec3 eye) {
    glm::vec3 normal = glm::normalize(inwardNormal);
    return glm::vec4(normal, glm::dot(normal, eye));
}

CameraView CameraView::fromCamera3D(const Camera3D &camera, Recti viewport) {
    CameraView result;

    float near_clip = camera.GetNearClip();
    float far_clip = camera.GetFarClip();

    result.projectionMatrix = glm::perspective(glm::radians(camera.fov_y_deg), camera.aspect, near_clip, far_clip);

    float camera_x = camera.vCameraPos.x;
    float camera_y = camera.vCameraPos.y;
    float camera_z = camera.vCameraPos.z;

    glm::vec3 campos = glm::vec3(camera_x, camera_y, camera_z);
    glm::vec3 eyepos = glm::vec3(camera_x - cosf(2.0f * M_PI * camera._viewYaw / 2048.0f),
        camera_y - sinf(2.0f * M_PI * camera._viewYaw / 2048.0f),
        camera_z - tanf(2.0f * M_PI * -camera._viewPitch / 2048.0f));
    glm::vec3 upvec = glm::vec3(0.0f, 0.0f, 1.0f);

    result.viewMatrix = glm::lookAtLH(campos, eyepos, upvec);

    float tanHalfFovY = std::tan(glm::radians(camera.fov_y_deg) / 2.0f);
    result.projection.tanLeft = -tanHalfFovY * camera.aspect;
    result.projection.tanRight = tanHalfFovY * camera.aspect;
    result.projection.tanUp = tanHalfFovY;
    result.projection.tanDown = -tanHalfFovY;
    result.projection.nearClip = near_clip;
    result.projection.farClip = far_clip;

    result.position = Vec3f(camera_x, camera_y, camera_z);
    result.worldToView = camera.ViewMatrix;
    result.frustumPlanes = camera.FrustumPlanes;
    result.viewport = viewport;
    return result;
}

CameraView CameraView::fromPose(Vec3f position, glm::quat orientation, const ProjectionDesc &projection, Recti viewport) {
    glm::mat3 rotation = glm::mat3_cast(orientation);
    glm::vec3 forward = rotation[0];
    glm::vec3 left = rotation[1];
    glm::vec3 up = rotation[2];
    glm::vec3 eye(position.x, position.y, position.z);

    // The shaders expect OpenGL's view space, which looks along -z, with +x to the right and +y up.
    glm::mat3 worldToGlView = glm::transpose(glm::mat3(-left, up, -forward));

    CameraView result;
    result.position = position;
    result.worldToView = rotation;
    result.viewMatrix = glm::mat4(worldToGlView);
    result.viewMatrix[3] = glm::vec4(-(worldToGlView * eye), 1.0f);
    result.projectionMatrix = glm::frustum(projection.tanLeft * projection.nearClip, projection.tanRight * projection.nearClip,
                                           projection.tanDown * projection.nearClip, projection.tanUp * projection.nearClip,
                                           projection.nearClip, projection.farClip);
    result.projection = projection;
    result.frustumPlanes[0] = frustumPlane(-projection.tanLeft * forward - left, eye);
    result.frustumPlanes[1] = frustumPlane(projection.tanRight * forward + left, eye);
    result.frustumPlanes[2] = frustumPlane(projection.tanUp * forward - up, eye);
    result.frustumPlanes[3] = frustumPlane(-projection.tanDown * forward + up, eye);
    result.viewport = viewport;
    return result;
}
