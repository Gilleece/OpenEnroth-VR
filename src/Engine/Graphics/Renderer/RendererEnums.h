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
 * Filtering of sun shadow edges.
 */
enum class ShadowFilterMode {
    SHADOW_FILTER_PCF3X3, // 3x3 bilinear depth comparisons.
    SHADOW_FILTER_PCF1, // One bilinear depth comparison.
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
    RENDER_TARGET_FORMAT_RGBA16F
};
using enum RenderTargetFormat;
