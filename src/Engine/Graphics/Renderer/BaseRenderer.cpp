#include "BaseRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Engine/Engine.h"
#include "Engine/SpellFxRenderer.h"
#include "Engine/Party.h"

#include "Engine/Objects/Actor.h"
#include "Engine/Objects/SpriteObject.h"

#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/CameraView.h"
#include "Engine/Graphics/DecalBuilder.h"
#include "Engine/Graphics/Lighting.h"
#include "Engine/Graphics/LightsStack.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/BspRenderer.h"
#include "Engine/Graphics/Sprites.h"
#include "Engine/Graphics/Viewport.h"
#include "Engine/Graphics/Vis.h"
#include "Engine/Graphics/ParticleEngine.h"
#include "Engine/Objects/Decoration.h"
#include "Engine/Tables/DecorationTable.h"
#include "Engine/Graphics/Image.h"
#include "Engine/EngineGlobals.h"

#include "Library/Logger/Logger.h"

#include "Utility/Math/TrigLut.h"
#include "Utility/Memory/MemSet.h"

void BaseRenderer::Initialize() {
    updateRenderDimensions();
}

unsigned int BaseRenderer::NextBillboardIndex() {
    if (uNumBillboardsToDraw >= MAX_BILLBOARDS_D3D) {
        return 0;
    }

    int index = uNumBillboardsToDraw++;
    pSortedBillboardRenderListD3D[index] = &pBillboardRenderListD3D[index];

    return index;
}

void BaseRenderer::resetBillboardRenderList() {
    uNumBillboardsToDraw = 0;
    _worldBillboardQuadsBuilt = false;
}

/**
 * @param view                          View to face.
 * @return                              Right and up axes for camera facing quads, those of `view` with its roll taken
 *                                      out.
 */
static std::pair<Vec3f, Vec3f> billboardAxes(const CameraView &view) {
    // Rows of the view matrix are the view's right, up and backward axes in world space.
    const glm::mat4 &matrix = view.viewMatrix;
    glm::vec3 viewRight(matrix[0][0], matrix[1][0], matrix[2][0]);
    glm::vec3 viewBack(matrix[0][2], matrix[1][2], matrix[2][2]);

    glm::vec3 right = glm::vec3(viewRight.x, viewRight.y, 0);
    right = glm::length(right) > 0.001f ? glm::normalize(right) : viewRight; // Rolled on its side, keep the roll.
    glm::vec3 up = glm::normalize(glm::cross(viewBack, right));
    return {Vec3f(right.x, right.y, right.z), Vec3f(up.x, up.y, up.z)};
}

void BaseRenderer::buildWorldBillboardQuads(const CameraView &view) {
    auto [right, up] = billboardAxes(view);
    for (unsigned i = 0; i < uNumBillboardsToDraw; i++) {
        WorldBillboard &billboard = _worldBillboards[i];
        for (int j = 0; j < billboard.numVertices; j++)
            billboard.corners[j] = billboard.pos + billboard.offsets[j] + right * billboard.facing[j].x + up * billboard.facing[j].y;
        billboard.distance = (billboard.pos - view.position).length();
    }
    _worldBillboardEye = view.position;
    _worldBillboardQuadsBuilt = true;
}

void BaseRenderer::sortBillboardRenderList() {
    // we need to loop over all billboards from farthest to nearest
    std::stable_sort(pSortedBillboardRenderListD3D.begin(), pSortedBillboardRenderListD3D.begin() + uNumBillboardsToDraw,
        [](const auto& a, const auto& b) {
            return a->view_space_z < b->view_space_z;
        });
}


void BaseRenderer::buildBloodsplatDecals() {
    if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
        buildIndoorBloodsplatDecals();
    } else {
        buildTerrainBloodsplatDecals();
        buildBuildingBloodsplatDecals();
    }
}

