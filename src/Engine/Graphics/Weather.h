#pragma once

#include <array>

#include "Library/Geometry/Point.h"

class Weather {
 public:
    void Initialize();

    /**
     * Moves the snowflakes, when it's snowing.
     */
    void updateSnow();

    /**
     * Draws the snowflakes over the game viewport, when it's snowing and the UI isn't a layer of its own.
     */
    void drawSnow();

    /**
     * Adds the snowflakes to the billboard list as world space billboards, when it's snowing. They hang around the
     * party over half a turn of yaw centred on where the party looks, so that a view turned away from the party camera,
     * like a VR headset's, still sees them fall around it.
     */
    void addWorldSnow();

    bool OnPlayerTurn(int dangle);

    std::array<Pointi, 1000> Screen_Coord;
    bool bNight = false;
    bool bRenderSnow = false;
};

extern Weather *pWeather;
