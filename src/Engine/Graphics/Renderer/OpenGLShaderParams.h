#pragma once

#include <array>

#include <glm/glm.hpp>

#include "Library/Color/Colorf.h"
#include "Library/Geometry/Vec.h"

class OpenGLShader;

struct FogUniforms {
    Colorf color;
    float weakDensity = 0;
    float strongDensity = 0;
    float weakDistance = 0;
    float strongDistance = 0;
    float clipDistance = 0;

    void submit(const OpenGLShader &shader, bool decal = false) const;
};

struct LineVertex {
    Vec2f pos;
    Colorf color;
};

struct LineUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};

    void submit(const OpenGLShader &shader) const;
};

struct ForcePerVertex {
    Vec3f pos;
    float w = 0;
    Vec2f texuv;
    float texw = 0;
    float screenspace = 0;
    Colorf color;
    float texid = 0;
};

struct ForcePerUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    FogUniforms fog;
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct TwoDVertex {
    Vec3f pos;
    Vec2f texuv;
    Colorf color;
    float texid = 0;
    float paletteid = 0;
    bool nearest = false; // Sampled without filtering, not passed to the shader.
};

struct TwoDUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    int paltex2D = 1;
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct TextVertex {
    Vec3f pos;
    Vec2f texuv;
    std::array<Colorf, 4> colors; // One color per texture channel.
};

struct TextUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct DecalVertex {
    Vec3f pos;
    Vec2f texuv;
    float texunit = 0;
    Colorf color;
};

struct DecalUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    FogUniforms fog;
    float decalBias = 0.002f;
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct SkyVertex {
    Vec3f pos;
    Colorf color;
    Vec2f texsize;
    Vec2f timeoffset; // Animation offset in texels, wrapped to the texture size.
    float texid = 0;
};

struct ShadowVertex {
    Vec3f pos;
};

struct ContactShadowVertex {
    Vec3f pos;
    Vec3f shape; // Position in the ellipse, -1 to 1 on both axes, and the shadow's strength.
};

struct TorchShadowVertex {
    Vec3f pos;
    Vec3f normal;
};

struct ShadowSpriteVertex {
    Vec3f pos;
    Vec2f texuv;
    float paletteId = 0;
};

struct CompositeVertex {
    Vec2f pos;
};

struct CompositeUniforms {
    int source = 0;
    float exposure = 1;
    float gamma = 1;
    int bloom = 0;
    float bloomIntensity = 0;

    void submit(const OpenGLShader &shader) const;
};

struct FxaaUniforms {
    int source = 0;
    Vec2f texelSize;

    void submit(const OpenGLShader &shader) const;
};

struct BloomUniforms {
    int source = 0;
    Vec2f texelSize;
    int brightPass = 0;
    float threshold = 0;

    void submit(const OpenGLShader &shader) const;
};

struct SkyUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    Vec3f eye;
    float planeHeight = 0;
    float elevationBias = 0;
    int fade = 0;
    float fadeTop = 0;
    float fadeBottom = 0;
    float horizon = 0;
    FogUniforms fog;
    float fogDistance = 0;
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct BillboardVertex {
    Vec3f pos;
    Vec2f texuv;
    Colorf color;
    float screenspace = 0;
    float texid = 0;
    float blend = 0;
    float paletteId = 0;
    Vec3f shadowAnchor; // Where the billboard looks up the sun shadow, only read when `shadowed` is 1.
    float shadowed = 0;
    Vec2f crossfadeTexuv;
    float crossfade = 0; // Weight of the second texture.
    float crossfadePaletteId = 0;
    float crossfadeTexid = 0; // Only for batching, not passed to the shader.
    Vec3f shapeRight; // World direction of the sprite image's u, only read when `shapeThickness` isn't 0.
    Vec2f shapeSize; // World width and height of the sprite image, negative width when mirrored.
    float shapeThickness = 0;
    float shapeTexid = 0; // Only for batching, not passed to the shader.
};

struct BillboardUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    FogUniforms fog;
    float gamma = 0;
    int paltex2D = 1;
    int texture0 = 0;

    void submit(const OpenGLShader &shader) const;
};

struct ShaderVertex {
    Vec3f pos;
    Vec2f texuv;
    float texturelayer = 0;
    Vec3f normal;
    float attribs = 0;
};

struct PointLightUniform {
    float type = 0; // 0 = unused, 1 = stationary, 2 = mobile
    Vec3f position;
    Colorf ambient;
    Colorf diffuse;
    Colorf specular;
    float radius = 0;
};

struct SunLightUniform {
    Vec3f direction;
    Colorf ambient;
    Colorf diffuse;
    Colorf specular;
};

/**
 * The sun and the point lights the level was lit with in the current view, for the sprites.
 */
struct SpriteLightUniforms {
    SunLightUniform sun;
    std::array<PointLightUniform, 20> pointLights;

    void submit(const OpenGLShader &shader) const;
};

struct TerrainUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    Vec3f cameraPos;
    FogUniforms fog;
    float gamma = 0;
    int waterframe = 0;
    int textureArray0 = 0;
    int textureArray1 = 1;

    SunLightUniform sun;
    std::array<PointLightUniform, 20> pointLights;

    void submit(const OpenGLShader &shader) const;
};

struct OutBuildUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    Vec3f cameraPos;
    FogUniforms fog;
    float gamma = 0;
    int waterframe = 0;
    int flowtimer = 0;
    int flowtimerms = 0;
    int textureArray0 = 0;
    int watertiles = 1;

    SunLightUniform sun;
    std::array<PointLightUniform, 20> pointLights;

    void submit(const OpenGLShader &shader) const;
};

struct BSPUniforms {
    glm::mat4 projection = {};
    glm::mat4 view = {};
    Vec3f cameraPos;
    float gamma = 0;
    int waterframe = 0;
    int flowtimer = 0;
    int flowtimerms = 0;
    int textureArray0 = 0;

    SunLightUniform sun;
    std::array<PointLightUniform, 40> pointLights;

    void submit(const OpenGLShader &shader) const;
};
