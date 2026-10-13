#pragma once

#include <glad/gl.h> // NOLINT: this is not a C system include.

#include "Library/Geometry/Size.h"

#include "RendererEnums.h"

/**
 * OpenGL framebuffer with a colour texture and an optional depth texture.
 */
class OpenGLRenderTarget {
 public:
    OpenGLRenderTarget() = default;

    ~OpenGLRenderTarget() {
        reset();
    }

    OpenGLRenderTarget(const OpenGLRenderTarget &) = delete;
    OpenGLRenderTarget &operator=(const OpenGLRenderTarget &) = delete;

    /**
     * Creates the target, releasing whatever it held before.
     *
     * @param size                      Size in pixels.
     * @param format                    Format of the colour texture.
     * @param withDepth                 Whether to add a 24-bit depth texture.
     */
    void reset(Sizei size, RenderTargetFormat format, bool withDepth);

    /**
     * Wraps a colour texture owned by someone else, releasing whatever the target held before. The texture is not
     * deleted when the target is released.
     *
     * @param colorTexture              Texture to draw into.
     * @param size                      Size of the texture.
     * @param withDepth                 Whether to add a 24-bit depth texture.
     */
    void resetExternal(GLuint colorTexture, Sizei size, bool withDepth);

    /**
     * Releases the OpenGL objects.
     */
    void reset();

    /**
     * Binds the target as the draw framebuffer.
     */
    void bind();

    [[nodiscard]] GLuint framebuffer() const {
        return _framebuffer;
    }

    [[nodiscard]] GLuint colorTexture() const {
        return _colorTexture;
    }

    [[nodiscard]] bool hasDepth() const {
        return _depthTexture != 0;
    }

    [[nodiscard]] GLuint depthTexture() const {
        return _depthTexture;
    }

    [[nodiscard]] Sizei size() const {
        return _size;
    }

    [[nodiscard]] explicit operator bool() const {
        return _framebuffer != 0;
    }

 private:
    void createDepthTexture();

 private:
    GLuint _framebuffer = 0;
    GLuint _colorTexture = 0;
    GLuint _depthTexture = 0;
    bool _ownsColorTexture = true;
    Sizei _size;
};
