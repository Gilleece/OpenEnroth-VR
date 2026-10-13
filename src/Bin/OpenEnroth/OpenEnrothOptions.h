#pragma once

#include <string>
#include <vector>

#include "Application/Startup/GameStarterOptions.h"

#include "Utility/System/Path.h"

class GameConfig;
class Platform;

struct OpenEnrothOptions : public GameStarterOptions {
    enum class Subcommand {
        SUBCOMMAND_GAME,
        SUBCOMMAND_PLAY,
        SUBCOMMAND_RETRACE,
        SUBCOMMAND_DUMP_SPRITE_PALETTES,
        SUBCOMMAND_BAKE_GI
    };
    using enum Subcommand;

    struct RetraceOptions {
        std::vector<Path> traces;
    };

    struct DumpSpritePalettesOptions {
        Path output;
    };

    struct BakeGiOptions {
        Path out; // Texture pack folder, the probe files go into its `gi` folder.
        std::vector<std::string> maps; // Map files in games.lod, all of them if empty.
        Path report;
    };

    struct PlayOptions {
        std::vector<Path> traces;
        float speed = 1.0f;
    };

    Subcommand subcommand = SUBCOMMAND_GAME;
    bool helpPrinted = false; // True means that help message was already printed.
    RetraceOptions retrace;
    PlayOptions play;
    DumpSpritePalettesOptions dumpSpritePalettes;
    BakeGiOptions bakeGi;

    /**
     * Parses OpenEnroth command line options.
     *
     * @param argc                      argc as passed to main.
     * @param argv                      argv as passed to main.
     * @throw std::exception            On errors.
     */
    static OpenEnrothOptions parse(int argc, char **argv);
};