// TODO: Move this to sprites ?
// combined with IndoorLocation::PrepareItemsRenderList_BLV() (0044028F)
void BaseRenderer::DrawSpriteObjects() {
    for (unsigned int i = 0; i < pSpriteObjects.size(); ++i) {
        // exit if we are at max sprites
        if (::uNumBillboardsToDraw >= 500) {
            MM_WARNING("Billboards Full");
            break;
        }

        SpriteObject *object = &pSpriteObjects[i];
        if (!object->uObjectDescID) {  // item probably pciked up - this also gets wiped at end of sprite anims/ particle effects
            continue;
        }
        if (!object->HasSprite()) {
            continue;
        }

        // view culling
        if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
            bool onlist = false;
            for (unsigned j = 0; j < pBspRenderer->uNumVisibleNotEmptySectors; j++) {
                if (pBspRenderer->pVisibleSectorIDs_toDrawDecorsActorsEtcFrom[j] == object->uSectorID) {
                    onlist = true;
                    break;
                }
            }
            if (!onlist) continue;
        } else {
            if (!IsCylinderInFrustum(object->vPosition, 512.0f)) continue;
        }

        // render as sprte 500 - 9081
        if (spell_fx_renderer->RenderAsSprite(object) ||
            ((object->spriteId < SPRITE_SPELL_FIRE_TORCH_LIGHT || object->spriteId >= SPRITE_10000) && // Not a spell sprite.
             (object->spriteId < SPRITE_PROJECTILE_AIR_BOLT || object->spriteId >= SPRITE_OBJECT_EXPLODE) && // Not a projectile.
             (object->spriteId < SPRITE_TRAP_FIRE || object->spriteId > SPRITE_TRAP_BODY))) { // Not a trap.
            SpriteFrame *frame = object->spriteFrame();
            if (frame->spriteName == "null" || frame->textureName == "null") {
                MM_TRACE("Trying to draw sprite with null frame");
                continue;
            }

            // sprite angle to camera
            unsigned int angle = TrigLUT.atan2(object->vPosition.x - pCamera3D->vCameraPos.x, object->vPosition.y - pCamera3D->vCameraPos.y);
            int octant = ((TrigLUT.uIntegerPi + (TrigLUT.uIntegerPi >> 3) + object->uFacing - angle) >> 8) & 7;

            // error catching
            if (frame->sprites[octant]->texture->height() == 0 || frame->sprites[octant]->texture->width() == 0) {
                MM_TRACE("Trying to draw sprite with empty octant texture");
                continue;
            }

            Vec3f posMod = object->vPosition;
            // centre sprite
            if (frame->flags & SPRITE_FRAME_CENTER) {
                posMod.z -= (frame->scale * frame->sprites[octant]->uHeight) / 2;
            }

            BillboardFlags setflags = billboardFlagsForSprite(frame->flags, octant);

            // lighting
            int lightradius = frame->glowRadius * object->field_22_glow_radius_multiplier;

            Color color = pSpriteObjects[i].GetParticleTrailColor();
            if (color.r == 0) color.r = 0xFF;
            if (color.g == 0) color.g = 0xFF;
            if (color.b == 0) color.b = 0xFF;
            if (lightradius) {
                pMobileLightsStack->AddLight(object->vPosition,
                                             object->uSectorID, lightradius, color, _4E94D3_light_type);
            }

            if (render->AddBillboardIfVisible(frame->sprites[octant], frame->paletteId, posMod, { frame->scale, frame->scale }, setflags, Pid(OBJECT_Sprite, i), object->uSectorID)) {
                ++uNumSpritesDrawnThisFrame;
                object->uAttributes |= SPRITE_VISIBLE;
            }
        }
    }
}

// TODO(pskelton): Move to outdoors - clean up
void BaseRenderer::PrepareDecorationsRenderList_ODM() {
    Duration v6;        // edi@9
    int v7;                 // eax@9
    SpriteFrame *frame;     // eax@9
    int v13;                // ecx@9
    Color color;
    BillboardFlags v38;                // [sp+88h] [bp-1Ch]@9

    for (unsigned int i = 0; i < pLevelDecorations.size(); ++i) {
        if (::uNumBillboardsToDraw >= 500) {
            MM_WARNING("Billboards Full");
            return;
        }

        // view cull
        if (!IsCylinderInFrustum(pLevelDecorations[i].vPosition, 512.0f)) continue;

        // LevelDecoration *decor = &pLevelDecorations[i];
        if ((!(pLevelDecorations[i].uFlags & LEVEL_DECORATION_OBELISK_CHEST) ||
            pLevelDecorations[i].IsObeliskChestActive()) &&
            !(pLevelDecorations[i].uFlags & LEVEL_DECORATION_INVISIBLE)) {
            const DecorationData *decor_desc = pDecorationTable->decoration(pLevelDecorations[i].uDecorationDescID);
            if (!(decor_desc->uFlags & DECORATION_DATA_EMITS_FIRE)) {
                if (!(decor_desc->uFlags & (DECORATION_DATA_MARKER | DECORATION_DATA_DONT_DRAW))) {
                    v6 = animTimer->time();
                    v7 = std::abs(pLevelDecorations[i].vPosition.x +
                        pLevelDecorations[i].vPosition.y);

                    frame = pSpriteFrameTable->GetFrame(decor_desc->uSpriteID,
                        v6 + Duration::fromTicks(v7));

                    if (config->graphics.SeasonsChange.value()) {
                        frame = LevelDecorationChangeSeason(decor_desc, v6 + Duration::fromTicks(v7), pParty->uCurrentMonth);
                    }

                    if (!frame || frame->textureName == "null" || frame->sprites[0] == NULL) {
                        continue;
                    }

                    // v8 = pSpriteFrameTable->GetFrame(decor_desc->uSpriteID,
                    // v6 + v7);

                    int v10 = TrigLUT.atan2(pLevelDecorations[i].vPosition.x - pCamera3D->vCameraPos.x,
                                            pLevelDecorations[i].vPosition.y - pCamera3D->vCameraPos.y);
                    v13 = ((signed int)(TrigLUT.uIntegerPi +
                        ((signed int)TrigLUT.uIntegerPi >>
                            3) +
                        pLevelDecorations[i]._yawAngle -
                        (int64_t)v10) >>
                        8) &
                        7;
                    int v37 = v13;


                    if (engine->config->graphics.AddMoreLights.value()) {
                        // This is an attempt to make the game look more nice.
                        // In MM7 data some sprites like a "cauldron" do not emit light.
                        // In MM8 pedestals and campfires have large radius and bright color (too bright actually).
                        // The following changes makes OpenEnroth look more like MM8.
                        // OpenEnroth do not support mods and data patches right now. TODO(pskelton): data patch
                        // So the changes are in C++ code. But it better be placed in lua scripts or binary data patches.
                        static const std::unordered_set<DecorationId> lightEmittingDecorations = {
                            DECORATION_CAMPFIRE_5, DECORATION_CAULDRON_6, DECORATION_BARREL_27, DECORATION_BURNED_OUT_FIRE_222, DECORATION_FIRE_184, DECORATION_MUSHROOM_187,
                            DECORATION_MUSHROOM_190, DECORATION_BEACON_FIRE_206, DECORATION_BEACON_FIRE_207, DECORATION_BEACON_FIRE_208, DECORATION_BEACON_FIRE_209,
                            DECORATION_MAGIC_PEDASTAL_210, DECORATION_MAGIC_PEDASTAL_211, DECORATION_MAGIC_PEDASTAL_212, DECORATION_MAGIC_PEDASTAL_213, DECORATION_MAGIC_PEDASTAL_214,
                            DECORATION_MAGIC_PEDASTAL_215, DECORATION_MAGIC_PEDASTAL_216, DECORATION_MAGIC_PEDASTAL_217, DECORATION_MAGIC_PEDASTAL_218, DECORATION_MAGIC_PEDASTAL_219,
                            DECORATION_MAGIC_PEDASTAL_220, DECORATION_MAGIC_PEDASTAL_221
                        };

                        if (lightEmittingDecorations.contains(pLevelDecorations[i].uDecorationDescID)) {
                             frame->glowRadius = engine->config->graphics.DefaultLightRadius.value();
                        }
                    }

                    Color defaultLightColor = engine->config->graphics.DefaultLightColor.value();
                    if (frame->glowRadius) {
                        color = defaultLightColor;
                        if (render->config->graphics.ColoredLights.value()) {
                            color = decor_desc->uColoredLight;
                            // to avoid black lights
                            if (color == Color(0, 0, 0)) {
                                color = defaultLightColor;
                            }
                        }
                        pStationaryLightsStack->AddLight(pLevelDecorations[i].vPosition + Vec3f(0, 0, decor_desc->uDecorationHeight / 2),
                            frame->glowRadius, color, _4E94D0_light_type);
                    }


                    if (render->AddBillboardIfVisible(frame->sprites[(int64_t)v37], frame->paletteId, pLevelDecorations[i].vPosition, { frame->scale, frame->scale },
                        v38, Pid(OBJECT_Decoration, i))) {
                        ++uNumDecorationsDrawnThisFrame;
                    }
                }
            } else {
                spell_fx_renderer->addFireParticle(pLevelDecorations[i].vPosition);
            }
        }
    }
}

