#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Testing/Game/GameTest.h"

#include "Application/GameConfig.h"

#include "Engine/Engine.h"
#include "Engine/EngineGlobals.h"
#include "Engine/Party.h"
#include "Engine/Graphics/RenderEntities.h"
#include "Engine/Graphics/Renderer/Renderer.h"
#include "Engine/Vr/VrSystem.h"

#include "GUI/GUIButton.h"
#include "GUI/GUIWindow.h"

#include "Io/Mouse.h"

#include "Library/Environment/Interface/Environment.h"
#include "Library/Logger/Logger.h"
#include "Library/Logger/LogSink.h"
#include "Library/Platform/Application/PlatformApplication.h"
#include "Library/Platform/Interface/PlatformEvents.h"
#include "Library/Platform/Interface/PlatformWindow.h"

#include "Utility/ScopeGuard.h"

static constexpr float MIN_FRAME_RATE = 50; // Frames per second, under the 72 Hz or more that headsets refresh at.

class WarningSink : public LogSink {
 public:
    explicit WarningSink(LogSink *next) : _next(next) {}

    virtual void write(const LogCategory &category, LogLevel level, std::string_view message) override {
        // Keeps the warnings and errors, and passes everything on.
        if (level >= LOG_WARNING)
            _lines.push_back(fmt::format("[{}] {}", logLevelName(level), message));
        _next->write(category, level, message);
    }

    LogSink *next() const {
        return _next;
    }

    const std::vector<std::string> &lines() const {
        return _lines;
    }

 private:
    LogSink *_next = nullptr;
    std::vector<std::string> _lines;
};

static int64_t countFrames(const VrSystem::FrameStats &stats, bool world, bool spreadHud) {
    // Frames the runtime wanted drawn, with or without the world and the spread HUD.
    int64_t result = 0;
    for (const auto &[kind, count] : stats.kinds)
        if (kind.world == world && kind.spreadHud == spreadHud)
            result += count;
    return result;
}

static std::map<uint16_t, Vec3f> uprightSpriteAxes() {
    // The level axis that each upright sprite of the last frame stands across, by object.
    std::map<uint16_t, Vec3f> result;
    for (int i = 0; i < static_cast<int>(render->uNumBillboardsToDraw); i++) {
        const WorldBillboard *billboard = render->builtWorldBillboard(i);
        if (billboard && billboard->frame && !billboard->centered && billboard->id)
            result[billboard->id.packed()] = billboard->right;
    }
    return result;
}

static int64_t countFadeFrames(const VrSystem::FrameStats &stats) {
    // Frames the runtime wanted drawn with the last world frame fading to black.
    int64_t result = 0;
    for (const auto &[kind, count] : stats.kinds)
        if (kind.fade)
            result += count;
    return result;
}

static void moveMouseBy(EngineController &game, Pointi rel) {
    // Motion as the window reports it in relative mode, with no position.
    std::unique_ptr<PlatformMouseEvent> event = std::make_unique<PlatformMouseEvent>();
    event->type = EVENT_MOUSE_MOVE;
    event->window = application->window();
    event->button = BUTTON_NONE;
    event->buttons = BUTTON_NONE;
    event->rel = rel;
    game.postEvent(std::move(event));
}

static void checkFrames(VrSystem *vr, std::string_view phase) {
    // Each frame carries the eyes' projection layer if it has the world, a quad over it while it fades out, and the UI
    // as one quad or the spread HUD's six, which only spreads around the world. Every quad shows its whole swapchain
    // image, since runtimes don't agree on where a part of one is.
    if (!vr)
        return;
    EXPECT_EQ(vr->frameStats().partialImageQuads, 0) << phase;
    for (const auto &[kind, count] : vr->frameStats().kinds) {
        uint32_t expected = (kind.world ? 1 : 0) + (kind.fade ? 1 : 0) + (kind.spreadHud ? 6 : 1);
        EXPECT_EQ(kind.layers, expected) << phase << ": " << count << " frames, world " << kind.world << ", fade " << kind.fade
                                         << ", spread HUD " << kind.spreadHud;
        EXPECT_TRUE(kind.world || !kind.spreadHud) << phase << ": " << count << " frames spread the HUD without the world";
        EXPECT_TRUE(kind.world || !kind.fade) << phase << ": " << count << " frames faded out a world they didn't carry";
    }
}

