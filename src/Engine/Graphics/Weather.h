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
     * Draws the snowflakes over the game viewport, when it's snowing.
     */
    void drawSnow();

    bool OnPlayerTurn(int dangle);

    std::array<Pointi, 1000> Screen_Coord;
    bool bNight = false;
    bool bRenderSnow = false;
};

extern Weather *pWeather;
