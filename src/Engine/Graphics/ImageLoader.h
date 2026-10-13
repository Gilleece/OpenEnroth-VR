#pragma once

#include <string>
#include <string_view>

#include "Library/Geometry/Size.h"
#include "Library/Image/Image.h"

class ResourceManager;

class ImageLoader {
 public:
    ImageLoader(ResourceManager *resources, std::string_view name) : _resources(resources), _name(name) {}
    virtual ~ImageLoader() = default;

    const std::string &name() const { return _name; }

    /**
     * @return                          Loaded image, or an empty image if loading failed.
     */
    virtual RgbaImage loadRgba() = 0;

    /**
     * @return                          Size of the image `loadRgba()` returns, read without loading it. Empty exactly
     *                                  when `loadRgba()` fails.
     */
    virtual Sizei loadSize() = 0;

    /**
     * @return                          Higher resolution replacement for the image `loadRgba()` returns, sized
     *                                  `loadSize()` times a power of two, or an empty image if there is none.
     */
    virtual RgbaImage loadTextureRgba() { return {}; }

    /**
     * @param textureSize               Size of the image the material belongs to, the one `loadTextureRgba()`
     *                                  returns, or `loadSize()` if there is no replacement.
     * @return                          Normal and roughness maps for the image as `TexturePack::materialImage()`
     *                                  combines them, sized `textureSize`, or an empty image if there are none.
     */
    virtual RgbaImage loadMaterialRgba(Sizei textureSize) { return {}; }

 protected:
    ResourceManager *resources() const { return _resources; }

 private:
    ResourceManager *_resources;
    std::string _name;
};

class Icon_LOD_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
    virtual RgbaImage loadTextureRgba() override;
};

class Buff_LOD_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
};

class Bitmaps_LOD_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
    virtual RgbaImage loadTextureRgba() override;
    virtual RgbaImage loadMaterialRgba(Sizei textureSize) override;
};

class Bitmaps_GEN_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
};

class Sprites_LOD_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
};

/**
 * The shape of a sprite, see `TexturePackShape`. Without a texture pack image it's flat, at the sprite's plane, with
 * the sprite's coverage. Its texture is the texture pack's shape image.
 */
class SpriteShape_LOD_Loader : public ImageLoader {
 public:
    using ImageLoader::ImageLoader;

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
    virtual RgbaImage loadTextureRgba() override;
};

/**
 * A sprite drawn with one palette, as a true colour image. Its texture is the texture pack's image for that palette.
 */
class SpriteVariant_LOD_Loader : public ImageLoader {
 public:
    SpriteVariant_LOD_Loader(ResourceManager *resources, std::string_view name, int paletteId) : ImageLoader(resources, name), _paletteId(paletteId) {}

    virtual RgbaImage loadRgba() override;
    virtual Sizei loadSize() override;
    virtual RgbaImage loadTextureRgba() override;

 private:
    int _paletteId = 0;
};
