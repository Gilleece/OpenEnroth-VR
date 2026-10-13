#include "GameTestOptions.h"

#include <memory>
#include <string>

#include "Library/Cli/CliApp.h"
#include "Library/Environment/Interface/Environment.h"

GameTestOptions GameTestOptions::parse(int argc, char **argv) {
    GameTestOptions result;
    result.ramFsUserData = true; // We want reproducible tests, so shouldn't depend on external user data.
    result.quickStart = true;
    std::optional<std::string> testPath;

    std::unique_ptr<CliApp> app = std::make_unique<CliApp>();

    std::string requiredOptions = "Required Options";
    std::string otherOptions = "Other Options";

    auto testPathOption = app->add_option("--test-path", testPath,
                                          "Path to test data dir.")->check(CLI::ExistingDirectory)->option_text("PATH")->group(requiredOptions);
    app->add_option(
        "--data-path", result.dataPath,
        "Path to game data dir.")->check(CLI::ExistingDirectory)->option_text("PATH")->group(otherOptions);
    app->add_flag(
        "--headless", result.headless,
        "Run in headless mode.")->group(otherOptions);
    app->add_option(
        "--speed", result.speed,
        "Playback speed, default is infinite, use '1.0' for realtime playback.")->option_text("SPEED");
    app->add_flag(
        "--tracing-rng", result.tracingRng,
        "Use random number generators that print stack trace on each call.")->group(otherOptions);
    app->add_option(
        "--log-level", result.logLevel,
        "Log level, one of 'none', 'trace', 'debug', 'info', 'warning', 'error', 'critical'.")->option_text("LOG_LEVEL");
    app->add_flag_callback(
        "-v,--verbose", [&] { result.logLevel = LOG_TRACE; },
        "Set log level to 'trace'.");
    app->set_help_flag("-h,--help", "Print help and exit.")->group(otherOptions);
    app->add_flag(
        "--gtest_list_tests", result.listRequested,
        "List the names of all tests instead of running them.")->group(""); // group("") hides the option. It's shown in gtest's help.
    app->allow_extras();

    app->parse(argc, argv, result.helpPrinted);

    if (!result.listRequested && !result.helpPrinted && !testPath)
        throw CLI::RequiredError(testPathOption->get_name());
    result.testPath = Path(testPath.value_or(""));

#ifdef OE_BUILD_VR
    // Run_VrSmoke runs the VrSmoke suite twice, flat and then in a headset session on the runtime OPENENROTH_VR_SMOKE names.
    std::unique_ptr<Environment> environment = Environment::createStandardEnvironment();
    std::string runtime = environment->getenv("OPENENROTH_VR_SMOKE");
    std::string pass = environment->getenv("OPENENROTH_VR_SMOKE_PASS");
    if (!runtime.empty() && !pass.empty()) {
        result.enhanced = true;
        result.vr = pass == "vr";
        if (result.vr)
            environment->setenv("XR_RUNTIME_JSON", runtime);
    }
#endif

    return result;
}
