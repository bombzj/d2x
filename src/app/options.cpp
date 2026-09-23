#include "options.hpp"
#include <charconv>
#include <filesystem>
#include <stdexcept>

namespace d2x {
AppOptions parseOptions(int argc, char **argv) {
    AppOptions options;
    bool explicitMpq = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() {
            if (++i >= argc)
                throw std::runtime_error("Missing value for " + arg);
            return std::string(argv[i]);
        };
        auto number = [&]() {
            auto text = value();
            size_t end = 0;
            int result = std::stoi(text, &end);
            if (end != text.size())
                throw std::runtime_error("Invalid number for " + arg);
            return result;
        };
        if (arg == "--mpq") {
            options.mpq = value();
            explicitMpq = true;
        } else if (arg == "--map")
            options.world.map = value();
        else if (arg == "--debug-pipe")
            options.debugPipe = value();
        else if (arg == "--debug-run")
            options.debugRun = true;
        else if (arg == "--level")
            options.world.level = number();
        else if (arg == "--preset")
            options.world.preset = number();
        else if (arg == "--level-type")
            options.world.levelType = number();
        else if (arg == "--variant")
            options.world.variant = number();
        else if (arg == "--maps")
            options.maps = true;
        else if (arg == "--screenshot")
            options.screenshot = value();
        else if (arg == "--pack")
            options.pack = value();
        else if (arg == "--save")
            options.save = value();
        else if (arg == "--load")
            options.load = value();
        else if (arg == "--frames")
            options.frameLimit = number();
        else if (arg == "--region")
            options.region = number();
        else if (arg == "--difficulty") {
            auto difficulty = value();
            if (difficulty == "normal")
                options.population.difficulty = 0;
            else if (difficulty == "nightmare")
                options.population.difficulty = 1;
            else if (difficulty == "hell")
                options.population.difficulty = 2;
            else
                throw std::runtime_error("--difficulty expects normal, nightmare or hell");
        } else if (arg == "--map-seed") {
            auto seed = value();
            auto [end, error] = std::from_chars(seed.data(), seed.data() + seed.size(), options.world.seed);
            if (error != std::errc{} || end != seed.data() + seed.size())
                throw std::runtime_error("--map-seed requires an unsigned 32-bit decimal integer");
        } else if (arg == "--population-seed") {
            auto seed = value();
            auto [end, error] =
                std::from_chars(seed.data(), seed.data() + seed.size(), options.population.seed);
            if (error != std::errc{} || end != seed.data() + seed.size())
                throw std::runtime_error("--population-seed requires an unsigned 32-bit decimal integer");
        } else if (arg == "--seed") {
            auto text = value();
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), options.lootSeed);
            if (error != std::errc{} || end != text.data() + text.size())
                throw std::runtime_error("--seed requires an unsigned 64-bit decimal integer");
        } else if (arg == "--hidden")
            options.hidden = true;
        else if (arg == "--stash")
            options.stash = true;
        else if (arg == "--inventory")
            options.inventory = true;
        else if (arg == "--skills")
            options.skills = true;
        else if (arg == "--help")
            options.help = true;
        else
            throw std::runtime_error("Unknown option: " + arg);
    }
    if (options.frameLimit < 0)
        throw std::runtime_error("--frames must not be negative");
    if (options.debugRun && options.debugPipe.empty())
        throw std::runtime_error("--debug-run requires --debug-pipe");
    if (options.world.variant < 0 || options.world.variant > 5)
        throw std::runtime_error("--variant must be 0..5 (original File1..File6 slots)");
    if (options.world.preset < 0 || (options.world.preset != 0) != (options.world.levelType > 0))
        throw std::runtime_error("--preset requires --level-type; both must be positive");
    if (!options.world.map.empty() && options.world.preset)
        throw std::runtime_error("Choose either --map or --preset");
    if (!explicitMpq) {
        auto discover = [](const std::filesystem::path &root) -> std::string {
            for (const auto *candidate :
                 {"assets/mpq2", "assets/mpq2/d2x-act1.mpq", "assets/mpq/d2x-mvp.mpq", "assets/mpq"}) {
                auto path = root / candidate;
                if (std::filesystem::is_regular_file(path) ||
                    (std::filesystem::is_directory(path) &&
                     (std::filesystem::is_regular_file(path / "d2data.mpq") ||
                      std::filesystem::is_regular_file(path / "D2Data.mpq"))))
                    return path.string();
            }
            return {};
        };
        auto discovered = discover(std::filesystem::current_path());
        auto directory = std::filesystem::absolute(argv[0]).parent_path();
        for (int depth = 0; discovered.empty() && depth < 5; ++depth) {
            discovered = discover(directory);
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