void BaseRenderer::TransformBillboards() {
    pODMRenderParams->uNumBillboards = ::uNumBillboardsToDraw;

    for (unsigned int i = 0; i < ::uNumBillboardsToDraw; ++i) {
        RenderBillboard *p = &pBillboardRenderList[i];
        if (p->hwsprite) {
            TransformBillboard(p, i);
        } else {
            MM_TRACE("Billboard with no sprite!");
        }
    }
}

Color BlendColors(Color a1, Color a2) {
    int alpha = (a1.a * a2.a + 127) / 255;
    int blue = (a1.b * a2.b + 127) / 255;
    int green = (a1.g * a2.g + 127) / 255;
    int red = (a1.r * a2.r + 127) / 255;
    return Color(red, green, blue, alpha);
}

void BaseRenderer::TransformBillboard(const RenderBillboard *pBillboard, int parent) {
    Sprite *pSprite = pBillboard->hwsprite;
    // error catching
    if (pSprite->texture->height() == 0 || pSprite->texture->width() == 0)
        assert(false);

    unsigned int billboard_index = NextBillboardIndex();
    RenderBillboardD3D *billboard = &pBillboardRenderListD3D[billboard_index];

    float scr_proj_x = pBillboard->screenspace_projection_factor.x;
    float scr_proj_y = pBillboard->screenspace_projection_factor.y;

    int dimming_level = pBillboard->dimming_level;
    Color diffuse = ::GetActorTintColor(dimming_level, 0, pBillboard->view_space_z, 0, pBillboard);

    bool opaquetest = false;
    if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        opaquetest = pBillboard->sTintColor.a;
    } else {
        opaquetest = dimming_level & 0xFF000000;
    }

    if (config->graphics.Tinting.value() && pBillboard->sTintColor.c32() & 0x00FFFFFF) {
        diffuse = BlendColors(pBillboard->sTintColor, diffuse);
        if (opaquetest)
            diffuse = Color(diffuse.r / 2, diffuse.g / 2, diffuse.b / 2, 0);
    }

    if (opaquetest)
        billboard->opacity = RenderBillboardD3D::Opaque_3;
    else
        billboard->opacity = RenderBillboardD3D::Transparent;

    float point_x = pSprite->uWidth / 2;
    float point_y = pSprite->uHeight;
    if (pBillboard->flags & BILLBOARD_MIRRORED) point_x *= -1.f;
    billboard->pQuads[0].diffuse = diffuse;
    billboard->pQuads[0].pos.x = pBillboard->screenPos.x - point_x * scr_proj_x;
    billboard->pQuads[0].pos.y = pBillboard->screenPos.y - point_y * scr_proj_y;
    billboard->pQuads[0].pos.z = pBillboard->view_space_z;
    billboard->pQuads[0].texcoord.x = 0.f;
    billboard->pQuads[0].texcoord.y = 0.f;

    point_x = pSprite->uWidth / 2;
    point_y = 0;
    if (pBillboard->flags & BILLBOARD_MIRRORED) point_x = point_x * -1.f;
    billboard->pQuads[1].diffuse = diffuse;
    billboard->pQuads[1].pos.x = pBillboard->screenPos.x - point_x * scr_proj_x;
    billboard->pQuads[1].pos.y = pBillboard->screenPos.y - point_y * scr_proj_y;
    billboard->pQuads[1].pos.z = pBillboard->view_space_z;
    billboard->pQuads[1].texcoord.x = 0.f;
    billboard->pQuads[1].texcoord.y = 1.f;

    point_x = pSprite->uWidth / 2;
    point_y = 0;
    if (pBillboard->flags & BILLBOARD_MIRRORED) point_x *= -1.f;
    billboard->pQuads[2].diffuse = diffuse;
    billboard->pQuads[2].pos.x = pBillboard->screenPos.x + point_x * scr_proj_x;
    billboard->pQuads[2].pos.y = pBillboard->screenPos.y - point_y * scr_proj_y;
    billboard->pQuads[2].pos.z = pBillboard->view_space_z;
    billboard->pQuads[2].texcoord.x = 1.f;
    billboard->pQuads[2].texcoord.y = 1.f;

    point_x = pSprite->uWidth / 2;
    point_y = pSprite->uHeight;
    if (pBillboard->flags & BILLBOARD_MIRRORED) point_x *= -1.f;
    billboard->pQuads[3].diffuse = diffuse;
    billboard->pQuads[3].pos.x = pBillboard->screenPos.x + point_x * scr_proj_x;
    billboard->pQuads[3].pos.y = pBillboard->screenPos.y - point_y * scr_proj_y;
    billboard->pQuads[3].pos.z = pBillboard->view_space_z;
    billboard->pQuads[3].texcoord.x = 1.f;
    billboard->pQuads[3].texcoord.y = 0.f;

    billboard->uNumVertices = 4;

    billboard->texture = pSprite->texture;
    billboard->view_space_z = pBillboard->view_space_z;
    billboard->view_space_L2 = pBillboard->view_space_L2;
    billboard->sParentBillboardID = parent;
    billboard->paletteId = pBillboard->uPaletteId;

    // Anchored at the bottom centre like the screen space quad above, with the same corners and texture coordinates.
    WorldBillboard &world = _worldBillboards[billboard_index];
    world = WorldBillboard();
    world.texture = billboard->texture;
    world.paletteId = billboard->paletteId;
    world.opacity = billboard->opacity;
    world.pos = pBillboard->worldPos;
    float halfWidth = pSprite->uWidth / 2 * pBillboard->scale.x;
    float height = pSprite->uHeight * pBillboard->scale.y;
    if (pBillboard->flags & BILLBOARD_MIRRORED)
        halfWidth = -halfWidth;
    world.facing = {{{-halfWidth, height}, {-halfWidth, 0}, {halfWidth, 0}, {halfWidth, height}}};
    for (int i = 0; i < 4; i++) {
        world.texcoords[i] = billboard->pQuads[i].texcoord;
        world.colors[i] = diffuse;
    }
}

