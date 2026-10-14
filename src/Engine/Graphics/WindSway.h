#pragma once

#include "Engine/Data/DecorationEnums.h"

#include "Library/Geometry/Vec.h"

constexpr Vec2f WIND_DIRECTION = Vec2f(0.8f, 0.6f); // Level direction the wind blows in.
constexpr float WIND_PERIOD = 8.0f; // Seconds after which the sway repeats.

/**
 * @param id                            Decoration type.
 * @return                              Part of the sprite's height by which its top sways in the wind at most, 0 for
 *                                      decorations that don't sway.
 */
float decorationSwayRatio(DecorationId id);

/**
 * @param pos                           Position of a swaying sprite.
 * @return                              Phase of its sway, so that gusts run across the level along the wind.
 */
float windSwayPhase(Vec3f pos);

/**
 * The sway that `glbillbshader.vert` applies, for the sprites that the CPU places.
 *
 * @param seconds                       Time, wrapped to `WIND_PERIOD`.
 * @param phase                         Phase of the sprite, see `windSwayPhase`.
 * @return                              Sway along the wind, from -1 to 1.
 */
float windSway(float seconds, float phase);
