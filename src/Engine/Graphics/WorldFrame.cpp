#include "WorldFrame.h"

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

static constexpr float SUN_SHADOW_CASTER_REACH = 8192.0f; // How far past the shadow distance a building can still cast into it.

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
        render->drawSunShadows(_sunShadowModels);
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

void WorldFrame::prepareSunShadowCasters() {
    _sunShadowModels.clear();
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
