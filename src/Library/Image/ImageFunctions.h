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
