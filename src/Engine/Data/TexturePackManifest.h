#pragma once

#include <string>
#include <unordered_map>

#include "Library/Json/JsonFwd.h"
#include "Library/Serialization/SerializationFwd.h"

/**
 * What the engine does with the colors of texture pack images.
 */
enum class TexturePackColors {
    TEXTURE_PACK_COLORS_RAW,    // Colors as in the LOD palettes. Bitmaps get the configured saturation and lightness.
    TEXTURE_PACK_COLORS_FINAL,  // Colors are used as is.
};
using enum TexturePackColors;
MM_DECLARE_SERIALIZATION_FUNCTIONS(TexturePackColors)
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackColors)

/**
 * One image in a texture pack, as the manifest lists it.
 */
struct TexturePackEntry {
    std::string crc32; // CRC-32 of the LOD entry the image replaces, as 8 hex digits.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackEntry)

/**
 * Contents of a texture pack's `manifest.json`.
 */
struct TexturePackManifest {
    int format = 0;
    TexturePackColors colors = TEXTURE_PACK_COLORS_RAW;
    std::unordered_map<std::string, TexturePackEntry> bitmaps; // Keyed by `bitmaps.lod` entry name.
    std::unordered_map<std::string, TexturePackEntry> icons; // Keyed by `icons.lod` entry name.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackManifest)
