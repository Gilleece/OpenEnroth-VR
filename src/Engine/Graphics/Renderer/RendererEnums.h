#pragma once

#include "Library/Serialization/SerializationFwd.h"

enum class RendererType {
    RENDERER_OPENGL,
    RENDERER_OPENGL_ES,
    RENDERER_NULL
};
using enum RendererType;
MM_DECLARE_SERIALIZATION_FUNCTIONS(RendererType)

enum class TextureFilter {
    TEXTURE_FILTER_LINEAR,
    TEXTURE_FILTER_MIPMAPPED, // Trilinear, plus anisotropic where the driver supports it.
};
using enum TextureFilter;
