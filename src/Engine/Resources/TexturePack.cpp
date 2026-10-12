#include "TexturePack.h"

#include <algorithm>
#include <string>
#include <exception>
#include <unordered_map>
#include <utility>

#include "Library/Compression/Compression.h"
#include "Library/FileSystem/Interface/FileSystem.h"
#include "Library/Image/Png.h"
#include "Library/Json/Json.h"
#include "Library/Logger/Logger.h"

#include "Utility/Exception.h"
#include "Utility/String/Ascii.h"
#include "Utility/String/Format.h"

static constexpr int TEXTURE_PACK_FORMAT = 1;
static constexpr int MAX_TEXTURE_PACK_SCALE = 4;

template<class Entry>
static std::unordered_map<std::string, Entry> lowercaseKeys(std::unordered_map<std::string, Entry> entries) {
    std::unordered_map<std::string, Entry> result;
    for (auto &[name, entry] : entries)
        result.emplace(ascii::toLower(name), std::move(entry));
    return result;
}

static bool isPackScale(Sizei size, Sizei lodSize) {
    for (int scale = 1; scale <= MAX_TEXTURE_PACK_SCALE; scale *= 2)
        if (size == Sizei(lodSize.w * scale, lodSize.h * scale))
            return true;
    return false;
}

static RgbaImage loadImage(const FileSystem *fs, std::string_view path, std::string_view crc, const Blob &lodEntry, Sizei lodSize) {
    std::string lodCrc = fmt::format("{:08x}", zlib::crc32(lodEntry));
    if (ascii::toLower(crc) != lodCrc) {
        MM_WARNING("Texture pack image '{}' was made from a different LOD entry. The manifest says CRC-32 {}, the LOD entry has {}. Using the original.",
                   fs->displayPath(path), crc, lodCrc);
        return {};
    }

    if (!fs->exists(path)) {
        MM_WARNING("Texture pack image '{}' is listed in the manifest but doesn't exist. Using the original.", fs->displayPath(path));
        return {};
    }

    RgbaImage result;
    try {
        result = png::decode(fs->read(path));
    } catch (const std::exception &e) {
        MM_WARNING("Could not decode texture pack image '{}': {}. Using the original.", fs->displayPath(path), e.what());
        return {};
    }

    if (!isPackScale(result.size(), lodSize)) {
        MM_WARNING("Texture pack image '{}' is {}x{}, but must be {}x{} times 1, 2 or 4. Using the original.",
                   fs->displayPath(path), result.width(), result.height(), lodSize.w, lodSize.h);
        return {};
    }

    return result;
}

TexturePack::TexturePack(const FileSystem *fs) : _fs(fs) {
    try {
        from_json(Json::parse(_fs->read("manifest.json").str()), _manifest);
    } catch (const std::exception &e) {
        throw Exception("Could not read texture pack manifest '{}': {}", _fs->displayPath("manifest.json"), e.what());
    }

    if (_manifest.format != TEXTURE_PACK_FORMAT)
        throw Exception("Texture pack manifest '{}' has format {}, only format {} is supported.",
                        _fs->displayPath("manifest.json"), _manifest.format, TEXTURE_PACK_FORMAT);

    _manifest.bitmaps = lowercaseKeys(std::move(_manifest.bitmaps));
    _manifest.icons = lowercaseKeys(std::move(_manifest.icons));
    _manifest.sprites = lowercaseKeys(std::move(_manifest.sprites));
}

TexturePack::~TexturePack() = default;

TexturePackColors TexturePack::colors() const {
    return _manifest.colors;
}

int TexturePack::count(TexturePackSection section) const {
    return static_cast<int>(section == TEXTURE_PACK_BITMAPS ? _manifest.bitmaps.size() : _manifest.icons.size());
}

RgbaImage TexturePack::image(TexturePackSection section, std::string_view name, const Blob &lodEntry, Sizei lodSize) const {
    const auto &entries = section == TEXTURE_PACK_BITMAPS ? _manifest.bitmaps : _manifest.icons;
    std::string lowercaseName = ascii::toLower(name);
    auto pos = entries.find(lowercaseName);
    if (pos == entries.end() || !lodEntry)
        return {};

    std::string path = fmt::format("{}/{}.png", section == TEXTURE_PACK_BITMAPS ? "bitmaps" : "icons", lowercaseName);
    return loadImage(_fs, path, pos->second.crc32, lodEntry, lodSize);
}

int TexturePack::spriteImageCount() const {
    int result = 0;
    for (const auto &[name, entry] : _manifest.sprites)
        result += static_cast<int>(entry.palettes.size());
    return result;
}

bool TexturePack::hasSpriteImage(std::string_view name, int paletteId) const {
    auto pos = _manifest.sprites.find(ascii::toLower(name));
    return pos != _manifest.sprites.end() && std::ranges::contains(pos->second.palettes, paletteId);
}

RgbaImage TexturePack::spriteImage(std::string_view name, int paletteId, const Blob &lodEntry, Sizei lodSize) const {
    if (!hasSpriteImage(name, paletteId) || !lodEntry)
        return {};

    std::string lowercaseName = ascii::toLower(name);
    std::string path = fmt::format("sprites/{}/{}.png", lowercaseName, paletteId);
    return loadImage(_fs, path, _manifest.sprites.find(lowercaseName)->second.crc32, lodEntry, lodSize);
}
