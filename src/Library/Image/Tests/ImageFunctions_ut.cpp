#include "Testing/Unit/UnitTest.h"

#include "Library/Image/ImageFunctions.h"

UNIT_TEST(ImageFunctions, ScaleNearest) {
    // Every source pixel becomes a factor by factor block, and factor 1 is a copy.
    RgbaImage image = RgbaImage::uninitialized(2, 3);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 2; x++)
            image[y][x] = Color(x, y, 100 + x, 200 + y);

    for (int factor : {1, 2, 4}) {
        RgbaImage scaled = scaleNearest(image, factor);
        ASSERT_EQ(scaled.size(), Sizei(2 * factor, 3 * factor));
        for (int y = 0; y < 3 * factor; y++)
            for (int x = 0; x < 2 * factor; x++)
                EXPECT_EQ(scaled[y][x], image[y / factor][x / factor]);
    }
}

UNIT_TEST(ImageFunctions, ScaleNearestEmpty) {
    // An empty image stays empty.
    EXPECT_FALSE(scaleNearest(RgbaImage(), 2));
}

UNIT_TEST(ImageFunctions, DownscaleHalf) {
    // Each pixel is the rounded average of a 2x2 block.
    RgbaImage image = RgbaImage::uninitialized(4, 2);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 4; x++)
            image[y][x] = Color(10 * x + y, 255, 0, x < 2 ? 0 : 255);

    RgbaImage half = downscaleHalf(image);
    ASSERT_EQ(half.size(), Sizei(2, 1));
    EXPECT_EQ(half[0][0], Color(6, 255, 0, 0)); // (0 + 1 + 10 + 11 + 2) / 4.
    EXPECT_EQ(half[0][1], Color(26, 255, 0, 255)); // (20 + 21 + 30 + 31 + 2) / 4.
}

UNIT_TEST(ImageFunctions, DownscaleHalfOddSizes) {
    // Odd sizes round down and drop the last column, a side of 1 averages with itself, and 1x1 stays 1x1.
    RgbaImage image = RgbaImage::solid(Color(100, 100, 100, 100), 3, 1);
    image[0][2] = Color(200, 200, 200, 200);

    RgbaImage half = downscaleHalf(image);
    ASSERT_EQ(half.size(), Sizei(1, 1));
    EXPECT_EQ(half[0][0], Color(100, 100, 100, 100));

    RgbaImage single = downscaleHalf(RgbaImage::solid(Color(7, 8, 9, 10), 1, 1));
    ASSERT_EQ(single.size(), Sizei(1, 1));
    EXPECT_EQ(single[0][0], Color(7, 8, 9, 10));
}
