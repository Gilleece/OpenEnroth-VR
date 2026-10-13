#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

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

/**
 * Groups the `sprites.lod` images that the referenced frame sequences draw, an animation's frames in all directions
 * together. Sequences that share an image end up in one group, so a monster's animations usually make one group.
 *
 * @return                              Groups of lowercase `sprites.lod` entry names.
 */
std::vector<std::set<std::string>> collectSpriteGroups();
