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

UNIT_TEST(TexturePack, SpriteImages) {
    // A sprite has one image per listed palette, and an unlisted palette has none.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod sprite");
    RgbaImage image = makeImage({6, 4});
    writeManifest(&fs, "", "", fmt::format(R"("sprites": {{"M452wa0": {{"crc32": "{}", "palettes": [453, 454, 455]}}}},)", crcString(lodEntry)));
    fs.write("sprites/m452wa0/453.png", png::encode(image));
    fs.write("sprites/m452wa0/455.png", png::encode(makeImage({5, 4})));

    TexturePack pack(&fs);
    EXPECT_EQ(pack.spriteImageCount(), 3);
    EXPECT_TRUE(pack.hasSpriteImage("m452wa0", 453));
    EXPECT_FALSE(pack.hasSpriteImage("m452wa0", 456));
    EXPECT_FALSE(pack.hasSpriteImage("m452wa1", 453));

    RgbaImage result = pack.spriteImage("M452WA0", 453, lodEntry, {3, 2});
    ASSERT_EQ(result.size(), image.size());
    EXPECT_TRUE(std::ranges::equal(result.pixels(), image.pixels()));

    EXPECT_FALSE(pack.spriteImage("m452wa0", 454, lodEntry, {3, 2})); // Listed, but the file is missing.
    EXPECT_FALSE(pack.spriteImage("m452wa0", 455, lodEntry, {3, 2})); // Wrong size.
    EXPECT_FALSE(pack.spriteImage("m452wa0", 456, lodEntry, {3, 2}));
    EXPECT_FALSE(pack.spriteImage("m452wa0", 453, Blob::fromString("patched lod sprite"), {3, 2}));
}

static RgbaImage solidImage(Sizei size, Color color) {
    return RgbaImage::solid(color, size);
}

UNIT_TEST(TexturePack, MaterialImage) {
    // Normal and roughness maps combine into one image, and a missing kind takes its default.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    std::string crc = crcString(lodEntry);
    writeManifest(&fs, fmt::format(R"("both": {{"crc32": "{0}", "maps": ["normal", "roughness"]}}, )"
                                   R"("normal": {{"crc32": "{0}", "maps": ["normal"]}}, )"
                                   R"("rough": {{"crc32": "{0}", "maps": ["roughness"]}}, )"
                                   R"("plain": {{"crc32": "{0}"}})", crc));
    for (std::string_view name : {"both", "normal"})
        fs.write(fmt::format("normal/bitmaps/{}.png", name), png::encode(solidImage({6, 4}, Color(10, 20, 30))));
    for (std::string_view name : {"both", "rough"})
        fs.write(fmt::format("roughness/bitmaps/{}.png", name), png::encode(solidImage({6, 4}, Color(255, 255, 255))));
    TexturePack pack(&fs);

    EXPECT_EQ(pack.materialImage("both", lodEntry, {6, 4})[0][0], Color(10, 20, 30, 255));
    EXPECT_EQ(pack.materialImage("normal", lodEntry, {6, 4})[0][0], Color(10, 20, 30, DEFAULT_MATERIAL_ROUGHNESS));
    EXPECT_EQ(pack.materialImage("rough", lodEntry, {6, 4})[0][0], Color(128, 128, 255, 255));
    EXPECT_EQ(pack.materialImage("rough", lodEntry, {6, 4}).size(), Sizei(6, 4));
    EXPECT_FALSE(pack.materialImage("plain", lodEntry, {6, 4}));
    EXPECT_FALSE(pack.materialImage("unlisted", lodEntry, {6, 4}));
}

UNIT_TEST(TexturePack, MaterialRoughnessEncoding) {
    // Roughness maps to alpha 1 to 255, alpha 0 stays free to mean "no material".
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    writeManifest(&fs, fmt::format(R"("rough": {{"crc32": "{}", "maps": ["roughness"]}})", crcString(lodEntry)));
    TexturePack pack(&fs);

    for (auto [gray, alpha] : {std::pair(0, 1), std::pair(90, 91), std::pair(128, 128), std::pair(255, 255)}) {
        fs.write("roughness/bitmaps/rough.png", png::encode(solidImage({2, 2}, Color(gray, gray, gray))));
        EXPECT_EQ(pack.materialImage("rough", lodEntry, {2, 2})[0][0].a, alpha) << "for gray " << gray;
    }
}

UNIT_TEST(TexturePack, MaterialRejected) {
    // A listed map that is missing, has the wrong size or was made from another LOD entry gives no material at all.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod entry");
    std::string crc = crcString(lodEntry);
    writeManifest(&fs, fmt::format(R"("missing": {{"crc32": "{0}", "maps": ["normal", "roughness"]}}, )"
                                   R"("small": {{"crc32": "{0}", "maps": ["normal"]}})", crc));
    fs.write("normal/bitmaps/missing.png", png::encode(solidImage({6, 4}, Color(10, 20, 30))));
    fs.write("normal/bitmaps/small.png", png::encode(solidImage({3, 2}, Color(10, 20, 30))));
    TexturePack pack(&fs);

    EXPECT_FALSE(pack.materialImage("missing", lodEntry, {6, 4}));
    EXPECT_FALSE(pack.materialImage("small", lodEntry, {6, 4}));
    EXPECT_TRUE(pack.materialImage("small", lodEntry, {3, 2}));
    EXPECT_FALSE(pack.materialImage("small", Blob::fromString("patched lod entry"), {3, 2}));
}

UNIT_TEST(TexturePack, MaterialBadMapName) {
    // An unknown map kind makes the manifest invalid.
    MemoryFileSystem fs("pack");
    writeManifest(&fs, R"("wall": {"crc32": "00000000", "maps": ["height"]})");
    EXPECT_ANY_THROW(TexturePack{&fs});
}

UNIT_TEST(TexturePack, SpriteShape) {
    // A sprite's shape image is listed by its thickness, shared by all palettes, and checked like the sprite images.
    MemoryFileSystem fs("pack");
    Blob lodEntry = Blob::fromString("lod sprite");
    RgbaImage shape = makeImage({8, 6});
    fs.write("manifest.json", Blob::fromString(fmt::format(
        R"({{"format": 1, "sprites": {{"Gobla0": {{"crc32": "{}", "palettes": [2], "shape": {{"thickness": 40.0}}}}, "goblb0": {{"crc32": "{}", "palettes": [2]}}}}}})",
        crcString(lodEntry), crcString(lodEntry))));
    fs.write("shape/sprites/gobla0.png", png::encode(shape));
    fs.write("shape/sprites/goblb0.png", png::encode(shape));

    TexturePack pack(&fs);
    EXPECT_EQ(pack.spriteShapeThickness("GOBLA0"), 40.0f);
    EXPECT_EQ(pack.spriteShapeThickness("goblb0"), 0.0f);
    EXPECT_EQ(pack.spriteShapeThickness("unlisted"), 0.0f);

    RgbaImage result = pack.spriteShapeImage("gobla0", lodEntry, {4, 3});
    ASSERT_EQ(result.size(), shape.size());
    EXPECT_TRUE(std::ranges::equal(result.pixels(), shape.pixels()));
    EXPECT_FALSE(pack.spriteShapeImage("goblb0", lodEntry, {4, 3})); // No thickness, no shape.
    EXPECT_FALSE(pack.spriteShapeImage("gobla0", Blob::fromString("other lod sprite"), {4, 3})); // Made from another sprite.
    EXPECT_FALSE(pack.spriteShapeImage("gobla0", lodEntry, {3, 3})); // Not a pack scale of the sprite.
}
