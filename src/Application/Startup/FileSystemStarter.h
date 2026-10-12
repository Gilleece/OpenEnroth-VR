#pragma once

#include <memory>

#include "Utility/System/Path.h"

class FileSystem;

class FileSystemStarter {
 public:
    FileSystemStarter();
    ~FileSystemStarter();

    void initUserFs(bool ramFs, const Path &path);
    void initDataFs(const Path &path, bool pathOverridesBuiltIn);
    void initTexturePackFs(const Path &path);

 private:
    std::unique_ptr<FileSystem> _userFs;
    std::unique_ptr<FileSystem> _dataEmbeddedFs;
    std::unique_ptr<FileSystem> _dataDirFs;
    std::unique_ptr<FileSystem> _dataDirLowercaseFs;
    std::unique_ptr<FileSystem> _dataFs;
    std::unique_ptr<FileSystem> _texturePackDirFs;
    std::unique_ptr<FileSystem> _texturePackLowercaseFs;
};
