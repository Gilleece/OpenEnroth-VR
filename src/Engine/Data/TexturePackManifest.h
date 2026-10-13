#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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
 * Material map that a texture pack can provide next to a bitmap.
 */
enum class TexturePackMap {
    TEXTURE_PACK_MAP_NORMAL,    // Tangent space normal map, `normal/bitmaps/<name>.png`.
    TEXTURE_PACK_MAP_ROUGHNESS, // Grayscale roughness map, `roughness/bitmaps/<name>.png`.
};
using enum TexturePackMap;
MM_DECLARE_SERIALIZATION_FUNCTIONS(TexturePackMap)
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackMap)

/**
 * One image in a texture pack, as the manifest lists it.
 */
struct TexturePackEntry {
    std::string crc32; // CRC-32 of the LOD entry the image replaces, as 8 hex digits.
    std::vector<TexturePackMap> maps; // Only bitmaps have maps.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackEntry)

/**
 * Shape image of a sprite, `shape/sprites/<name>.png`. RG is the surface normal's x (right) and y (up) as
 * `n * 0.5 + 0.5`, B is the depth, 255 nearest the viewer and 127.5 on the sprite's plane, A is the coverage.
 */
struct TexturePackShape {
    float thickness = 0; // Game units from B = 0 to B = 255, 0 if the sprite has no shape.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackShape)

/**
 * One sprite in a texture pack, as the manifest lists it. A sprite has one image per palette it's drawn with.
 */
struct TexturePackSpriteEntry {
    std::string crc32; // CRC-32 of the `sprites.lod` entry the images replace, as 8 hex digits.
    std::vector<int> palettes; // Ids of the palettes the pack has an image for.
    TexturePackShape shape; // Shared by all palettes.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackSpriteEntry)

/**
 * Contents of a texture pack's `manifest.json`.
 */
struct TexturePackManifest {
    int format = 0;
    TexturePackColors colors = TEXTURE_PACK_COLORS_RAW;
    std::unordered_map<std::string, TexturePackEntry> bitmaps; // Keyed by `bitmaps.lod` entry name.
    std::unordered_map<std::string, TexturePackEntry> icons; // Keyed by `icons.lod` entry name.
    std::unordered_map<std::string, TexturePackSpriteEntry> sprites; // Keyed by `sprites.lod` entry name.
};
MM_DECLARE_JSON_SERIALIZATION_FUNCTIONS(TexturePackManifest)
