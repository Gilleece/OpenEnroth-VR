#pragma once

#include "Image.h"
#include "Palette.h"

RgbaImage makeRgbaImage(GrayscaleImageView indexedImage, const Palette &palette);

RgbaImage flipVertically(RgbaImageView image);

/**
 * @param image                         Image to scale.
 * @param factor                        Positive scale factor.
 * @return                              `image` scaled up `factor` times in both dimensions, every pixel repeated in a
 *                                      `factor` by `factor` block.
 */
RgbaImage scaleNearest(RgbaImageView image, int factor);

/**
 * @param image                         Image to shrink, not empty.
 * @return                              The next mip level of `image`: half its size in each dimension, rounded down
 *                                      and at least 1, every pixel the rounded average of the up to four pixels it
 *                                      covers.
 */
RgbaImage downscaleHalf(RgbaImageView image);
