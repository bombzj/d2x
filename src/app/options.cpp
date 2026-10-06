#include "options.hpp"
#include <raylib.h>
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <random>

namespace d2x {
AppOptions parseOptions(int argc, char **argv) {
    AppOptions options;
    bool explicitMpq = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() {
            if (++i >= argc) throw std::runtime_error("Missing value for " + arg);
            const std::string text = argv[i];
            if (text.empty()) throw std::runtime_error("Empty value for " + arg);
            return text;
        };
        if (arg == "--mpq") { options.mpq = value(); explicitMpq = true; }
        else if (arg == "--online-config") options.onlineConfig = value();
        else if (arg == "--debug-pipe") options.debugPipe = value();
        else if (arg == "--online-character") options.onlineCharacter = value();
        else if (arg == "--online-play") options.onlinePlay = value();
        else if (arg == "--online-create-game") options.onlineCreateGame = value();
        else if (arg == "--online-join-game") options.onlineJoinGame = value();
        else if (arg == "--screenshot") options.screenshot = value();
        else if (arg == "--pack") options.pack = value();
        else if (arg == "--frames") {
            const auto text = value();
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), options.frameLimit);
            if (error != std::errc{} || end != text.data() + text.size() || options.frameLimit < 0)
                throw std::runtime_error("--frames requires a nonnegative integer");
        }
        else if (arg == "--hidden") options.hidden = true;
        else if (arg == "--help") options.help = true;
        else if (arg == "--debug-run") { /* Legacy spelling: online always starts running. */ }
        else if (arg == "--save" || arg == "--load" || arg == "--class" || arg == "--seed" ||
                 arg == "--map-seed" || arg == "--population-seed" || arg == "--population" ||
                 arg == "--map" || arg == "--region" || arg == "--level" || arg == "--preset" ||
                 arg == "--level-type" || arg == "--variant" || arg == "--difficulty" ||
                 arg == "--inventory" || arg == "--stash" || arg == "--skills")
            throw std::runtime_error(arg + " is a retired local-game option; select a server character instead");
        else throw std::runtime_error("Unknown option: " + arg);
    }
    if (!options.onlinePlay.empty()) {
        if (!options.onlineCharacter.empty() || !options.onlineCreateGame.empty() || !options.onlineJoinGame.empty())
            throw std::runtime_error("--online-play cannot be combined with manual quick-entry options");
        options.onlineCharacter = options.onlinePlay;
        options.onlineCreateGame = "d2x" + std::to_string(std::random_device{}());
    }
    if (!options.onlineCreateGame.empty() && !options.onlineJoinGame.empty())
        throw std::runtime_error("Choose either --online-create-game or --online-join-game");
    if ((!options.onlineCreateGame.empty() || !options.onlineJoinGame.empty()) && options.onlineCharacter.empty())
        throw std::runtime_error("Quick game entry requires --online-character");
    if (!explicitMpq) {
        auto discover = [](const std::filesystem::path &path) -> std::string {
            if (std::filesystem::is_directory(path) &&
                (std::filesystem::is_regular_file(path / "d2data.mpq") ||
                 std::filesystem::is_regular_file(path / "D2Data.mpq")))
                return path.string();
            return {};
        };
        auto workingDirectory = std::filesystem::current_path();
        auto directory = std::filesystem::path(GetApplicationDirectory());
        if (!directory.has_filename())
            directory = directory.parent_path();
        auto discovered = discover(workingDirectory);
        if (discovered.empty())
            discovered = discover(directory);
        if (discovered.empty())
            discovered = discover(workingDirectory / "assets/mpq2");
        for (int depth = 0; discovered.empty() && depth < 5; ++depth) {
            discovered = discover(directory / "assets/mpq2");
            if (directory == directory.parent_path())
                break;
            directory = directory.parent_path();
        }
        if (!discovered.empty())
            options.mpq = discovered;
    }
    return options;
}
} // namespace d2x