bool BaseRenderer::AddBillboardIfVisible(Sprite* spr, int palette, const Vec3f& pos, const Vec2f& scale, BillboardFlags flags, Pid id, int sector) {
    // TODO(pskelton): replace asserts with warning and return false
    assert(::uNumBillboardsToDraw < 500 - 1);
    assert(spr != nullptr);
    assert(spr->uWidth != 0 && spr->uHeight != 0);

    Vec3f viewspace;
    if (pCamera3D->ViewClip(pos, &viewspace)) {
        Vec2f projected = pCamera3D->Project(viewspace);
        Vec2f billScale = scale * pCamera3D->ViewPlaneDistPixels / viewspace.x;
        float billboardWidth = billScale.x * spr->uWidth;
        float billboardHeight = billScale.y * spr->uHeight;
        Rectf billboardRect(projected.x - billboardWidth / 2, projected.y - billboardHeight, billboardWidth, billboardHeight);

        if (pViewport.intersects(billboardRect)) {
            RenderBillboard& bill = pBillboardRenderList[::uNumBillboardsToDraw++];

            bill.hwsprite = spr;
            bill.worldPos = pos;
            bill.screenPos = projected;
            bill.view_space_z = viewspace.x;
            bill.view_space_L2 = viewspace.length();
            bill.screenspace_projection_factor = billScale;
            bill.scale = scale;
            bill.uPaletteId = palette;
            bill.flags = flags;
            bill.uIndoorSectorID = sector;
            bill.object_pid = id;
            bill.dimming_level = 0;
            bill.sTintColor = Color();
            return true;
        }
    }
    return false;
}

