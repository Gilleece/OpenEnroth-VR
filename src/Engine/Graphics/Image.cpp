#include "Engine/Graphics/Image.h"

#include <bit>
#include <cassert>
#include <utility>
#include <memory>
#include <string>

#include "Engine/Graphics/ImageLoader.h"
#include "Engine/Graphics/Renderer/Renderer.h"

static bool isPowerOfTwoMultiple(Sizei scaled, Sizei size) {
    if (size.w == 0 || scaled.w % size.w != 0)
        return false;
    int scale = scaled.w / size.w;
    return std::has_single_bit(static_cast<unsigned>(scale)) && scaled.h == size.h * scale;
}

GraphicsImage::GraphicsImage() = default;

GraphicsImage::~GraphicsImage() {
    if (render) // GPU textures go away with the GL context once the renderer is shut down.
        releaseRenderId();
}

std::unique_ptr<GraphicsImage> GraphicsImage::Create(RgbaImage image) {
    std::unique_ptr<GraphicsImage> result(new GraphicsImage());
    result->_size = image.size();
    result->_rgba = std::move(image);
    result->_textureRgba = RgbaImage();
    result->_materialRgba = RgbaImage();
    result->_renderId = render->CreateTexture(*result->_rgba, TEXTURE_FILTER_LINEAR);
    return result;
}

std::unique_ptr<GraphicsImage> GraphicsImage::Create(int width, int height) {
    assert(width > 0 && height > 0);
    return Create(RgbaImage::solid(Color(), width, height));
}

std::unique_ptr<GraphicsImage> GraphicsImage::Create(Sizei size) {
    return Create(size.w, size.h);
}

std::unique_ptr<GraphicsImage> GraphicsImage::Create(std::unique_ptr<ImageLoader> loader) {
    std::unique_ptr<GraphicsImage> result(new GraphicsImage());
    result->_name = loader->name();
    result->_loader = std::move(loader);
    return result;
}

int GraphicsImage::width() {
    return size().w;
}

int GraphicsImage::height() {
    return size().h;
}

Sizei GraphicsImage::size() {
    if (!_size)
        _size = _loader->loadSize();
    return *_size;
}

RgbaImage &GraphicsImage::rgba() {
    if (!_rgba) {
        _rgba = _loader->loadRgba();
        assert(!_size || _rgba->size() == *_size);
        _size = _rgba->size();
    }
    return *_rgba;
}

RgbaImageView GraphicsImage::textureRgba() {
    if (!_textureRgba) {
        _textureRgba = _loader->loadTextureRgba();
        assert(!*_textureRgba || isPowerOfTwoMultiple(_textureRgba->size(), size()));
    }
    if (*_textureRgba)
        return *_textureRgba;
    return rgba();
}

int GraphicsImage::textureScale() {
    RgbaImageView texture = textureRgba();
    return texture.width() == width() ? 1 : texture.width() / width();
}

RgbaImageView GraphicsImage::materialRgba() {
    if (!_materialRgba) {
        Sizei textureSize = textureRgba().size();
        _materialRgba = _loader->loadMaterialRgba(textureSize);
        assert(!*_materialRgba || _materialRgba->size() == textureSize);
    }
    return *_materialRgba;
}

const std::string &GraphicsImage::name() {
    return _name;
}

[[nodiscard]] TextureRenderId GraphicsImage::renderId() {
    if (!_renderId)
        _renderId = render->CreateTexture(textureRgba(), textureScale() > 1 ? TEXTURE_FILTER_MIPMAPPED : TEXTURE_FILTER_LINEAR);

    return _renderId;
}

void GraphicsImage::releaseRenderId() {
    if (!_renderId)
        return;

    render->DeleteTexture(_renderId);
    _renderId = TextureRenderId();
}
