#pragma once

#include <map>
#include <set>
#include <string>

/**
 * Collects which palettes each `sprites.lod` image is drawn with, as the sprite frame table says.
 *
 * Only frames that the game data references are visited: the sprites of every monster, every decoration in every
 * season, and every object. The frame table also lists frames that nothing uses, and some of those point at images
 * that `sprites.lod` doesn't have.
 *
 * @return                              Map from lowercase `sprites.lod` entry name to the palette ids it's drawn with.
 */
std::map<std::string, std::set<int>> collectSpritePalettes();