void BaseRenderer::MakeParticleBillboardAndPush(const Particle& p) {
    unsigned int billboard_index = NextBillboardIndex();
    RenderBillboardD3D *billboard = &pBillboardRenderListD3D[billboard_index];

    billboard->opacity = RenderBillboardD3D::Opaque_1;
    billboard->view_space_z = p.view_space_z;
    billboard->view_space_L2 = p.view_space_L2;
    billboard->sParentBillboardID = -1;
    billboard->texture = p.type & ParticleType_Diffuse ? nullptr : p.texture;
    billboard->paletteId = p.paletteID;
    billboard->uNumVertices = 4;

    float acos = std::cos(p.angle); // TODO(captainurist): taking cos of an INT angle? WTF?
    float asin = std::sin(p.angle);

    WorldBillboard &world = _worldBillboards[billboard_index];
    world = WorldBillboard();
    world.texture = billboard->texture;
    world.paletteId = billboard->paletteId;
    world.opacity = billboard->opacity;
    world.pos = Vec3f(p.x, p.y, p.z);
    std::array<Vec2f, 4> corners = {{{-12, -12}, {-12, 12}, {12, 12}, {12, -12}}};
    for (int i = 0; i < 4; i++) {
        // The screen space corners below in world units, with y flipped to point up.
        float x = acos * corners[i].x - asin * corners[i].y;
        float y = acos * corners[i].y + asin * corners[i].x - 12.f;
        world.facing[i] = Vec2f(x, -y) * p.particle_size;
        world.colors[i] = p.fadedColor;
    }
    world.texcoords = {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};

    {
        float v16 = -12.f;
        float v17 = -12.f;
        billboard->pQuads[0].pos.x = (acos * v16 - asin * v17) * p.screenspace_scale + (float)p.uScreenSpaceX;
        billboard->pQuads[0].pos.y = (acos * v17 + asin * v16 - 12.f) * p.screenspace_scale + (float)p.uScreenSpaceY;
        billboard->pQuads[0].pos.z = p.view_space_z;
        billboard->pQuads[0].diffuse = p.fadedColor;
        billboard->pQuads[0].texcoord.x = 0.f;
        billboard->pQuads[0].texcoord.y = 0.f;
    }

    {
        float v31 = -12;
        float v32 = 12;
        billboard->pQuads[1].pos.x = (acos * v31 - asin * v32) * p.screenspace_scale + (float)p.uScreenSpaceX;
        billboard->pQuads[1].pos.y = (acos * v32 + asin * v31 - 12.f) * p.screenspace_scale + (float)p.uScreenSpaceY;
        billboard->pQuads[1].pos.z = p.view_space_z;
        billboard->pQuads[1].diffuse = p.fadedColor;
        billboard->pQuads[1].texcoord.x = 0.0;
        billboard->pQuads[1].texcoord.y = 1.0;
    }

    {
        float v23 = 12;
        float v24 = 12;
        billboard->pQuads[2].pos.x = (acos * v23 - asin * v24) * p.screenspace_scale + (float)p.uScreenSpaceX;
        billboard->pQuads[2].pos.y = (acos * v24 + asin * v23 - 12.f) * p.screenspace_scale + (float)p.uScreenSpaceY;
        billboard->pQuads[2].pos.z = p.view_space_z;
        billboard->pQuads[2].diffuse = p.fadedColor;
        billboard->pQuads[2].texcoord.x = 1.0;
        billboard->pQuads[2].texcoord.y = 1.0;
    }

    {
        float v39 = 12;
        float v40 = -12;
        billboard->pQuads[3].pos.x = (acos * v39 - asin * v40) * p.screenspace_scale + (float)p.uScreenSpaceX;
        billboard->pQuads[3].pos.y = (acos * v40 + asin * v39 - 12.f) * p.screenspace_scale + (float)p.uScreenSpaceY;
        billboard->pQuads[3].pos.z = p.view_space_z;
        billboard->pQuads[3].diffuse = p.fadedColor;
        billboard->pQuads[3].texcoord.x = 1.0;
        billboard->pQuads[3].texcoord.y = 0.0;
    }
}

float BaseRenderer::GetGamma() {
    const float base = 0.60f;
    const float mult = 0.1f;
    int level = config->graphics.Gamma.value();
    return base + mult * level;
}

