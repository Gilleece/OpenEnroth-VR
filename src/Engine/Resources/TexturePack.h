#pragma once

#include <mutex>
#include <optional>
#include <string_view>

#include "Engine/Data/TexturePackManifest.h"
#include "Engine/Resources/GiProbes.h"

#include "Library/Geometry/Size.h"
#include "Library/Image/Image.h"

#include "Utility/Memory/Blob.h"

class FileSystem;

/**
 * Material alpha used when a pack has a normal map but no roughness map. At this roughness the material lighting
 * comes closest to the fixed specular exponent of the stock shaders.
 */
inline constexpr int DEFAULT_MATERIAL_ROUGHNESS = 91;

enum class TexturePackSection {
    TEXTURE_PACK_BITMAPS,
    TEXTURE_PACK_ICONS,
};
using enum TexturePackSection;

/**
 * Higher resolution replacements for images in `bitmaps.lod`, `icons.lod` and `sprites.lod`.
 *
 * A pack is a folder with a `manifest.json` and `bitmaps`, `icons` and `sprites` subfolders. Each bitmap or icon
 * replacement is a PNG named after the LOD entry it replaces, e.g. `bitmaps/d8s2wl.png` or `icons/foo.pcx.png`. Sprites
 * are stored with their palette applied, one PNG per palette, e.g. `sprites/m452wa0/453.png`, and can have a shape
 * image shared by all palettes, e.g. `shape/sprites/m452wa0.png`.
 */
class TexturePack {
 public:
    /**
     * @param fs                        File system rooted at the pack folder. Must outlive this object.
     * @throws Exception                If the manifest is missing, malformed or of an unsupported format.
     */
    explicit TexturePack(const FileSystem *fs);
    ~TexturePack();

    [[nodiscard]] TexturePackColors colors() const;

    /**
     * @param section                   Section to count images in.
     * @return                          Number of images the manifest lists in `section`.
     */
    [[nodiscard]] int count(TexturePackSection section) const;

    /**
     * @param section                   Section to look the image up in.
     * @param name                      LOD entry name, case-insensitive.
     * @param lodEntry                  The LOD entry the image replaces, as stored in the LOD.
     * @param lodSize                   Size of the LOD image.
     * @return                          Replacement image, sized `lodSize` times 1, 2 or 4, or an empty image if the
     *                                  pack doesn't replace this entry. A replacement that was made from a different
     *                                  LOD entry, is missing, or has a wrong size is logged and not returned.
     */
    [[nodiscard]] RgbaImage image(TexturePackSection section, std::string_view name, const Blob &lodEntry, Sizei lodSize) const;

    /**
     * @param name                      `bitmaps.lod` entry name, case-insensitive.
     * @param lodEntry                  The LOD entry the maps belong to, as stored in the LOD.
     * @param colorSize                 Size of the pack's color image for this entry. The maps must match it.
     * @return                          The entry's normal and roughness maps combined into one image, or an empty
     *                                  image if the pack has no maps for it or a listed map is missing or invalid.
     *                                  RGB is the tangent space normal, flat if there's no normal map. Alpha is
     *                                  `1 + roughness * 254 / 255`, `DEFAULT_MATERIAL_ROUGHNESS` if there's no
     *                                  roughness map, so that alpha 0 can mean "no material" elsewhere.
     */
    [[nodiscard]] RgbaImage materialImage(std::string_view name, const Blob &lodEntry, Sizei colorSize) const;

    /**
     * @return                          Number of sprite images the manifest lists, over all sprites and palettes.
     */
    [[nodiscard]] int spriteImageCount() const;

    /**
     * @param name                      `sprites.lod` entry name, case-insensitive.
     * @param paletteId                 Palette the sprite is drawn with.
     * @return                          Whether the manifest lists an image for this sprite and palette. The image
     *                                  itself is only checked by `spriteImage()`.
     */
    [[nodiscard]] bool hasSpriteImage(std::string_view name, int paletteId) const;

    /**
     * @param name                      `sprites.lod` entry name, case-insensitive.
     * @param paletteId                 Palette the sprite is drawn with.
     * @param lodEntry                  The LOD entry the image replaces, as stored in the LOD.
     * @param lodSize                   Size of the LOD sprite.
     * @return                          The sprite with the palette applied, sized `lodSize` times 1, 2 or 4, or an
     *                                  empty image under the same conditions as `image()`.
     */
    [[nodiscard]] RgbaImage spriteImage(std::string_view name, int paletteId, const Blob &lodEntry, Sizei lodSize) const;

    /**
     * @param name                      `sprites.lod` entry name, case-insensitive.
     * @return                          Thickness of the sprite's shape in game units, or 0 if the pack has no shape
     *                                  for it.
     */
    [[nodiscard]] float spriteShapeThickness(std::string_view name) const;

    /**
     * @param name                      `sprites.lod` entry name, case-insensitive.
     * @param lodEntry                  The LOD entry the shape belongs to, as stored in the LOD.
     * @param lodSize                   Size of the LOD sprite.
     * @return                          The sprite's shape image, see `TexturePackShape`, sized `lodSize` times 1, 2
     *                                  or 4, or an empty image under the same conditions as `image()`.
     */
    [[nodiscard]] RgbaImage spriteShapeImage(std::string_view name, const Blob &lodEntry, Sizei lodSize) const;

    /**
     * @param mapFile                   `games.lod` map file name, case-insensitive, e.g. `d18.blv`.
     * @param lodEntry                  The map's entry in `games.lod`, as stored in the LOD.
     * @return                          The map's baked indirect light, or `std::nullopt` if the pack has none for it.
     *                                  Probes baked from a different map entry, or a missing or malformed probe file,
     *                                  are logged and not returned.
     */
    [[nodiscard]] std::optional<GiProbes> giProbes(std::string_view mapFile, const Blob &lodEntry) const;

 private:
    const FileSystem *_fs = nullptr;
    mutable std::mutex _fsMutex; // The image functions may be called from several threads at once.
    TexturePackManifest _manifest;
};
