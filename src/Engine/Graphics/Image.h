#pragma once

#include <string>
#include <memory>
#include <optional>
#include <span>

#include "Engine/Graphics/Renderer/TextureRenderId.h"

#include "Library/Geometry/Size.h"
#include "Library/Image/Image.h"
#include "Library/Image/Palette.h"

class ImageLoader;
struct DecodedReplacement;

class GraphicsImage {
 public:
    static std::unique_ptr<GraphicsImage> Create(RgbaImage image);
    static std::unique_ptr<GraphicsImage> Create(int width, int height);
    static std::unique_ptr<GraphicsImage> Create(Sizei size);
    static std::unique_ptr<GraphicsImage> Create(std::unique_ptr<ImageLoader> loader);

    /**
     * Loads the higher resolution replacements of `images` and their materials on several threads, so that
     * `textureRgba()` and `materialRgba()` find them loaded. Their loaders must be safe to call from several threads
     * at once, like those that read from a texture pack. Images whose replacements are loaded already are skipped.
     *
     * @param images                    Images to load the replacements of, each at most once.
     */
    static void loadReplacements(std::span<GraphicsImage *const> images);

    ~GraphicsImage();

    int width();
    int height();
    Sizei size();

    RgbaImage &rgba();

    /**
     * Replaces the pixels of an image made from an `RgbaImage`, and those of its texture if it has one.
     *
     * @param image                     New pixels, the same size as the image.
     */
    void update(RgbaImage image);

    /**
     * @return                          Pixels to upload to the GPU. Same as `rgba()`, unless the loader provides a
     *                                  higher resolution replacement, sized `size()` times `textureScale()`.
     */
    RgbaImageView textureRgba();

    /**
     * @return                          How many times larger `textureRgba()` is than `size()`, a power of two.
     */
    int textureScale();

    /**
     * @return                          Normal and roughness maps that go with `textureRgba()`, same size as it, or an
     *                                  empty image if there are none. See `TexturePack::materialImage()` for the
     *                                  format.
     */
    RgbaImageView materialRgba();

    const std::string &name();

    [[nodiscard]] TextureRenderId renderId();
    void releaseRenderId();

    /**
     * @return                          Whether the image has its texture on the GPU, so that `renderId()` returns at
     *                                  once.
     */
    [[nodiscard]] bool hasRenderId() const {
        return static_cast<bool>(_renderId);
    }

    /**
     * Decodes the image's texture pack replacement and material through its loader, with the replacement's mip
     * levels when it's scaled up. Touches nothing else of the image, so it can run on another thread while the image
     * is in use, as long as the loader is safe to call from there.
     *
     * @return                          The decoded replacement, without `image` set.
     */
    [[nodiscard]] DecodedReplacement decodeReplacement() const;

    /**
     * Takes a replacement that `decodeReplacement` decoded and makes the image's texture from it, unless the image has
     * one already.
     *
     * @param decoded                   Replacement of this image.
     */
    void installReplacement(DecodedReplacement decoded);

 private:
    GraphicsImage();

 private:
    std::string _name;
    std::unique_ptr<ImageLoader> _loader;
    std::optional<Sizei> _size; // From the loader's loadSize() or from the loaded image, whichever comes first.
    std::optional<RgbaImage> _rgba; // Empty image if the loader failed.
    std::optional<RgbaImage> _textureRgba; // Empty image if the loader has no replacement.
    std::optional<RgbaImage> _materialRgba; // Empty image if the loader has no material.
    TextureRenderId _renderId;
};
