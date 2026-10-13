#pragma once

#include "Library/FileSystem/Interface/FileSystem.h"

#include "Utility/System/Path.h"

extern FileSystem *dfs;
extern FileSystem *ufs;
extern FileSystem *tfs; // Texture pack, null if there is none.
extern Path dataDirPath; // Native folder of the game data behind `dfs`.
extern Path texturePackDirPath; // Native folder behind `tfs`, empty if there is none.
