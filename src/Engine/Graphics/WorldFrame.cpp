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

void WorldFrame::prepare() {
    if (uCurrentlyLoadedLevelType == LEVEL_INDOOR) {
        prepareIndoor();
        _prepared = true;
    } else if (uCurrentlyLoadedLevelType == LEVEL_OUTDOOR) {
        prepareOutdoor();
        _prepared = true;
    }
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

    engine->particle_engine->prepareFrame();
    // pWeather->updateSnow(); // Engine::DrawGUI already calls this once a frame, a second call doubles the snow speed.
    trail_particle_generator.UpdateParticles();
    render->sortBillboardRenderList();
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
