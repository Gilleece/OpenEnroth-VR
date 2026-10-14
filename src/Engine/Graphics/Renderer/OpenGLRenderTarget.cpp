#include "OpenGLRenderTarget.h"

#include <cassert>
#include <string>

#include "Library/Logger/Logger.h"
#include "Library/Serialization/EnumSerialization.h"

namespace detail_fb_error {
MM_DEFINE_ENUM_SERIALIZATION_FUNCTIONS(GLenum, CASE_SENSITIVE, {
    {GL_FRAMEBUFFER_UNDEFINED, "framebuffer is undefined"},
    {GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT, "framebuffer has missing attachment"},
    {GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT, "framebuffer has incomplete attachment"},
    {GL_FRAMEBUFFER_UNSUPPORTED, "framebuffer is unsupported"},
    {GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE, "framebuffer has incomplete multisample"},
    {0, "unknown error"}
})
} // namespace detail_fb_error

static void GL_Check_Framebuffer(const char *name) {
    std::string error;

    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (!detail_fb_error::trySerialize(status, &error))
        return;

    MM_WARNING("OpenGL Framebuffer error ({}): {} from function {}", status, error, name);
}

static GLenum internalFormat(RenderTargetFormat format) {
    switch (format) {
    case RENDER_TARGET_FORMAT_RGBA16F: return GL_RGBA16F;
    case RENDER_TARGET_FORMAT_R11G11B10F: return GL_R11F_G11F_B10F;
    case RENDER_TARGET_FORMAT_RG16F: return GL_RG16F;
    case RENDER_TARGET_FORMAT_R16F: return GL_R16F;
    default: return GL_RGBA8;
    }
}

void OpenGLRenderTarget::reset(Sizei size, RenderTargetFormat format, bool withDepth) {
    reset();

    _size = size;
    _format = format;
    glGenFramebuffers(1, &_framebuffer);

    _ownsColorTexture = true;
    glGenTextures(1, &_colorTexture);
    glBindTexture(GL_TEXTURE_2D, _colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    if (format == RENDER_TARGET_FORMAT_RGBA16F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.w, size.h, 0, GL_RGBA, GL_FLOAT, NULL);
    } else if (format == RENDER_TARGET_FORMAT_R11G11B10F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, size.w, size.h, 0, GL_RGB, GL_FLOAT, NULL);
    } else if (format == RENDER_TARGET_FORMAT_RG16F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size.w, size.h, 0, GL_RG, GL_FLOAT, NULL);
    } else if (format == RENDER_TARGET_FORMAT_R32F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size.w, size.h, 0, GL_RED, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // GL ES can't filter 32 bit floats.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else if (format == RENDER_TARGET_FORMAT_R16F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, size.w, size.h, 0, GL_RED, GL_FLOAT, NULL);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.w, size.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (withDepth)
        createDepthTexture();
}

void OpenGLRenderTarget::resetExternal(GLuint colorTexture, Sizei size, bool withDepth) {
    reset();

    _size = size;
    glGenFramebuffers(1, &_framebuffer);

    _ownsColorTexture = false;
    _colorTexture = colorTexture;

    if (withDepth)
        createDepthTexture();
}

void OpenGLRenderTarget::resetMultisample(Sizei size, RenderTargetFormat format, int samples) {
    reset();

    _size = size;
    _format = format;
    _samples = samples;
    glGenRenderbuffers(1, &_colorRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internalFormat(format), size.w, size.h);
    glGenRenderbuffers(1, &_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _depthRenderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, size.w, size.h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffer);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRenderbuffer);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthRenderbuffer);
    GL_Check_Framebuffer(__FUNCTION__);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous);
}

void OpenGLRenderTarget::resolveInto(OpenGLRenderTarget &target, GLbitfield buffers) const {
    assert(_samples > 0 && target._samples == 0 && target._size == _size && target._format == _format);

    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    target.bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer);
    glBlitFramebuffer(0, 0, _size.w, _size.h, 0, 0, _size.w, _size.h, buffers, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

void OpenGLRenderTarget::reset() {
    if (_framebuffer) {
        glDeleteFramebuffers(1, &_framebuffer);
        _framebuffer = 0;
    }
    for (GLuint *renderbuffer : {&_colorRenderbuffer, &_depthRenderbuffer}) {
        if (*renderbuffer) {
            glDeleteRenderbuffers(1, renderbuffer);
            *renderbuffer = 0;
        }
    }
    _samples = 0;
    if (_colorTexture) {
        if (_ownsColorTexture)
            glDeleteTextures(1, &_colorTexture);
        _colorTexture = 0;
    }
    if (_depthTexture) {
        glDeleteTextures(1, &_depthTexture);
        _depthTexture = 0;
    }
    _size = Sizei();
}

void OpenGLRenderTarget::createDepthTexture() {
    glGenTextures(1, &_depthTexture);
    glBindTexture(GL_TEXTURE_2D, _depthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, _size.w, _size.h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderTarget::bind() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffer);
    if (_samples > 0)
        return; // The renderbuffers are attached once, when they're made.
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorTexture, 0);
    if (_depthTexture)
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _depthTexture, 0);

    GL_Check_Framebuffer(__FUNCTION__);
}
