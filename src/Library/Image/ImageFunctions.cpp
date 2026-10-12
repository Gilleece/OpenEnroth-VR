#include "ImageFunctions.h"

#include <cassert>

RgbaImage makeRgbaImage(GrayscaleImageView indexedImage, const Palette &palette) {
    if (!indexedImage)
        return RgbaImage();

    RgbaImage result = RgbaImage::uninitialized(indexedImage.width(), indexedImage.height());
    auto srcPixels = indexedImage.pixels();
    auto dstPixels = result.pixels();
    assert(srcPixels.size() == dstPixels.size());

    for (size_t i = 0, size = srcPixels.size(); i < size; i++)
        dstPixels[i] = palette.colors[srcPixels[i]];

    return result;
}

RgbaImage flipVertically(RgbaImageView image) {
    if (!image)
        return RgbaImage();

    RgbaImage result = RgbaImage::uninitialized(image.width(), image.height());
    for (size_t y = 0, h = image.height(); y < h; y++)
        memcpy(result[h - y - 1].data(), image[y].data(), image[y].size_bytes());
    return result;
}

RgbaImage scaleNearest(RgbaImageView image, int factor) {
    assert(factor > 0);
    if (!image)
        return RgbaImage();

    RgbaImage result = RgbaImage::uninitialized(image.width() * factor, image.height() * factor);
    for (size_t y = 0, h = result.height(); y < h; y++) {
        std::span<const Color> src = image[y / factor];
        std::span<Color> dst = result[y];
        for (size_t x = 0, w = result.width(); x < w; x++)
            dst[x] = src[x / factor];
    }
    return result;
}
