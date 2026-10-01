#include "presentation/scene_view.hpp"
#include "gameplay/session/session.hpp"
#include "application.hpp"
#include "character_frontend.hpp"
#include "input.hpp"
#include "options.hpp"
#include "core/random.hpp"
#include "core/random_seed.hpp"
#include "app/debug/debug_pipe.hpp"
#include "app/debug/debug_commands.hpp"
#include "persistence/save_file.hpp"
#include "presentation/controller.hpp"
#include "presentation/graphics/graphics.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <span>

namespace d2x {
namespace {
struct ClientPreferences {
    bool running = false;
    bool miniPanelOpen = false;
};
constexpr auto preferencesPath = "client-settings.json";
ClientPreferences loadClientPreferences() {
    std::ifstream file(preferencesPath);
    if (!file) return {};
    try {
        const auto settings = nlohmann::json::parse(file);
        if (!settings.is_object()) {
            std::cerr << "Invalid client settings: expected an object\n";
            return {};
        }
        auto setting = [&](const char *key) {
            const auto found = settings.find(key);
            if (found == settings.end()) return false;
            if (found->is_boolean()) return found->get<bool>();
            std::cerr << "Invalid client setting: " << key << " must be boolean\n";
            return false;
        };
        return {setting("running"), setting("miniPanelOpen")};
    } catch (const nlohmann::json::exception &error) {
        std::cerr << "Invalid client settings: " << error.what() << '\n';
        return {};
    }
}
bool saveClientPreferences(const ClientPreferences &preferences) {
    try {
        const auto text = nlohmann::json{{"running", preferences.running},
                                       {"miniPanelOpen", preferences.miniPanelOpen}}.dump(2);
        writeFileAtomically(preferencesPath,
            std::span<const uint8_t>{reinterpret_cast<const uint8_t *>(text.data()), text.size()});
        return true;
    } catch (const std::exception &error) {
        std::cerr << "Cannot write client settings: " << error.what() << '\n';
        return false;
    }
}
class Platform {
  public:
    explicit Platform(bool hidden) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | (hidden ? FLAG_WINDOW_HIDDEN : 0));
        InitWindow(1280, 816, "D2X - Diablo II Expansion / C++");
        if (!IsWindowReady())
            throw std::runtime_error("Unable to create OpenGL window");
        SetWindowMinSize(800, 510);
        SetExitKey(KEY_NULL);
        SetTargetFPS(hidden ? 0 : 60);
        if (!hidden) {
            HideCursor();
            InitAudioDevice();
        }
    }
    Platform(const Platform &) = delete;
    Platform &operator=(const Platform &) = delete;
    ~Platform() {
        if (IsAudioDeviceReady())
            CloseAudioDevice();
        CloseWindow();
    }
};
class RenderTarget {
  public:
    RenderTexture2D handle{};
    RenderTarget() : handle(LoadRenderTexture(W, H)) {
        if (!handle.id)
            throw std::runtime_error("Unable to allocate scene render target");
        SetTextureFilter(handle.texture, TEXTURE_FILTER_POINT);
    }
    RenderTarget(const RenderTarget &) = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;
    ~RenderTarget() { UnloadRenderTexture(handle); }
    void save(const std::string &path) const {
        if (std::filesystem::path(path).has_parent_path())
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        Image image = LoadImageFromTexture(handle.texture);
        ImageFlipVertical(&image);
        bool ok = ExportImage(image, path.c_str());
        UnloadImage(image);
        if (!ok)
            throw std::runtime_error("Screenshot export failed");
    }
};
} // namespace
int runGame(int argc, char **argv) {
    auto options = parseOptions(argc, argv);
    if (options.help) {
        std::cout << "D2X: --mpq <folder|archive> --level <Act I ID> --variant <0-5> --maps "
                     "--preset <LvlPrest Def> --level-type <LvlTypes ID> "
                     "--map <complete preset DS1> --region <scene index> "
                     "--difficulty <normal|nightmare|hell> --population-seed <uint32> "
                     "--map-seed <uint32> --seed <uint32> --inventory --skills --stash --screenshot <png> "
                     "--frames N --hidden --pack "
                     "<new.mpq> --save <file.d2s> --load <file.d2s>\n"
                     "--class <MPQ class name>: start a new character directly without the frontend or a save.\n"
                     "Choose --class or --load, not both. New direct characters auto-save only with --save.\n"
                     "--seed <uint32>: reproducible whole-game root; default is a fresh seed per game.\n"
                     "--map-seed and --population-seed override individual plans; D2S keeps its saved map seed.\n"
                     "--debug-pipe <name>: opt-in local Windows debug commands (starts paused).\n"
                     "--debug-run: start the debug-enabled game without pausing.\n"
                     "F11: save character; Ctrl+F11: start a new game in town from save. "
                     "Default slot: saves/quick.d2s\n";
        return 0;
    }
    Archives archives;
    archives.mountDirectory(options.mpq);
    if (archives.names.empty())
        throw std::runtime_error("No MPQs found. Supply --mpq <Lord-of-Destruction-folder|archive>.");
    // Declaration order guarantees GPU/audio resources die before their devices.
    Platform platform(options.hidden);
    std::optional<Graphics> loadingGraphics;
    GpuAnimation loadingFrames;
    if (!options.hidden) {
        loadingGraphics.emplace(archives, "data/global/palette/loading/pal.dat");
        loadingFrames = loadingGraphics->single("data/global/ui/loading/loadingscreen.dc6");
        if (loadingFrames.frames.size() < 2)
            throw std::runtime_error("Original MPQ loading screen is missing or incomplete");
    }
    auto beginLoading = [&]() {
        if (options.hidden) return;
        archives.setLoadingPulse([&, started = GetTime(), lastDraw = -1.0]() mutable {
            double now = GetTime();
            if (now - lastDraw < 1.0 / 30.0) return;
            lastDraw = now;
            const int index = std::min(int((now - started) * 3), loadingFrames.count - 1);
            const auto *frame = loadingFrames.frame(0, index);
            const auto viewport = currentViewport();
            const auto &texture = frame->texture;
            const float width = texture.width * viewport.scale;
            const float height = texture.height * viewport.scale;
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           {(GetScreenWidth() - width) * .5f, (GetScreenHeight() - height) * .5f,
                            width, height}, {0, 0}, 0, WHITE);
            EndDrawing();
        });
        archives.pulseLoading();
    };
    ClientPreferences preferences = loadClientPreferences();
    bool preferencesDirty = false;
    double preferencesRetryAt = 0;
    for (;;) {
        const bool returnToCharacters = [&]() {
            beginLoading();
            std::optional<CharacterSaveData> restored;
            std::optional<CharacterChoice> character;
            std::optional<RenderTarget> frontendTarget;
            const bool frontend = !options.directGame;
            if (frontend) {
                std::cout << "Character frontend: preparing\n" << std::flush;
                frontendTarget.emplace();
                archives.setLoadingPulse({});
                ShowCursor();
                character = chooseCharacter(archives, frontendTarget->handle);
                if (!character)
                    return false;
                HideCursor();
                beginLoading();
            }
            if (character)
                options.load = character->created ? std::string{} : character->path.string();
            const uint32_t rootSeed = options.seed ? *options.seed : freshSeed();
            uint64_t random = initialRandom(rootSeed);
            const uint32_t mapSeed = rollRandom(random), populationSeed = rollRandom(random);
            const uint32_t sessionSeed = rollRandom(random);
            if (!options.mapSeedExplicit) options.world.seed = mapSeed;
            if (!options.populationSeedExplicit) options.population.seed = populationSeed;
            if (!options.load.empty()) {
                restored = loadSave(options.load, loadClassicData(archives));
                options.world.seed = restored->mapSeed;
                options.population.difficulty = restored->difficulty;
            }
            GameSession session(archives, options.world, options.region, sessionSeed, options.population,
                                character                        ? character->characterClass
                                : restored                       ? restored->player.characterClass
                                : options.characterClass.empty() ? "Barbarian"
                                                                 : options.characterClass,
                                character  ? character->name
                                : restored ? restored->player.name
                                           : "Hero");
            std::cout << "Game seed=" << rootSeed << " map=" << options.world.seed
                      << " population=" << options.population.seed << " session=" << sessionSeed << '\n';
            std::cout << "MPQ data: " << session.content().profile << ", "
                      << session.inventory().catalog().entries().size() << " items, "
                      << session.content().monsters.size() << " monsters, "
                      << session.content().treasures.size() << " treasure classes\n"
                      << LootSystem::unavailableReason << '\n';
            if (!options.load.empty()) {
                session.restore(std::move(*restored));
                std::cout << "Loaded " << options.load << '\n';
            }
            session.setRunning(preferences.running);
            const std::string savePath = character               ? character->path.string()
                                         : !options.save.empty() ? options.save
                                         : !options.load.empty() ? options.load
                                                                 : "saves/quick.d2s";
            if (character && character->created)
                writeSave(savePath, session.characterSave(), session.content());
            frontendTarget.reset();
            SceneView view(archives, session);
            view.ui().miniPanelOpen = preferences.miniPanelOpen;
            auto syncPreferences = [&](bool force = false) {
                if (preferences.running != session.state().player.running ||
                    preferences.miniPanelOpen != view.ui().miniPanelOpen) {
                    preferences = {session.state().player.running, view.ui().miniPanelOpen};
                    preferencesDirty = true;
                }
                if (!preferencesDirty || (!force && GetTime() < preferencesRetryAt)) return;
                preferencesDirty = !saveClientPreferences(preferences);
                preferencesRetryAt = GetTime() + 2;
            };
            view.ui().travelMenu = options.maps;
            view.ui().inventory.open = options.inventory;
            if (options.skills && !options.inventory && !options.stash && !options.maps)
                view.ui().skillTreeOpen = true;
            if (options.stash) {
                bool found = false;
                for (const auto &object : session.region().objects)
                    if (object.interaction == Interaction::Stash) {
                        session.submit(Interact{object.id});
                        found = true;
                        break;
                    }
                if (!found)
                    throw std::runtime_error("--stash requires a region with an original stash object");
            }
            SceneController controller(session, view);
            if (frontend) controller.resetInput();
            RenderTarget target;
            archives.setLoadingPulse({});
            DebugPipe debugPipe(options.debugPipe);
            bool debugPaused = !options.debugPipe.empty() && !options.debugRun, debugQuit = false;
            if (!options.debugPipe.empty() && options.hidden)
                SetTargetFPS(60);
            if (!options.debugPipe.empty())
                std::cout << "Debug pipe ready: " << options.debugPipe
                          << (debugPaused ? " (paused)\n" : " (running)\n") << std::flush;
            float accumulator = 0;
            int frames = 0;
            bool returningToCharacters = false;
            const bool ownsSave = character.has_value() || !options.save.empty() || !options.load.empty();
            auto saveBeforeExit = [&]() {
                try {
                    if (session.hasPendingCommands()) {
                        session.tick(0);
                        view.advance(0);
                    }
                    if (ownsSave) {
                        writeSave(savePath, session.characterSave(), session.content());
                        std::cout << "Saved " << savePath << '\n' << std::flush;
                    }
                    return true;
                } catch (const std::exception &error) {
                    std::cerr << error.what() << '\n';
                    view.notice(std::string("Save failed; game kept open: ") + error.what(), true);
                    return false;
                }
            };
            std::optional<FrameInput> debugInput;
            while (true) {
                bool exitRequested = WindowShouldClose();
                debugPipe.poll([&](const std::string &request) {
                    return debugCommand(
                        request, session, view, debugPaused, debugQuit, savePath,
                        [&](const std::string &path) {
                            target.save(path);
                        },
                        [&](FrameInput input) {
                            if (debugInput)
                                throw std::runtime_error("UI input already queued for this frame");
                            debugInput = std::move(input);
                        });
                });
                exitRequested |= debugQuit;
                if (exitRequested) {
                    if (saveBeforeExit()) break;
                    debugQuit = false;
                }
                float dt = std::min(GetFrameTime(), .1f);
                auto viewport = currentViewport();
                auto input = pollInput(viewport);
                if (debugInput) {
                    input = std::move(*debugInput);
                    debugInput.reset();
                }
                bool persistenceInput = input.focused && !view.ui().gameMenuOpen && (input.save || input.load);
                if (persistenceInput) {
                    try {
                        if (input.load) {
                            session.restore(loadSave(savePath, session.content()));
                            session.setRunning(preferences.running);
                            view.sessionRestored();
                            controller.resetInput();
                            accumulator = 0;
                            view.notice("Character loaded in town; monsters have reset.");
                        } else {
                            writeSave(savePath, session.characterSave(), session.content());
                            view.notice("Character saved.");
                        }
                        std::cout << (input.load ? "Loaded " : "Saved ") << savePath << '\n';
                    } catch (const std::exception &error) {
                        std::cerr << error.what() << '\n';
                        view.notice(error.what(), true);
                    }
                } else if (!controller.handle(input, dt)) {
                    if (saveBeforeExit()) {
                        returningToCharacters = true;
                        std::cout << "Character session: returning to list\n" << std::flush;
                        break;
                    }
                }
                // Modal windows suspend world time, but their commands still have to
                // commit and publish events before this frame is drawn.
                if (session.hasPendingCommands()) {
                    session.tick(0);
                    view.advance(0);
                }
                syncPreferences();
                view.advanceUi(dt, persistenceInput || debugPaused);
                if (view.ui().blocksWorld() || persistenceInput || debugPaused)
                    accumulator = 0;
                else
                    accumulator += dt;
                while (accumulator >= GameSession::fixedStep && !view.ui().blocksWorld()) {
                    session.tick(GameSession::fixedStep, controller.movement(), controller.temporaryRun());
                    view.advance(GameSession::fixedStep);
                    accumulator -= GameSession::fixedStep;
                }
                view.ui().combatTarget = controller.combatTarget();
                BeginTextureMode(target.handle);
                view.draw(input.mouse);
                EndTextureMode();
                BeginDrawing();
                ClearBackground(BLACK);
                DrawTexturePro(target.handle.texture, {0, 0, float(W), -float(H)},
                               {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale},
                               {0, 0}, 0, WHITE);
                EndDrawing();
                if (input.screenshot)
                    target.save("artifacts/d2x-capture.png");
                if (options.frameLimit > 0 && ++frames >= options.frameLimit) {
                    if (saveBeforeExit()) break;
                    options.frameLimit = 0;
                }
            }
            syncPreferences(true);
            if (returningToCharacters)
                std::cout << "Character session: finalizing resources\n" << std::flush;
            if (!options.screenshot.empty())
                target.save(options.screenshot);
            if (!options.pack.empty())
                view.collectMapVariants(archives);
            std::filesystem::create_directories("artifacts");
            std::ofstream manifest("artifacts/mvp-manifest.txt");
            for (const auto &name : archives.used)
                manifest << name << '\n';
            if (!manifest)
                throw std::runtime_error("Cannot write resource manifest");
            if (!options.pack.empty())
                archives.packUsed(options.pack);
            if (returningToCharacters)
                std::cout << "Character session: releasing resources\n" << std::flush;
            return returningToCharacters;
        }();
        if (!returnToCharacters)
            return 0;
        std::cout << "Character session: released\n" << std::flush;
        AppOptions next;
        next.mpq = options.mpq;
        next.hidden = options.hidden;
        next.debugPipe = options.debugPipe;
        next.debugRun = options.debugRun;
        options = std::move(next);
    }
}
} // namespace d2x
