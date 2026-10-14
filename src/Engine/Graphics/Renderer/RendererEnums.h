#pragma once

#include "Library/Serialization/SerializationFwd.h"

enum class RendererType {
    RENDERER_OPENGL,
    RENDERER_OPENGL_ES,
    RENDERER_NULL
};
using enum RendererType;
MM_DECLARE_SERIALIZATION_FUNCTIONS(RendererType)

/**
 * Antialiasing of the 3D world, applied when it's composited into the game viewport.
 */
enum class AntialiasingMode {
    ANTIALIASING_OFF,
    ANTIALIASING_FXAA, // Fast approximate antialiasing on the tonemapped colour.
};
using enum AntialiasingMode;
MM_DECLARE_SERIALIZATION_FUNCTIONS(AntialiasingMode)

/**
 * How the UI quad of a headset follows the head.
 */
enum class VrUiFollow {
    VR_UI_FOLLOW_FIXED, // Hangs in front of where the head looked when the screen opened, and stays there.
    VR_UI_FOLLOW_LAZY, // Stays put until the head turns or moves past a dead zone, then eases in front of it again.
};
using enum VrUiFollow;
MM_DECLARE_SERIALIZATION_FUNCTIONS(VrUiFollow)

/**
 * How the game screen is laid out in a headset.
 */
enum class VrHudLayout {
    VR_HUD_LAYOUT_CLASSIC, // The whole screen on one quad.
    VR_HUD_LAYOUT_SPREAD, // The HUD of the game screen on quads of its own around the view.
};
using enum VrHudLayout;
MM_DECLARE_SERIALIZATION_FUNCTIONS(VrHudLayout)

/**
 * What the upright sprites on world space billboards turn to face.
 */
enum class SpriteFacing {
    SPRITE_FACING_VIEW_PLANE, // All stand across the view, so that turning the view turns them all.
    SPRITE_FACING_VIEW_POSITION, // Each turns to the view's position, so that turning the view in place leaves them still.
};
using enum SpriteFacing;

/**
 * Filtering of sun shadow edges.
 */
enum class ShadowFilterMode {
    SHADOW_FILTER_PCF3X3, // 3x3 bilinear depth comparisons.
    SHADOW_FILTER_PCF1, // One bilinear depth comparison.
    SHADOW_FILTER_SOFT, // A penumbra that widens with the distance from the caster.
};
using enum ShadowFilterMode;
MM_DECLARE_SERIALIZATION_FUNCTIONS(ShadowFilterMode)

enum class TextureFilter {
    TEXTURE_FILTER_LINEAR,
    TEXTURE_FILTER_MIPMAPPED, // Trilinear, plus anisotropic where the driver supports it.
};
using enum TextureFilter;

enum class RenderTargetFormat {
    RENDER_TARGET_FORMAT_RGBA8,
    RENDER_TARGET_FORMAT_RGBA16F,
    RENDER_TARGET_FORMAT_R11G11B10F, // Half the size of RGBA16F, for colour without alpha.
    RENDER_TARGET_FORMAT_RG16F,
    RENDER_TARGET_FORMAT_R16F,
    RENDER_TARGET_FORMAT_R32F
};
using enum RenderTargetFormat;
