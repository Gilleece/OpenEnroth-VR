#include <string>
#include <string_view>

#include "Testing/Unit/UnitTest.h"

#include "Application/GameConfig.h"

#include "Utility/Memory/Blob.h"
#include "Utility/Streams/BlobOutputStream.h"
#include "Utility/Streams/MemoryInputStream.h"

static void loadConfig(GameConfig *config, std::string_view ini) {
    MemoryInputStream stream(ini.data(), ini.size());
    config->load(&stream);
}

static std::string saveConfig(const GameConfig &config) {
    Blob result;
    BlobOutputStream stream(&result);
    config.save(&stream);
    stream.close();
    return std::string(result.str());
}

UNIT_TEST(GameConfig, PresetKeepsExplicitOff) {
    // An ini with enhanced on and global_illumination off got the indirect light anyway, the preset took the default
    // value for an unset one.
    GameConfig config;
    loadConfig(&config, "; Entries that are commented out are at their default value.\n[graphics]\nenhanced = true\nglobal_illumination = false\n");
    config.updateEnhancedGraphics();
    EXPECT_FALSE(config.graphics.GlobalIllumination.value());
    EXPECT_TRUE(config.graphics.SunShadows.value());
}

UNIT_TEST(GameConfig, PresetFillsUnsetEntries) {
    // The preset turns on what the ini doesn't mention, also in inis written before entries at their default were
    // commented out.
    for (std::string_view header : {"; Entries that are commented out are at their default value.\n", ""}) {
        GameConfig config;
        loadConfig(&config, std::string(header) + "[graphics]\nenhanced = true\n; global_illumination = false\nsun_shadows = false\n");
        config.updateEnhancedGraphics();
        EXPECT_TRUE(config.graphics.GlobalIllumination.value());
        EXPECT_EQ(config.graphics.SunShadows.value(), header.empty()); // Older inis wrote every entry, at its default too.
    }
}

UNIT_TEST(GameConfig, TogglesSurviveRestart) {
    // The video options toggles didn't keep a feature off across a restart with enhanced on.
    GameConfig first;
    loadConfig(&first, "; Entries that are commented out are at their default value.\n[graphics]\nenhanced = true\nsprite_depth = true\nsprite_crossfade = true\n");
    first.updateEnhancedGraphics();
    ASSERT_TRUE(first.graphics.GlobalIllumination.value());
    first.graphics.GlobalIllumination.toggle();
    first.graphics.SpriteDepth.toggle();
    first.graphics.SpriteCrossfade.toggle();
    first.removeEnhancedGraphics();
    std::string saved = saveConfig(first);

    GameConfig second;
    loadConfig(&second, saved);
    second.updateEnhancedGraphics();
    EXPECT_FALSE(second.graphics.GlobalIllumination.value());
    EXPECT_FALSE(second.graphics.SpriteDepth.value());
    EXPECT_FALSE(second.graphics.SpriteCrossfade.value());
    EXPECT_TRUE(second.graphics.SunShadows.value());
}
