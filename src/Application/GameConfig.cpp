#include "GameConfig.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include "Library/Logger/Logger.h"

GameConfig::GameConfig() = default;
GameConfig::~GameConfig() = default;

void GameConfig::updateEnhancedGraphics() {
    if (!graphics.Enhanced.value()) {
        removeEnhancedGraphics();
        return;
    }

    auto apply = [&](auto &entry, auto value) {
        bool applied = std::ranges::contains(_enhancedEntries, &entry, [](const auto &pair) { return pair.first; });
        if (applied || entry.value() != entry.defaultValue())
            return;
        entry.setValue(value);
        _enhancedEntries.emplace_back(&entry, entry.string());
    };
    apply(graphics.LinearPipeline, true);
    apply(graphics.Bloom, true);
    apply(graphics.Antialiasing, ANTIALIASING_FXAA);
    apply(graphics.SunShadows, true);
    apply(graphics.TorchShadows, true);
    apply(graphics.AmbientOcclusion, true);
    apply(graphics.TerrainVariation, true);
    apply(graphics.GlobalIllumination, true);
    apply(graphics.WorldSpaceBillboards, true);
    apply(graphics.UiScale, 2);
}

void GameConfig::removeEnhancedGraphics() {
    for (const auto &[entry, value] : _enhancedEntries)
        if (entry->string() == value)
            entry->reset();
    _enhancedEntries.clear();
}

GameConfig::CheatCommands::CheatCommands(GameConfig *config) : ConfigSection(config, "cheat_commands") {
    // All 40 slots are empty by default. Built-in commands are defined in
    // resources/scripts/dev/cheat_commands_list.lua so they can be reordered
    // without touching config or renumbering entries.
    static const int maxNumberOfCommands = 40;
    for (int i = 0; i < maxNumberOfCommands; ++i) {
        _addCommand(i, "");
    }
}

void GameConfig::CheatCommands::_addCommand(int commandIndex, const std::string& defaultValue) {
    std::string name = fmt::format("command{:02}", commandIndex + 1);
    auto item = std::make_unique<String>(this, name, defaultValue, fmt::format("Cheat command #{}.", commandIndex + 1));
    _commandList.push_back(std::move(item));
}
