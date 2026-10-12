#pragma once

#include <string>
#include <memory>
#include <optional>

#include "Engine/Graphics/Renderer/TextureRenderId.h"

#include "Library/Geometry/Size.h"
#include "Library/Image/Image.h"
#include "Library/Image/Palette.h"

class ImageLoader;

class GraphicsImage {
 public:
    static std::unique_ptr<GraphicsImage> Create(RgbaImage image);
    static std::unique_ptr<GraphicsImage> Create(int width, int height);
    static std::unique_ptr<GraphicsImage> Create(Sizei size);
    static std::unique_ptr<GraphicsImage> Create(std::unique_ptr<ImageLoader> loader);

    ~GraphicsImage();

    int width();
    int height();
    Sizei size();

    RgbaImage &rgba();

    /**
     * @return                          Pixels to upload to the GPU. Same as `rgba()`, unless the loader provides a
     *                                  higher resolution replacement, sized `size()` times `textureScale()`.
     */
    RgbaImageView textureRgba();

    /**
     * @return                          How many times larger `textureRgba()` is than `size()`, a power of two.
     */
    int textureScale();

    const std::string &name();

    [[nodiscard]] TextureRenderId renderId();
    void releaseRenderId();

 private:
    GraphicsImage();

 private:
    std::string _name;
    std::unique_ptr<ImageLoader> _loader;
    std::optional<Sizei> _size; // From the loader's loadSize() or from the loaded image, whichever comes first.
    std::optional<RgbaImage> _rgba; // Empty image if the loader failed.
    std::optional<RgbaImage> _textureRgba; // Empty image if the loader has no replacement.
    TextureRenderId _renderId;
};
