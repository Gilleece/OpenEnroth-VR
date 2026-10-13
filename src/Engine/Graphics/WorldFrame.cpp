#include "WorldFrame.h"

#include <cstdlib>

#include "Engine/Engine.h"
#include "Engine/SpellFxRenderer.h"
#include "Engine/Graphics/DecalBuilder.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/Lighting.h"
#include "Engine/Graphics/LocationFunctions.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/ParticleEngine.h"
#include "Engine/Graphics/Renderer/Renderer.h"
#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/Sprites.h"
#include "Engine/Graphics/SpriteEnumFunctions.h"
#include "Engine/Objects/Actor.h"
#include "Engine/Objects/Decoration.h"
#include "Engine/Tables/DecorationTable.h"
#include "Engine/Party.h"
#include "Engine/Timer.h"

#include "Utility/Math/TrigLut.h"


static constexpr float SUN_SHADOW_CASTER_REACH = 8192.0f; // How far past the shadow distance a building can still cast into it.
static constexpr float SUN_SHADOW_SPRITE_REACH = 1024.0f; // Same for sprites.

void WorldFrame::prepare() {
    if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
        prepareIndoor();
        _prepared = true;
    } else if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        prepareOutdoor();
        _prepared = true;
    }
}

void WorldFrame::drawShadows() {
    if (_prepared && uCurrentlyLoadedLevelType == LEVEL_OUTDOOR)
        render->drawSunShadows(_sunShadowModels, _sunShadowSprites);
    if (_prepared && uCurrentlyLoadedLevelType == LEVEL_INDOOR)
        render->drawTorchShadows();
}

void WorldFrame::draw() {
    if (_prepared) {
        drawLevel();
        engine->decal_builder->DrawBloodsplats();
    }
    render->DrawBillboards_And_MaybeRenderSpecialEffects_And_EndScene();
}

void WorldFrame::drawWithoutDecals() {
    if (_prepared)
        drawLevel();
    render->DrawBillboards_And_MaybeRenderSpecialEffects_And_EndScene();
}

void WorldFrame::finish() {
    engine->spell_fx_renedrer->finishFrame();
    _prepared = false;
}

//----- (00441CFF) --------------------------------------------------------
void WorldFrame::prepareOutdoor() {
    pOutdoor->prepareFrameStatic();
    render->buildBloodsplatDecals();

    // The sky, terrain and buildings are lit with the previous frame's lights, prepareFrameLists replaces them.
    _staticGeometryLights.mobile = *pMobileLightsStack;
    _staticGeometryLights.stationary = *pStationaryLightsStack;
    pOutdoor->prepareFrameLists();
    prepareSunShadowCasters();

    engine->particle_engine->prepareFrame();
    // pWeather->updateSnow(); // Engine::DrawGUI already calls this once a frame, a second call doubles the snow speed.
    trail_particle_generator.UpdateParticles();
    render->sortBillboardRenderList();
}

/**
 * @param pos                           Position of a sprite.
 * @param yaw                           Yaw of the sprite.
 * @return                              Octant of the sprite's frame that faces the sun, like the octants that face the
 *                                      camera in `OutdoorLocation::PrepareActorsDrawList`.
 */
static int sunOctant(Vec3f pos, int yaw) {
    Vec3f sun = pOutdoor->vSunlight * 65536.0f;
    int angle = TrigLUT.atan2(static_cast<int>(-sun.x), static_cast<int>(-sun.y));
    return ((TrigLUT.uIntegerPi + (TrigLUT.uIntegerPi >> 3) + yaw - angle) >> 8) & 7;
}

static void addSunShadowSprite(std::vector<SunShadowSprite> *sprites, SpriteFrame *frame, Vec3f pos, int yaw) {
    int octant = sunOctant(pos, yaw);
    Sprite *sprite = frame->sprites[octant];
    if (!sprite || sprite->uWidth == 0 || sprite->uHeight == 0)
        return;

    SunShadowSprite &result = sprites->emplace_back();
    result.sprite = sprite;
    result.paletteId = frame->paletteId;
    result.pos = pos;
    result.scale = frame->scale;
    result.mirrored = billboardFlagsForSprite(frame->flags, octant) & BILLBOARD_MIRRORED;
}