GAME_TEST(VrSmoke, Session) {
    // Plays a session from the main menu to quitting through the screens the headset draws, and checks what the
    // runtime got. Run_VrSmoke runs it flat first, which records the warnings and errors the headset pass has to match.
    std::unique_ptr<Environment> environment = Environment::createStandardEnvironment();
    std::string pass = environment->getenv("OPENENROTH_VR_SMOKE_PASS");
    std::string baseline = environment->getenv("OPENENROTH_VR_SMOKE_BASELINE");
    if (environment->getenv("OPENENROTH_VR_SMOKE").empty() || pass.empty() || baseline.empty())
        GTEST_SKIP() << "Run_VrSmoke runs this, with OPENENROTH_VR_SMOKE naming an OpenXR runtime json.";

    bool headset = pass == "vr";
    VrSystem *vr = dynamic_cast<VrSystem *>(engine->_worldFrameViews);
    if (headset)
        ASSERT_NE(vr, nullptr) << "The headset session didn't start.";
    ASSERT_TRUE(engine->config->graphics.Enhanced.value());

    WarningSink warnings(detail::logger->sink());
    detail::logger->setSink(&warnings);
    auto restore = ScopeGuard([&] {
        detail::logger->setSink(warnings.next());
        engine->config->vr.HudLayout.setValue(VR_HUD_LAYOUT_CLASSIC);
        engine->config->vr.UiFollow.setValue(VR_UI_FOLLOW_FIXED);
        if (vr) {
            vr->setHeadTurn(0, 0);
            vr->setHeadShift(Vec3f());
            vr->setPositionTracking(true);
        }
    });

    auto phase = [&](std::string_view name) {
        checkFrames(vr, name);
        if (vr)
            vr->resetFrameStats();
    };

    // From the main menu to a new game.
    engine->config->debug.NoActors.setValue(true); // Nobody wanders into the way of the Space presses.
    engine->config->debug.NoMargaret.setValue(true); // She would stop the walk with her greeting.
    game.tick(10);
    phase("main menu");
    game.pressGuiButton("MainMenu_NewGame");
    game.tick(10);
    phase("party creation");
    game.pressGuiButton("PartyCreation_OK");
    game.skipLoadingScreen();
    game.tick(10);
    phase("new game");
    if (vr) {
        EXPECT_TRUE(vr->focused());
        // The session grabs the mouse, and the window still reports the setting that window.mouse_grab is saved from.
        EXPECT_EQ(application->window()->grabsMouse(), engine->config->window.MouseGrab.value());
    }
    int startYaw = pParty->_viewYaw * 90 / 512;

    // A few seconds of walking and turning.
    game.pressKey(PlatformKey::KEY_UP);
    game.tick(120);
    game.releaseKey(PlatformKey::KEY_UP);
    game.pressKey(PlatformKey::KEY_LEFT);
    game.tick(90);
    game.releaseKey(PlatformKey::KEY_LEFT);
    game.pressKey(PlatformKey::KEY_RIGHT);
    game.tick(90);
    game.releaseKey(PlatformKey::KEY_RIGHT);
    if (vr) {
        const VrSystem::FrameStats &stats = vr->frameStats();
        EXPECT_GT(countFrames(stats, true, false), 0);
        double seconds = std::chrono::duration<double>(stats.lastFrameTime - stats.firstFrameTime).count();
        EXPECT_GE((stats.frames - 1) / seconds, MIN_FRAME_RATE);
    }
    phase("walking and turning");

    // Looking up and down.
    if (vr)
        vr->setHeadTurn(0, 45);
    game.tick(30);
    if (vr)
        vr->setHeadTurn(0, -45);
    game.tick(30);
    if (vr)
        vr->setHeadTurn(0, 0);
    phase("looking up and down");

    // Turning the head in place leaves the upright sprites standing as they were, each facing the head.
    if (vr) {
        game.tick(2);
        std::map<uint16_t, Vec3f> ahead = uprightSpriteAxes();
        vr->setHeadTurn(30, 0);
        game.tick(2);
        std::map<uint16_t, Vec3f> turned = uprightSpriteAxes();
        vr->setHeadTurn(0, 0);
        int compared = 0;
        for (const auto &[id, axis] : turned) {
            if (!ahead.contains(id))
                continue;
            EXPECT_GT(dot(axis, ahead.at(id)), 0.9999f) << "object " << id;
            compared++;
        }
        EXPECT_GT(compared, 0);

        // Moving the head by as much as tracking noise leaves them exactly where they were, moving it further turns them.
        vr->setHeadShift(Vec3f(0.003f, 0, 0));
        game.tick(2);
        std::map<uint16_t, Vec3f> nudged = uprightSpriteAxes();
        vr->setHeadShift(Vec3f(0.2f, 0, 0));
        game.tick(2);
        std::map<uint16_t, Vec3f> leaned = uprightSpriteAxes();
        vr->setHeadShift(Vec3f());
        int still = 0;
        int followed = 0;
        for (const auto &[id, axis] : ahead) {
            if (nudged.contains(id)) {
                EXPECT_EQ(nudged.at(id), axis) << "object " << id;
                still++;
            }
            if (leaned.contains(id) && leaned.at(id) != axis)
                followed++;
        }
        EXPECT_GT(still, 0);
        EXPECT_GT(followed, 0);
    }
    phase("turning the head");

    // A menu hangs where the head looked when it opened and stays there, or follows the head lazily.
    if (vr) {
        auto uiYaw = [&] {
            std::optional<XrPosef> pose = vr->uiPose(); // Upright, only turned about the vertical.
            return pose ? static_cast<float>(2 * std::atan2(pose->orientation.y, pose->orientation.w) * 180 / M_PI) : 1000.0f;
        };
        engine->config->vr.UiFollow.setValue(VR_UI_FOLLOW_FIXED);
        vr->setHeadTurn(30, 0);
        game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
        game.tick(5);
        EXPECT_EQ(current_screen_type, SCREEN_MENU);
        EXPECT_NEAR(uiYaw(), 30, 0.5);
        vr->setHeadTurn(-30, 0);
        game.tick(5);
        EXPECT_NEAR(uiYaw(), 30, 0.5);
        engine->config->vr.UiFollow.setValue(VR_UI_FOLLOW_LAZY);
        game.tick(90); // Past the half second the lazy screen takes to ease back in front.
        EXPECT_NEAR(uiYaw(), -30, 0.5);
        engine->config->vr.UiFollow.setValue(VR_UI_FOLLOW_FIXED);
        vr->setHeadTurn(0, 0);
        game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
        game.tick(5);
        EXPECT_EQ(current_screen_type, SCREEN_GAME);
    }
    phase("a menu fixed in space");

    // Without position tracking, the way a headset that only tracks orientation runs.
    if (vr)
        vr->setPositionTracking(false);
    game.tick(10);
    if (vr) {
        EXPECT_GT(countFrames(vr->frameStats(), true, false), 0);
        vr->setPositionTracking(true);
    }
    phase("without position tracking");

    // A chest, opened with Space.
    game.teleportTo(MAP_EMERALD_ISLAND, Vec3f(8600, 5200, 1), startYaw);
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_SPACE);
    game.tick(30);
    EXPECT_EQ(current_screen_type, SCREEN_CHEST);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    phase("chest");

    // A book.
    game.pressAndReleaseKey(PlatformKey::KEY_Q);
    game.tick(30);
    EXPECT_EQ(current_screen_type, SCREEN_BOOKS);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    phase("book");

    // A house with its video, and its shop.
    engine->config->debug.NoVideo.setValue(false);
    game.teleportTo(MAP_TATALIA, Vec3f(19174, 15056, 3040), 0); // In front of the door of Vander's Blades & Bows.
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_SPACE);
    game.tick(100);
    EXPECT_EQ(current_screen_type, SCREEN_HOUSE);
    if (vr)
        EXPECT_GT(countFadeFrames(vr->frameStats()), 0); // The world fades to black behind the house.
    phase("entering a house");
    game.pressGuiButton("HouseDialogue_Option0");
    game.tick(30);
    if (vr)
        EXPECT_EQ(countFrames(vr->frameStats(), true, false), 0); // And stays black.
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(30);
    EXPECT_EQ(current_screen_type, SCREEN_GAME);
    engine->config->debug.NoVideo.setValue(true);
    phase("house and shop");

    // A level transition, through the door of Castle Harmondale.
    pParty->_questBits.set(QBIT_BUTLER_MET); // The castle door leads inside instead of to the butler.
    pParty->_questBits.set(QBIT_FALSE_LOREN_GIVEN);
    game.teleportTo(MAP_HARMONDALE, Vec3f(-18118, 12544, 480), 180); // In front of the castle door.
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_SPACE);
    game.tick(10);
    game.pressGuiButton("Transition_Yes");
    game.skipLoadingScreen();
    game.tick(30);
    EXPECT_EQ(engine->_currentLoadedMapId, MAP_CASTLE_HARMONDALE);
    phase("level transition");

    // Saving and loading.
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    game.pressGuiButton("GameMenu_SaveGame");
    game.tick(10);
    game.doubleClickGuiButton("SaveMenu_Slot0");
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_DIGIT_0);
    game.tick(2);
    game.pressGuiButton("SaveMenu_Save");
    game.tick(10);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    game.pressGuiButton("GameMenu_LoadGame");
    game.tick(10);
    game.pressGuiButton("LoadMenu_Slot0");
    game.tick(2);
    game.pressGuiButton("LoadMenu_Load");
    game.tick(2);
    game.skipLoadingScreen();
    game.tick(30);
    EXPECT_EQ(current_screen_type, SCREEN_GAME);
    phase("save and load");

    // Both pages of the VR block on the video options screen, spreading the HUD from the second one.
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_O);
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_O);
    game.tick(2);
    EXPECT_EQ(current_screen_type, SCREEN_VIDEO_OPTIONS);
    game.pressAndReleaseButton(BUTTON_LEFT, 350, 271); // More...
    game.tick(2);
    game.pressAndReleaseButton(BUTTON_LEFT, 350, 166); // HUD layout.
    game.tick(2);
    game.pressAndReleaseButton(BUTTON_LEFT, 350, 256); // Back.
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(2);
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(30);
    EXPECT_EQ(current_screen_type, SCREEN_GAME);
    if (vr) {
        EXPECT_EQ(engine->config->vr.HudLayout.value(), VR_HUD_LAYOUT_SPREAD);
        EXPECT_GT(countFrames(vr->frameStats(), true, true), 0);
    }

    // The spread HUD turns its cursor with the mouse over its parts around the head, across the gaps between them, and
    // clicks land where it is.
    if (vr) {
        ASSERT_TRUE(vr->pointHudCursorAt(Pointi(450, 30))); // Near the viewport's upper right corner.
        moveMouseBy(game, Pointi(100, -50)); // Up and right, into the gap before the minimap.
        game.tick(2);
        EXPECT_TRUE(Recti(469, 0, 171, 148).contains(mouse->position())) << mouse->position().x << ", " << mouse->position().y;
        moveMouseBy(game, Pointi(5000, 0)); // Far past everything to the right.
        game.tick(2);
        EXPECT_TRUE(vr->hudShows(mouse->position())) << mouse->position().x << ", " << mouse->position().y;
        ASSERT_TRUE(vr->pointHudCursorAt(pBtn_Rest->rect.center()));
        game.pressAndReleaseButton(BUTTON_LEFT, 0, 0); // Where the event says doesn't matter, the cursor is pinned.
        game.tick(5);
        EXPECT_EQ(current_screen_type, SCREEN_REST);
        game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
        game.tick(5);
        EXPECT_EQ(current_screen_type, SCREEN_GAME);
    }
    phase("options and the spread HUD");

    // Quitting to the main menu.
    game.pressAndReleaseKey(PlatformKey::KEY_ESCAPE);
    game.tick(10);
    game.pressGuiButton("GameMenu_Quit");
    game.tick(10);
    game.pressGuiButton("GameMenu_Quit");
    game.tick(30);
    EXPECT_EQ(GetCurrentMenuID(), MENU_MAIN);
    phase("quit");

    std::vector<std::string> lines = warnings.lines();
    std::ranges::sort(lines);
    if (!headset) {
        std::ofstream out(baseline, std::ios::binary | std::ios::trunc);
        for (const std::string &line : lines)
            out << line << '\n';
        ASSERT_TRUE(out.good()) << "Couldn't write " << baseline;
    } else {
        std::ifstream in(baseline, std::ios::binary);
        ASSERT_TRUE(in.good()) << "Couldn't read " << baseline << ", the flat pass writes it.";
        std::vector<std::string> flat;
        for (std::string line; std::getline(in, line);)
            flat.push_back(line);
        EXPECT_EQ(lines, flat);
    }
}
