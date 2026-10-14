#pragma once

#include <cstdint>
#include <array>

#include "Engine/Pid.h"

#include "Library/Color/Color.h"
#include "Library/Geometry/Vec.h"

#include "SpriteEnums.h"

class Sprite;
class SpriteFrame;
class GraphicsImage;

/**
 * Sprite that casts a sun shadow, drawn as a quad standing at `pos` and turned about the vertical axis to face the sun.
 */
struct SunShadowSprite {
    Sprite *sprite = nullptr; // Frame as seen from the sun.
    int paletteId = 0;
    Vec3f pos; // Bottom centre.
    float scale = 1;
    bool mirrored = false;
    float swayRatio = 0; // Part of the height by which the top sways in the wind, see `decorationSwayRatio`.
    float swayPhase = 0; // See `windSwayPhase`.
};

struct ODMRenderParams {
    int shading_dist_shade = 0x800;
    int shading_dist_shademist = 0x1000;
    int uNumPolygons = 0;
    unsigned int _unused_uNumEdges = 0;
    unsigned int _unused_uNumSurfs = 0;
    unsigned int _unused_uNumSpans = 0;
    unsigned int uNumBillboards = 0;
    float field_40 = 0;
    unsigned int bNoSky = 0;
    unsigned int bDoNotRenderDecorations = 0;
    int field_5C = 0;
    int field_60 = 0;
    int outdoor_no_wavy_water = 0;
    int outdoor_no_mist = 0;
    int building_gamme = 0;
    int terrain_gamma = 0;
};
extern ODMRenderParams *pODMRenderParams;

struct RenderVertexSoft {
    Vec3f vWorldPosition;
    Vec3f vWorldViewPosition;
    Vec2f vWorldViewProj;
    float _rhw = 0;
    float u = 0;
    float v = 0;
    float flt_2C = 0;

    friend bool operator==(const RenderVertexSoft &l, const RenderVertexSoft &r) = default;
};

struct RenderVertexD3D3 {
    Vec3f pos;
    float rhw = 0.0f;
    Color diffuse;
    Vec2f texcoord;
};

struct RenderBillboard {
    Vec2f screenspace_projection_factor;
    Vec2f scale; // World units per sprite pixel.
    Sprite* hwsprite = nullptr;
    const SpriteFrame *frame = nullptr; // Frame `hwsprite` is one side of.
    int octant = 0; // Index of `hwsprite` in `frame->sprites`.
    int yaw = 0; // Facing of the object, which picks the side of `frame` that a view sees.
    bool centered = false; // `worldPos` was lowered by half the sprite's height.
    int16_t uPaletteId = 0;
    int uIndoorSectorID = 0;
    BillboardFlags flags;
    Vec3f worldPos;
    Vec2f screenPos;
    int32_t view_space_z = 0;
    float view_space_L2 = 0.0f;
    Pid object_pid;
    uint16_t dimming_level = 0;
    Color sTintColor;
};

struct RenderBillboardD3D {
    enum class OpacityType : uint32_t {
        Transparent = 0,
        Opaque_1 = 1,
        Opaque_2 = 2,
        Opaque_3 = 3,
        NoBlend = 0xFFFFFFFF
    };
    using enum OpacityType;

    GraphicsImage *texture = nullptr;
    unsigned int uNumVertices = 4;
    std::array<RenderVertexD3D3, 4> pQuads;
    OpacityType opacity = Transparent;

    int view_space_z = 0;
    int sParentBillboardID = -1;
    int paletteId = 0;
    float view_space_L2 = 0.0f;
};

/**
 * Billboard placed in the world, to be drawn through the view and projection matrices. Vertex `i` sits at
 * `pos + offsets[i] + right * facing[i].x + up * facing[i].y`. For a sprite that stands on its bottom edge `up` is the
 * world's up and `right` is level and across the view it's drawn from, so that the sprite stands upright. For the rest
 * `right` and `up` are the axes of that view with the roll taken out.
 */
struct WorldBillboard {
    GraphicsImage *texture = nullptr; // Null for the solid particle texture.
    int paletteId = 0;
    RenderBillboardD3D::OpacityType opacity = RenderBillboardD3D::Transparent;
    int numVertices = 4;
    Vec3f pos;
    std::array<Vec3f, 4> offsets = {{}};
    std::array<Vec2f, 4> facing = {{}};
    std::array<Vec2f, 4> texcoords = {{}};
    std::array<Color, 4> colors = {{}};
    std::array<Vec3f, 4> corners = {{}}; // World space vertices, set by `Renderer::buildWorldBillboardQuads`.
    float distance = 0; // Distance from the eye the quads were built for, for fog.
    const SpriteFrame *frame = nullptr; // Set for sprites, see `RenderBillboard`.
    Pid id; // Of the object a sprite shows.
    int octant = 0;
    int yaw = 0;
    bool centered = false;
    Vec2f scale; // World units per sprite pixel.
    Vec3f right; // Level axis an upright sprite stands across, set with `corners`.
    const Sprite *sprite = nullptr; // Side of the sprite frame shown, null for billboards that aren't sprites.
    GraphicsImage *shape = nullptr; // Shape of the side shown, see `TexturePackShape`, null when there's none.
    float shapeThickness = 0; // Game units across the shape's depth range.
    Vec2f sideExtent; // Half width and height of the side shown in world units, negative half width when mirrored.
    GraphicsImage *crossfadeTexture = nullptr; // Neighbouring side blended in, null when there's none.
    std::array<Vec2f, 4> crossfadeTexcoords = {{}};
    float crossfade = 0; // Weight of `crossfadeTexture`, up to 0.5 at the boundary between the sides.
};