void WorldFrame::prepareSunShadowCasters() {
    _sunShadowModels.clear();
    _sunShadowSprites.clear();
    if (!engine->config->graphics.SunShadows.value())
        return;

    // Not the view's culled lists, a model outside every view can still shadow what's in one. In VR those lists are
    // also culled from the head, not from the party.
    Vec2f camera(pCamera3D->vCameraPos.x, pCamera3D->vCameraPos.y);
    float reach = engine->config->graphics.ShadowDistance.value() + SUN_SHADOW_CASTER_REACH;
    for (int i = 0; i < static_cast<int>(pOutdoor->pBModels.size()); i++) {
        const BSPModel &model = pOutdoor->pBModels[i];
        if ((model.boundingCenter.xy() - camera).length() < reach + model.boundingRadius)
            _sunShadowModels.push_back(i);
    }

    // Sprites are short, only those inside the shadow distance can cast into it. The frames follow
    // PrepareDecorationsRenderList_ODM and PrepareActorsDrawList, without their side effects.
    float spriteReach = engine->config->graphics.ShadowDistance.value() + SUN_SHADOW_SPRITE_REACH;
    for (const LevelDecoration &decoration : pLevelDecorations) {
        if ((decoration.vPosition.xy() - camera).length() > spriteReach)
            continue;
        if (decoration.uFlags & (LEVEL_DECORATION_OBELISK_CHEST | LEVEL_DECORATION_INVISIBLE))
            continue; // IsObeliskChestActive can reveal the chest, which only the view's own prepare may do.
        const DecorationData *desc = pDecorationTable->decoration(decoration.uDecorationDescID);
        if (desc->uFlags & (DECORATION_DATA_EMITS_FIRE | DECORATION_DATA_MARKER | DECORATION_DATA_DONT_DRAW))
            continue;

        Duration time = animTimer->time() + Duration::fromTicks(std::abs(decoration.vPosition.x + decoration.vPosition.y));
        SpriteFrame *frame = engine->config->graphics.SeasonsChange.value() ?
            LevelDecorationChangeSeason(desc, time, pParty->uCurrentMonth) : pSpriteFrameTable->GetFrame(desc->uSpriteID, time);
        if (!frame || frame->textureName == "null")
            continue;
        addSunShadowSprite(&_sunShadowSprites, frame, decoration.vPosition, decoration._yawAngle);
    }

    for (int i = 0; i < static_cast<int>(pActors.size()); i++) {
        const Actor &actor = pActors[i];
        if (actor.aiState == Removed || actor.aiState == Disabled || actor.aiState == Summoned)
            continue;
        if ((actor.pos.xy() - camera).length() > spriteReach)
            continue;

        Duration time = actor.currentActionTime;
        if (actor.currentActionAnimation == ANIM_Walking)
            time = i * 32_ticks + (pParty->bTurnBasedModeOn ? animTimer->time() : gameTimer->time());
        if (actor.buffs[ACTOR_BUFF_STONED].Active() || actor.buffs[ACTOR_BUFF_PARALYZED].Active())
            time = 0_ticks;

        SpriteFrame *frame = actor.aiState == Resurrected ?
            pSpriteFrameTable->GetFrameReversed(actor.spriteIds[actor.currentActionAnimation], time) :
            pSpriteFrameTable->GetFrame(actor.spriteIds[actor.currentActionAnimation], time);
        if (!frame || frame->spriteName == "null")
            continue;
        addSunShadowSprite(&_sunShadowSprites, frame, actor.pos, actor.yawAngle);
    }
}

//----- (00441BD4) --------------------------------------------------------
void WorldFrame::prepareIndoor() {
    PrepareDrawLists_BLV();
    if (pBLVRenderParams->uPartySectorID)
        render->buildBloodsplatDecals();
    render->TransformBillboards();
    engine->particle_engine->prepareFrame();
    trail_particle_generator.UpdateParticles();
    render->sortBillboardRenderList();
}

void WorldFrame::drawLevel() {
    if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
        drawIndoor();
    } else {
        drawOutdoor();
    }
}

void WorldFrame::drawOutdoor() {
    pOutdoor->drawStatic(_staticGeometryLights);
    engine->particle_engine->drawLines();
}

void WorldFrame::drawIndoor() {
    if (pBLVRenderParams->uPartySectorID)
        pIndoor->DrawIndoorFaces(true);
    engine->particle_engine->drawLines();
}
