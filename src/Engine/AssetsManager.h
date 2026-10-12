#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include "GUI/GUIFont.h"

class GraphicsImage;

class AssetsManager {
 public:
    AssetsManager();
    ~AssetsManager();

    void releaseAllTextures();

    /**
     * Removes an image from the cache and frees it.
     *
     * @param image                     Image returned by one of the getters below.
     */
    void releaseImage(GraphicsImage *image);

    GraphicsImage *getIcon(std::string_view name);
    GraphicsImage *getImage_Buff(std::string_view name);

    GraphicsImage *getBitmap(std::string_view name, bool generated = false);
    GraphicsImage *getSprite(std::string_view name);

    /**
     * @param sprite                    Image to draw with a palette. Only images returned by `getSprite()` have
     *                                  variants.
     * @param paletteId                 Palette the image is drawn with.
     * @return                          The texture pack's true colour image of the sprite drawn with this palette, to
     *                                  be drawn without a palette, or `nullptr` if there is none.
     */
    GraphicsImage *getSpriteVariant(GraphicsImage *sprite, int paletteId);

    std::unique_ptr<GUIFont> pFontBookOnlyShadow;
    std::unique_ptr<GUIFont> pFontBookLloyds;
    std::unique_ptr<GUIFont> pFontArrus;
    std::unique_ptr<GUIFont> pFontLucida;
    std::unique_ptr<GUIFont> pFontBookTitle;
    std::unique_ptr<GUIFont> pFontBookCalendar;
    std::unique_ptr<GUIFont> pFontCreate;
    std::unique_ptr<GUIFont> pFontCChar;
    std::unique_ptr<GUIFont> pFontComic;
    std::unique_ptr<GUIFont> pFontSmallnum;

 protected:
    std::unordered_map<std::string, std::unique_ptr<GraphicsImage>> bitmaps;
    std::unordered_map<std::string, std::unique_ptr<GraphicsImage>> sprites;
    std::unordered_map<std::string, std::unique_ptr<GraphicsImage>> images;
    std::unordered_map<std::string, std::unordered_map<int, std::unique_ptr<GraphicsImage>>> spriteVariants; // By sprite name, then palette id.
};

extern AssetsManager *assets;
