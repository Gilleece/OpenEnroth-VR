#include "ImageFunctions.h"

#include <algorithm>
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

RgbaImage downscaleHalf(RgbaImageView image) {
    assert(image);

    int w = image.width();
    int h = image.height();
    RgbaImage result = RgbaImage::uninitialized(std::max(1, w / 2), std::max(1, h / 2));
    for (int y = 0; y < result.height(); y++) {
        std::span<const Color> row0 = image[std::min(2 * y, h - 1)];
        std::span<const Color> row1 = image[std::min(2 * y + 1, h - 1)];
        std::span<Color> dst = result[y];
        for (int x = 0; x < result.width(); x++) {
            int x0 = std::min(2 * x, w - 1);
            int x1 = std::min(2 * x + 1, w - 1);
            const Color &a = row0[x0], &b = row0[x1], &c = row1[x0], &d = row1[x1];
            dst[x] = Color((a.r + b.r + c.r + d.r + 2) / 4, (a.g + b.g + c.g + d.g + 2) / 4,
                           (a.b + b.b + c.b + d.b + 2) / 4, (a.a + b.a + c.a + d.a + 2) / 4);
        }
    }
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
