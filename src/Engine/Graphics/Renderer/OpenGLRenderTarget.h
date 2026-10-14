#pragma once

#include <glad/gl.h> // NOLINT: this is not a C system include.

#include "Library/Geometry/Size.h"

#include "RendererEnums.h"

/**
 * OpenGL framebuffer with a colour texture and an optional depth texture, or a multisampled one with a colour and a
 * depth renderbuffer.
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
     * Creates a multisampled target with a colour and a 24-bit depth renderbuffer, releasing whatever the target held
     * before. It can't be sampled, `resolveInto` copies it into a target that can.
     *
     * @param size                      Size in pixels.
     * @param format                    Format of the colour renderbuffer.
     * @param samples                   Samples per pixel.
     */
    void resetMultisample(Sizei size, RenderTargetFormat format, int samples);

    /**
     * Resolves a multisampled target into a single sampled one of the same size and format, leaving the draw
     * framebuffer unbound. Depth resolves to one of each pixel's samples.
     *
     * @param target                    Target to resolve into.
     * @param buffers                   `GL_COLOR_BUFFER_BIT`, `GL_DEPTH_BUFFER_BIT` or both.
     */
    void resolveInto(OpenGLRenderTarget &target, GLbitfield buffers = GL_COLOR_BUFFER_BIT) const;

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
        return _depthTexture != 0 || _depthRenderbuffer != 0;
    }

    [[nodiscard]] RenderTargetFormat format() const {
        return _format;
    }

    /**
     * @return                          Samples per pixel of a multisampled target, 0 for one that isn't.
     */
    [[nodiscard]] int samples() const {
        return _samples;
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
    GLuint _colorRenderbuffer = 0;
    GLuint _depthRenderbuffer = 0;
    bool _ownsColorTexture = true;
    Sizei _size;
    RenderTargetFormat _format = RENDER_TARGET_FORMAT_RGBA8;
    int _samples = 0;
};
