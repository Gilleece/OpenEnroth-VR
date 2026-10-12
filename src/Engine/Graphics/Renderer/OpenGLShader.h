#pragma once

#include <span>
#include <string>
#include <string_view>

#include "Utility/Memory/Blob.h"

class FileSystem;

/**
 * Class for loading in and building gl shaders.
 *
 * Code derived from:
 * https://learnopengl.com/code_viewer_gh.php?code=includes/learnopengl/shader.h
 */
class OpenGLShader {
 public:
    OpenGLShader() = default;
    ~OpenGLShader();

    [[nodiscard]] bool isValid() const {
       return _id != 0;
    }

    /**
     * @param vertSource                Vertex shader source.
     * @param fragSource                Fragment shader source.
     * @param openGLES                  Whether to compile for OpenGL ES.
     * @param pwd                       File system that `#include` directives are resolved against.
     * @param defines                   Macros to define for both shaders, to build a variant of the same sources.
     * @return                          Whether the program compiled and linked.
     */
    [[nodiscard]] bool load(const Blob &vertSource, const Blob &fragSource, bool openGLES, const FileSystem *pwd,
                            std::span<const std::string_view> defines = {});
    void release();

    [[nodiscard]] int uniformLocation(const char *name) const;
    // No attribLocation() - use vertex buffers.

    void use();
    void unuse();

 private:
    [[nodiscard]] unsigned loadShader(const Blob &source, int type, bool openGLES, const FileSystem *pwd,
                                      std::span<const std::string_view> defines);

 private:
    unsigned _id = 0;
    std::string _paths;
};
