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
