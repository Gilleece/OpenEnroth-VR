#include "OpenGLRenderTarget.h"

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

void OpenGLRenderTarget::reset(Sizei size, RenderTargetFormat format, bool withDepth) {
    reset();

    _size = size;
    glGenFramebuffers(1, &_framebuffer);

    glGenTextures(1, &_colorTexture);
    glBindTexture(GL_TEXTURE_2D, _colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    if (format == RENDER_TARGET_FORMAT_RGBA16F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.w, size.h, 0, GL_RGBA, GL_FLOAT, NULL);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.w, size.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (withDepth) {
        glGenTextures(1, &_depthTexture);
        glBindTexture(GL_TEXTURE_2D, _depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.w, size.h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void OpenGLRenderTarget::reset() {
    if (_framebuffer) {
        glDeleteFramebuffers(1, &_framebuffer);
        _framebuffer = 0;
    }
    if (_colorTexture) {
        glDeleteTextures(1, &_colorTexture);
        _colorTexture = 0;
    }
    if (_depthTexture) {
        glDeleteTextures(1, &_depthTexture);
        _depthTexture = 0;
    }
    _size = Sizei();
}

void OpenGLRenderTarget::bind() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorTexture, 0);
    if (_depthTexture)
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _depthTexture, 0);

    GL_Check_Framebuffer(__FUNCTION__);
}
