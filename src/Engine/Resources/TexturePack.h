#pragma once

#include <string_view>

#include "Engine/Data/TexturePackManifest.h"

#include "Library/Geometry/Size.h"
#include "Library/Image/Image.h"

#include "Utility/Memory/Blob.h"

class FileSystem;

enum class TexturePackSection {
    TEXTURE_PACK_BITMAPS,
    TEXTURE_PACK_ICONS,
};
using enum TexturePackSection;

/**
 * Higher resolution replacements for images in `bitmaps.lod` and `icons.lod`.
 *
 * A pack is a folder with a `manifest.json` and `bitmaps` and `icons` subfolders. Each replacement is a PNG named after
 * the LOD entry it replaces, e.g. `bitmaps/d8s2wl.png` or `icons/foo.pcx.png`.
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

 private:
    const FileSystem *_fs = nullptr;
    TexturePackManifest _manifest;
};