//----- (004A4CC9) ---------------------------------------
void BaseRenderer::BillboardSphereSpellFX(SpellFX_Billboard *a1, Color diffuse) {
    // fireball / implosion sphere
    // TODO(pskelton): could draw in 3d rather than convert to billboard for ogl

    if (a1->uNumVertices < 3) {
        return;
    }

    float depth = 1000000.0;
    int index = 0;
    for (unsigned i = 0; i < (unsigned int)a1->uNumVertices; ++i) {
        if (a1->field_104[i].pos.z < depth) {
            depth = a1->field_104[i].pos.z;
            index = i;
        }
    }
    float view_space_L2 = a1->field_104[index].pos.length();

    unsigned int v5 = NextBillboardIndex();
    pBillboardRenderListD3D[v5].sParentBillboardID = -1;
    pBillboardRenderListD3D[v5].opacity = RenderBillboardD3D::Opaque_2;
    pBillboardRenderListD3D[v5].texture = nullptr;
    pBillboardRenderListD3D[v5].uNumVertices = a1->uNumVertices;
    pBillboardRenderListD3D[v5].paletteId = 0;
    pBillboardRenderListD3D[v5].view_space_z = depth;
    pBillboardRenderListD3D[v5].view_space_L2 = view_space_L2;

    pBillboardRenderListD3D[v5].pQuads[3].pos.x = 0.0f;
    pBillboardRenderListD3D[v5].pQuads[3].pos.y = 0.0f;
    pBillboardRenderListD3D[v5].pQuads[3].pos.z = 0.0f;

    // The whole triangle, the frustum clips it on the GPU.
    WorldBillboard &world = _worldBillboards[v5];
    world = WorldBillboard();
    world.opacity = RenderBillboardD3D::Opaque_2;
    world.numVertices = 3;
    world.pos = (a1->field_14[0].pos + a1->field_14[1].pos + a1->field_14[2].pos) / 3;
    for (int i = 0; i < 3; i++) {
        world.offsets[i] = a1->field_14[i].pos - world.pos;
        world.texcoords[i] = Vec2f(0.5f, 0.5f);
        world.colors[i] = diffuse.a ? a1->field_14[i].diffuse : diffuse;
    }

    for (unsigned int i = 0; i < (unsigned int)a1->uNumVertices; ++i) {
        pBillboardRenderListD3D[v5].pQuads[i].pos = a1->field_104[i].pos;

        Color v12;
        if (diffuse.a) {
            v12 = a1->field_104[i].diffuse;
        } else {
            v12 = diffuse;
        }
        pBillboardRenderListD3D[v5].pQuads[i].diffuse = v12;

        pBillboardRenderListD3D[v5].pQuads[i].texcoord.x = 0.5;
        pBillboardRenderListD3D[v5].pQuads[i].texcoord.y = 0.5;
    }
}

void BaseRenderer::DrawMonsterPortrait(const Recti &rc, SpriteFrame *Portrait, int Y_Offset) {
    Recti rct;
    rct.x = rc.x + 64 - Portrait->sprites[0]->uWidth / 2;
    rct.y = rc.y + Y_Offset;
    rct.w = Portrait->sprites[0]->uWidth;
    rct.h = Portrait->sprites[0]->uHeight;

    render->SetUIClipRect(rc);
    render->DrawImage(Portrait->sprites[0]->texture, rct, Portrait->paletteId);
    render->ResetUIClipRect();
}

void BaseRenderer::DrawSpecialEffectsQuad(GraphicsImage *texture, int palette) {
    // TODO(captainurist): Old code used BR - TL (one less than actual width/height), preserving that behavior for now.
    Recti targetrect(pViewport.x, pViewport.y, pViewport.w - 1, pViewport.h - 1);
    DrawImage(texture, targetrect, palette, colorTable.MediumGrey);
}

void BaseRenderer::DrawBillboards_And_MaybeRenderSpecialEffects_And_EndScene() {
    render->DoRenderBillboards_D3D();
    engine->draw_debug_outlines();
    spell_fx_renderer->drawSpecialEffects();
}

// TODO: should this be combined / moved out of render
std::vector<Actor*> BaseRenderer::getActorsInViewport(int pDepth) {
    std::vector<Actor*> foundActors;

    for (int i = 0; i < render->uNumBillboardsToDraw; i++) {
        int renderId = render->pSortedBillboardRenderListD3D[i]->sParentBillboardID;
        if(renderId == -1) {
            continue; // E.g. spell particle.
        }

        Pid pid = pBillboardRenderList[renderId].object_pid;
        if (pid.type() == OBJECT_Actor) {
            if (pBillboardRenderList[renderId].view_space_z <= pDepth) {
                int id = pid.id();
                if (pActors[id].aiState != Dead &&
                    pActors[id].aiState != Dying &&
                    pActors[id].aiState != Removed &&
                    pActors[id].aiState != Disabled &&
                    pActors[id].aiState != Summoned) {
                    if (vis->DoesRayIntersectBillboard(static_cast<float>(pDepth), i)) {
                        // Limit for 100 actors was removed
                        foundActors.push_back(&pActors[id]);
                    }
                }
            }
        }
    }
    return foundActors;
}

bool BaseRenderer::Reinitialize(bool firstInit) {
    updateRenderDimensions();
    return true;
}

Sizei BaseRenderer::GetRenderDimensions() {
    return outputRender;
}

Sizei BaseRenderer::GetPresentDimensions() {
    return outputPresent;
}

void BaseRenderer::updateRenderDimensions() {
    outputPresent = window->size();

    if (config->graphics.RenderFilter.value() != 0)
        outputRender = {config->graphics.RenderWidth.value(), config->graphics.RenderHeight.value()};
    else
        outputRender = outputPresent;

    // Set viewport from config values (inclusive TL/BR coordinates).
    int tlX = config->graphics.ViewPortX1.value();  // 8 in vanilla
    int tlY = config->graphics.ViewPortY1.value();  // 8 in vanilla
    int brX = outputRender.w - config->graphics.ViewPortX2.value();  // 468 in vanilla
    int brY = outputRender.h - config->graphics.ViewPortY2.value();  // 352 in vanilla
    pViewport = Recti(tlX, tlY, brX - tlX + 1, brY - tlY + 1);
}

