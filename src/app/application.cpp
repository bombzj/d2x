#include "application.hpp"
#include "input.hpp"
#include "options.hpp"
#include "debug_pipe.hpp"
#include "debug_commands.hpp"
#include "persistence/save_file.hpp"
#include "presentation/controller.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace d2x {
namespace {
class Platform {
  public:
    explicit Platform(bool hidden) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | (hidden ? FLAG_WINDOW_HIDDEN : 0));
        InitWindow(1280, 816, "D2X - Diablo II Classic / C++");
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
                     "--map-seed <uint32> --seed <uint64> --inventory --skills --stash --screenshot <png> "
                     "--frames N --hidden --pack "
                     "<new.mpq> --save <file.d2xsave> --load <file.d2xsave>\n"
                     "--debug-pipe <name>: opt-in local Windows debug commands (starts paused).\n"
                     "--debug-run: start the debug-enabled game without pausing.\n"
                     "F11: save; Ctrl+F11: load. Default slot: saves/quick.d2xsave\n";
        return 0;
    }
    Archives archives;
    archives.mountDirectory(options.mpq);
    if (archives.names.empty())
        throw std::runtime_error("No MPQs found. Supply --mpq <classic-game-folder|archive>.");
    // Declaration order guarantees GPU/audio resources die before their devices.
    Platform platform(options.hidden);
    BeginDrawing();
    ClearBackground({9, 11, 10, 255});
    DrawText("D2X", GetScreenWidth() / 2 - 40, GetScreenHeight() / 2 - 40, 36, gold);
    DrawText("Loading classic maps and animations...", GetScreenWidth() / 2 - 180, GetScreenHeight() / 2 + 20,
             18, parchment);
    EndDrawing();
    std::optional<SessionSnapshot> restored;
    if (!options.load.empty()) {
        restored = loadSave(options.load);
        options.world.seed = restored->world.mapSeed;
        options.population = restored->world.population;
    }
    GameSession session(archives, options.world, options.region, options.lootSeed, options.population);
    std::cout << "MPQ data: " << session.content().profile << ", "
              << session.inventory().catalog().entries().size() << " items, "
              << session.content().monsters.size() << " monsters, " << session.content().treasures.size()
              << " treasure classes\n"
              << LootSystem::unavailableReason << '\n';
    if (!options.load.empty()) {
        session.restore(std::move(*restored));
        std::cout << "Loaded " << options.load << '\n';
    }
    const std::string savePath = !options.save.empty()   ? options.save
                                 : !options.load.empty() ? options.load
                                                         : "saves/quick.d2xsave";
    SceneView view(archives, session);
    view.ui().travelMenu = options.maps;
    view.ui().inventory.open = options.inventory;
    if (options.skills && !options.inventory && !options.stash && !options.maps)
        view.ui().skillPicker = true;
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
    RenderTarget target;
    DebugPipe debugPipe(options.debugPipe);
    bool debugPaused = !options.debugPipe.empty() && !options.debugRun, debugQuit = false;
    if (!options.debugPipe.empty() && options.hidden)
        SetTargetFPS(60);
    if (!options.debugPipe.empty())
        std::cout << "Debug pipe ready: " << options.debugPipe
                  << (debugPaused ? " (paused)\n" : " (running)\n") << std::flush;
    float accumulator = 0;
    int frames = 0;
    while (!WindowShouldClose()) {
        debugPipe.poll([&](const std::string &request) {
            return debugCommand(request, session, view, debugPaused, debugQuit, savePath,
                                [&](const std::string &path) { target.save(path); });
        });
        if (debugQuit)
            break;
        float dt = std::min(GetFrameTime(), .1f);
        auto viewport = currentViewport();
        auto input = pollInput(viewport);
        bool persistenceInput = input.focused && (input.save || input.load);
        if (persistenceInput) {
            try {
                if (input.load) {
                    session.restore(loadSave(savePath));
                    view.sessionRestored();
                    controller.resetInput();
                    accumulator = 0;
                    view.notice("Loaded game.");
                } else {
                    writeSave(savePath, session.snapshot());
                    view.notice("Game saved.");
                }
                std::cout << (input.load ? "Loaded " : "Saved ") << savePath << '\n';
            } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                view.notice(error.what(), true);
            }
        } else if (!controller.handle(input, dt))
            break;
        if (view.ui().blocksWorld() || persistenceInput || debugPaused)
            accumulator = 0;
        else
            accumulator += dt;
        while (accumulator >= GameSession::fixedStep && !view.ui().blocksWorld()) {
            session.tick(GameSession::fixedStep, controller.movement());
            view.advance(GameSession::fixedStep);
            accumulator -= GameSession::fixedStep;
        }
        BeginTextureMode(target.handle);
        view.draw(input.mouse);
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.handle.texture, {0, 0, float(W), -float(H)},
                       {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale}, {0, 0},
                       0, WHITE);
        EndDrawing();
        if (input.screenshot)
            target.save("artifacts/d2x-capture.png");
        if (options.frameLimit > 0 && ++frames >= options.frameLimit)
            break;
    }
    if (!options.screenshot.empty())
        target.save(options.screenshot);
    if (!options.save.empty()) {
        writeSave(options.save, session.snapshot());
        std::cout << "Saved " << options.save << '\n';
    }
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
    return 0;
}
} // namespace d2x
