#include <algorithm>
#include <string>

#include "Testing/Unit/UnitTest.h"

#include "Engine/Resources/TexturePack.h"

#include "Library/Compression/Compression.h"
#include "Library/FileSystem/Memory/MemoryFileSystem.h"
#include "Library/Image/Png.h"

#include "Utility/String/Format.h"

static RgbaImage makeImage(Sizei size) {
    RgbaImage result = RgbaImage::uninitialized(size);
    for (int y = 0; y < size.h; y++)
        for (int x = 0; x < size.w; x++)
            result[y][x] = Color(x, y, 128, 255);
    return result;
}

static std::string crcString(const Blob &blob) {
    return fmt::format("{:08x}", zlib::crc32(blob));
}

static void writeManifest(MemoryFileSystem *fs, std::string_view bitmaps, std::string_view icons = "", std::string_view extra = "") {
    fs->write("manifest.json", Blob::fromString(fmt::format(R"({{"format": 1, {} "bitmaps": {{{}}}, "icons": {{{}}}}})", extra, bitmaps, icons)));
}

UNIT_TEST(TexturePack, Image) {
    // A listed image made from the same LOD entry comes back as is, and the name is case-insensitive.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    RgbaImage image = makeImage({6, 4});
    writeManifest(&fs, fmt::format(R"("Wall": {{"crc32": "{}"}})", crcString(lodEntry)));
    fs.write("bitmaps/wall.png", png::encode(image));

    TexturePack pack(&fs);
    EXPECT_EQ(pack.count(TEXTURE_PACK_BITMAPS), 1);
    EXPECT_EQ(pack.count(TEXTURE_PACK_ICONS), 0);
    EXPECT_EQ(pack.colors(), TEXTURE_PACK_COLORS_RAW);

    RgbaImage result = pack.image(TEXTURE_PACK_BITMAPS, "WALL", lodEntry, {3, 2});
    ASSERT_EQ(result.size(), image.size());
    EXPECT_TRUE(std::ranges::equal(result.pixels(), image.pixels()));
}

UNIT_TEST(TexturePack, Sections) {
    // Bitmaps and icons are separate, an icon is looked up only in the icons folder.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    writeManifest(&fs, "", fmt::format(R"("arrow.pcx": {{"crc32": "{}"}})", crcString(lodEntry)));
    fs.write("icons/arrow.pcx.png", png::encode(makeImage({4, 4})));

    TexturePack pack(&fs);
    EXPECT_EQ(pack.count(TEXTURE_PACK_ICONS), 1);
    EXPECT_TRUE(pack.image(TEXTURE_PACK_ICONS, "arrow.pcx", lodEntry, {2, 2}));
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "arrow.pcx", lodEntry, {2, 2}));
}

UNIT_TEST(TexturePack, Scales) {
    // Only 1x, 2x and 4x the LOD size are accepted, with the same scale on both axes.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    writeManifest(&fs, fmt::format(R"("wall": {{"crc32": "{}"}})", crcString(lodEntry)));
    TexturePack pack(&fs);

    for (Sizei size : {Sizei(3, 2), Sizei(6, 4), Sizei(12, 8)}) {
        fs.write("bitmaps/wall.png", png::encode(makeImage(size)));
        EXPECT_EQ(pack.image(TEXTURE_PACK_BITMAPS, "wall", lodEntry, {3, 2}).size(), size);
    }

    for (Sizei size : {Sizei(24, 16), Sizei(6, 2), Sizei(9, 6), Sizei(4, 2)}) {
        fs.write("bitmaps/wall.png", png::encode(makeImage(size)));
        EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "wall", lodEntry, {3, 2}));
    }
}

UNIT_TEST(TexturePack, Rejected) {
    // Unlisted, missing, undecodable and stale images all fall back to the original.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    std::string crc = crcString(lodEntry);
    writeManifest(&fs, fmt::format(R"("missing": {{"crc32": "{0}"}}, "broken": {{"crc32": "{0}"}}, "stale": {{"crc32": "{0}"}})", crc));
    fs.write("bitmaps/unlisted.png", png::encode(makeImage({3, 2})));
    fs.write("bitmaps/broken.png", Blob::fromString("not a png"));
    fs.write("bitmaps/stale.png", png::encode(makeImage({3, 2})));

    TexturePack pack(&fs);
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "unlisted", lodEntry, {3, 2}));
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "missing", lodEntry, {3, 2}));
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "broken", lodEntry, {3, 2}));
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "stale", Blob::fromString("patched lod entry"), {3, 2}));
    EXPECT_FALSE(pack.image(TEXTURE_PACK_BITMAPS, "stale", Blob(), {3, 2}));
}

UNIT_TEST(TexturePack, Colors) {
    // The manifest can ask for the colors to be used as is.
    MemoryFileSystem fs("pack");
    writeManifest(&fs, "", "", R"("colors": "final",)");
    EXPECT_EQ(TexturePack(&fs).colors(), TEXTURE_PACK_COLORS_FINAL);
}

UNIT_TEST(TexturePack, BadManifest) {
    // A pack without a readable manifest of a known format is refused.
    MemoryFileSystem fs("pack");
    EXPECT_ANY_THROW(TexturePack{&fs});

    fs.write("manifest.json", Blob::fromString("{"));
    EXPECT_ANY_THROW(TexturePack{&fs});

    fs.write("manifest.json", Blob::fromString(R"({"format": 2})"));
    EXPECT_ANY_THROW(TexturePack{&fs});

    fs.write("manifest.json", Blob::fromString(R"({"bitmaps": {}})"));
    EXPECT_ANY_THROW(TexturePack{&fs});

    fs.write("manifest.json", Blob::fromString(R"({"format": 1, "colors": "vivid"})"));
    EXPECT_ANY_THROW(TexturePack{&fs});
}