Pointi BaseRenderer::MapToRender(Pointi position) {
    Sizef renDims = { (float)GetRenderDimensions().w, (float)GetRenderDimensions().h };
    Sizef prDims = { (float)GetPresentDimensions().w, (float)GetPresentDimensions().h };
    Pointi result = position;

    if (renDims != prDims) {
        Sizef ratioCorections = { prDims.w / renDims.w, prDims.h / renDims.h };
        float ratioCorrection = std::min(ratioCorections.w, ratioCorections.h);

        float w = renDims.w * ratioCorrection;
        float h = renDims.h * ratioCorrection;

        result.x = std::round((position.x - (prDims.w / 2 - w / 2)) / ratioCorrection);
        result.y = std::round((position.y - (prDims.h / 2 - h / 2)) / ratioCorrection);
    }

    return result;
}

Pointi BaseRenderer::MapToPresent(Pointi position) {
    Sizef renDims = { (float)GetRenderDimensions().w, (float)GetRenderDimensions().h };
    Sizef prDims = { (float)GetPresentDimensions().w, (float)GetPresentDimensions().h };
    Pointi result = position;

    if (renDims != prDims) {
        Sizef ratioCorections = { prDims.w / renDims.w, prDims.h / renDims.h };
        float ratioCorrection = std::min(ratioCorections.w, ratioCorections.h);

        float w = renDims.w * ratioCorrection;
        float h = renDims.h * ratioCorrection;

        result.x = std::round(position.x * ratioCorrection + (prDims.w / 2 - w / 2));
        result.y = std::round(position.y * ratioCorrection + (prDims.h / 2 - h / 2));
    }

    return result;
}

void BaseRenderer::buildTerrainBloodsplatDecals() {
    // TODO(pskelton): clean up and move to seperate function in decal builder
    if (!decal_builder->bloodsplat_container->uNumBloodsplats) return;
    unsigned int NumBloodsplats = decal_builder->bloodsplat_container->uNumBloodsplats;

    // loop over blood to lay
    for (unsigned i = 0; i < NumBloodsplats; ++i) {
        // approx location of bloodsplat
        Vec2i gridPos = worldToGrid(decal_builder->bloodsplat_container->pBloodsplats_to_apply[i].pos);
        // use terrain squares in block surrounding to try and stack faces

        int scope = std::ceil(decal_builder->bloodsplat_container->pBloodsplats_to_apply[i].radius / 512);

        for (int loopy = (gridPos.y - scope); loopy <= (gridPos.y + scope); ++loopy) {
            for (int loopx = (gridPos.x - scope); loopx <= (gridPos.x + scope); ++loopx) {
                if (loopy < 0) continue;
                if (loopy > 126) continue;
                if (loopx < 0) continue;
                if (loopx > 126) continue;


                // top tri
                // x, y
                VertexRenderList[0].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx, loopy}).toFloat();
                // x + 1, y + 1
                VertexRenderList[1].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx + 1, loopy + 1}).toFloat();
                // x + 1, y
                VertexRenderList[2].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx + 1, loopy}).toFloat();

                // bottom tri
                // x, y
                VertexRenderList[3].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx, loopy}).toFloat();
                // x, y + 1
                VertexRenderList[4].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx, loopy + 1}).toFloat();
                // x + 1, y + 1
                VertexRenderList[5].vWorldPosition = pOutdoor->pTerrain.vertexByGridUnsafe({loopx + 1, loopy + 1}).toFloat();

                float WorldMinZ = pOutdoor->GetPolygonMinZ(VertexRenderList, 6);
                float WorldMaxZ = pOutdoor->GetPolygonMaxZ(VertexRenderList, 6);

                // TODO(pskelton): terrain and boxes should be saved for easier retrieval
                // test expanded box against bloodsplat
                BBoxf thissquare{ VertexRenderList[0].vWorldPosition.x,
                                  VertexRenderList[1].vWorldPosition.x,
                                  VertexRenderList[1].vWorldPosition.y,
                                  VertexRenderList[0].vWorldPosition.y,
                                  WorldMinZ,
                                  WorldMaxZ };

                // skip this square if no splat over lap
                if (!thissquare.intersectsCube(decal_builder->bloodsplat_container->pBloodsplats_to_apply[i].pos, decal_builder->bloodsplat_container->pBloodsplats_to_apply[i].radius))
                    continue;

                // splat hits this square of terrain
                bool fading = pOutdoor->pTerrain.isWaterOrShoreByGrid({loopx, loopy});

                const auto &[norm, norm2] = pOutdoor->pTerrain.normalsByGridUnsafe({loopx, loopy});

                float Light_tile_dist = 0.0;

                // top tri
                float _f1 = norm.x * pOutdoor->vSunlight.x + norm.y * pOutdoor->vSunlight.y + norm.z * pOutdoor->vSunlight.z;
                int dimming_level = std::clamp(static_cast<int>(20.0f - floorf(20.0f * _f1 + 0.5f)), 0, 31);

                decal_builder->ApplyBloodSplatToTerrain(fading, norm, &Light_tile_dist, VertexRenderList, i);
                Planef plane;
                plane.normal = norm;
                plane.dist = Light_tile_dist;
                if (decal_builder->uNumSplatsThisFace > 0)
                    decal_builder->BuildAndApplyDecals(31 - dimming_level, LocationTerrain, plane, 3, VertexRenderList, 0, -1);

                //bottom tri
                float _f = norm2.x * pOutdoor->vSunlight.x + norm2.y * pOutdoor->vSunlight.y + norm2.z * pOutdoor->vSunlight.z;
                dimming_level = std::clamp(static_cast<int>(20.0 - floorf(20.0 * _f + 0.5f)), 0, 31);

                decal_builder->ApplyBloodSplatToTerrain(fading, norm2, &Light_tile_dist, (VertexRenderList + 3), i);
                plane.normal = norm2;
                plane.dist = Light_tile_dist;
                if (decal_builder->uNumSplatsThisFace > 0)
                    decal_builder->BuildAndApplyDecals(31 - dimming_level, LocationTerrain, plane, 3, (VertexRenderList + 3), 0, -1);
            }
        }
    }
}

