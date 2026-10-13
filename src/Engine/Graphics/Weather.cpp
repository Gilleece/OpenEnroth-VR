#include <cmath>
#include <numbers>

#include "Engine/Graphics/Renderer/Renderer.h"

#include "Engine/Engine.h"
#include "Engine/Party.h"

#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/Viewport.h"
#include "Engine/Graphics/Weather.h"
#include "Engine/Random/Random.h"

Weather *pWeather = new Weather;

static constexpr float WORLD_SNOW_ARC = std::numbers::pi_v<float>; // Yaw span of the game viewport's width in world space snow.

/**
 * @param index                         Index of a snowflake.
 * @return                              Size of the snowflake in game viewport pixels.
 */
static int snowflakeSize(unsigned int index) {
    if (index >= 950)
        return 4;
    if (index >= 700)
        return 2;
    return 1;
}

void Weather::Initialize() {
    int width = pViewport.w - 1;
    int height = pViewport.h - 1;
    for (Pointi &point : Screen_Coord) {
        point.x = pViewport.x + vrng->random(width);
        point.y = pViewport.y + vrng->random(height);
    }
}

void Weather::updateSnow() {
    if (!bRenderSnow || !engine->config->graphics.Snow.value())
        return;

    for (unsigned int i = 0; i < Screen_Coord.size(); ++i) {
        int size = 1;
        int base = 3;
        if (i >= 950) {
            size = 4;
            base = 10;
        } else if (i >= 700) {
            size = 2;
            base = 5;
        }

        Screen_Coord[i].x += vrng->random(base) - base / 2;
        Screen_Coord[i].y += vrng->random(size) + size;
        if (Screen_Coord[i].x < pViewport.x) {
            Screen_Coord[i].x = pViewport.x + vrng->random(base);
        } else if (Screen_Coord[i].x >= (pViewport.x + pViewport.w - 1 - size)) {
            Screen_Coord[i].x = pViewport.x + pViewport.w - 1 - vrng->random(base);
        }
        if (Screen_Coord[i].y >= (pViewport.y + pViewport.h - 1 - size)) {
            Screen_Coord[i].y = pViewport.y;
            Screen_Coord[i].x = pViewport.x + vrng->random(pViewport.w - 1 - size);
        }
    }
}

void Weather::drawSnow() {
    if (!bRenderSnow || !engine->config->graphics.Snow.value() || render->uiLayer())
        return;

    for (unsigned int i = 0; i < Screen_Coord.size(); ++i) {
        int size = 1;
        if (i >= 950) {
            size = 4;
        } else if (i >= 700) {
            size = 2;
        }

        render->FillRect(Recti(Screen_Coord[i].x, Screen_Coord[i].y, size, size), colorTable.White);
    }
}

void Weather::addWorldSnow() {
    if (!bRenderSnow || !engine->config->graphics.Snow.value())
        return;

    Vec3f eye(pParty->pos.x, pParty->pos.y, pParty->pos.z + pParty->eyeLevel);
    float yaw = pParty->_viewYaw * std::numbers::pi_v<float> / 1024;
    float arcPerPixel = WORLD_SNOW_ARC / pViewport.w;
    float sizePerPixel = pCamera3D->odm_fov_rad / pViewport.w; // Keeps the flakes as big as they look on the flat screen.
    Pointi centre = pViewport.center();

    for (unsigned int i = 0; i < Screen_Coord.size(); ++i) {
        int size = snowflakeSize(i);
        float distance = 512.0f / size; // Bigger flakes are nearer, for parallax.
        float azimuth = yaw - (Screen_Coord[i].x - centre.x) * arcPerPixel;
        float elevation = (centre.y - Screen_Coord[i].y) * arcPerPixel;

        WorldBillboard flake;
        flake.opacity = RenderBillboardD3D::Opaque_1;
        flake.pos = eye + Vec3f(std::cos(elevation) * std::cos(azimuth), std::cos(elevation) * std::sin(azimuth), std::sin(elevation)) * distance;
        float half = size * distance * sizePerPixel / 2;
        flake.facing = {{{-half, -half}, {-half, half}, {half, half}, {half, -half}}};
        flake.texcoords = {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
        flake.colors.fill(colorTable.White);
        render->addWorldBillboard(flake);
    }
}

bool Weather::OnPlayerTurn(int dangle) {
    if (!bRenderSnow) {
        return false;
    }

    unsigned int screen_width = pViewport.w - 1;

    for (Pointi &point : Screen_Coord) {
        point.x += dangle;
        if (point.x < pViewport.x + pViewport.w - 1) {
            if (point.x >= pViewport.x) {
                continue;
            }
            point.x += screen_width;
        } else {
            point.x -= screen_width;
        }
    }

    return true;
}