void BaseRenderer::buildBuildingBloodsplatDecals() {
    // TODO(pskelton): clean up
    if (!decal_builder->bloodsplat_container->uNumBloodsplats) return;

    for (BSPModel &model : pOutdoor->pBModels) {
        if (model.faces.empty()) {
            continue;
        }

        // check for any splat in this models box - if not continue
        bool found = false;
        for (int splat = 0; splat < decal_builder->bloodsplat_container->uNumBloodsplats; ++splat) {
            Bloodsplat *thissplat = &decal_builder->bloodsplat_container->pBloodsplats_to_apply[splat];
            if (model.boundingBox.intersectsCube(thissplat->pos, thissplat->radius)) {
                found = true;
                break;
            }
        }
        if (!found) continue;

        for (int faceId : model.faces) {
            BLVFace &face = pOutdoor->faces[faceId];
            if (face.Invisible()) {
                continue;
            }

            float _f1 = face.facePlane.normal.x * pOutdoor->vSunlight.x + face.facePlane.normal.y * pOutdoor->vSunlight.y + face.facePlane.normal.z * pOutdoor->vSunlight.z;
            int dimming_level = std::clamp(static_cast<int>(20.0 - floorf(20.0 * _f1 + 0.5f)), 0, 31);

            for (unsigned vertex_id = 1; vertex_id <= face.vertexIds.size(); vertex_id++) {
                array_73D150[vertex_id - 1].vWorldPosition = pOutdoor->vertices[face.vertexIds[vertex_id - 1]];
            }

            for (int vertex_id = 0; vertex_id < face.vertexIds.size(); ++vertex_id) {
                memcpy(&VertexRenderList[vertex_id], &array_73D150[vertex_id], sizeof(VertexRenderList[vertex_id]));
                VertexRenderList[vertex_id]._rhw = 1.0 / (array_73D150[vertex_id].vWorldViewPosition.x + 0.0000001);
            }

            decal_builder->ApplyBloodsplatDecalsToFace(&face);
            if (decal_builder->uNumSplatsThisFace > 0) {
                decal_builder->BuildAndApplyDecals(
                    31 - dimming_level, LocationBuildings,
                    face.facePlane,
                    face.vertexIds.size(), VertexRenderList, 0, -1);
            }
        }
    }
}

void BaseRenderer::buildIndoorBloodsplatDecals() {
    if (!decal_builder->bloodsplat_container->uNumBloodsplats) return;
    RenderVertexSoft static_vertices_buff_in[64];  // buff in
    int uCurrentAmbientLightLevel = pIndoor->ambientLightLevel();

    // loop over faces
    for (int test = 0; test < pIndoor->faces.size(); test++) {
        BLVFace *pface = &pIndoor->faces[test];

        if (pface->isPortal()) continue;
        // TODO(yoctozepto, pskelton): we should probably try to handle these faces as they are otherwise marked as visible (see also BSPRenderer)
        if (!pface->GetTexture()) continue;

        // check if faces is visible
        bool onlist = false;
        for (unsigned i = 0; i < pBspRenderer->uNumVisibleNotEmptySectors; ++i) {
            int listsector = pBspRenderer->pVisibleSectorIDs_toDrawDecorsActorsEtcFrom[i];
            if (pface->sectorId == listsector) {
                onlist = true;
                break;
            }
        }
        if (!onlist) continue;


        decal_builder->ApplyBloodsplatDecalsToFace(pface);
        if (!decal_builder->uNumSplatsThisFace) continue;

        // copy to buff in
        for (unsigned i = 0; i < pface->vertexIds.size(); ++i) {
            static_vertices_buff_in[i].vWorldPosition = pIndoor->vertices[pface->vertexIds[i]];
            static_vertices_buff_in[i].u = pface->textureUs[i];
            static_vertices_buff_in[i].v = pface->textureVs[i];
        }

        // blood draw
        decal_builder->BuildAndApplyDecals(uCurrentAmbientLightLevel, LocationIndoors, pface->facePlane,
            pface->vertexIds.size(), static_vertices_buff_in,
            0, pface->sectorId);
    }
}
