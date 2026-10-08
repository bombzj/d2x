#include "application.hpp"
#include "frontend.hpp"
#include "options.hpp"
#include "input.hpp"
#include "presentation/graphics/graphics.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace d2x {
namespace {
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
        SetTargetFPS(60);
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
    const auto options = parseOptions(argc, argv);
    if (options.help) {
        std::cout << "D2X: Single Player characters / original-server client\n"
                     "Single Player lists local D2S characters; F11 saves, Ctrl+F11 reloads in town.\n"
                     "--mpq <folder|archive> --online-config <file.json>\n"
                     "--host-lan <IPv4 interface> --host-saves <folder>: shared MCP/D2GS host.\n"
                     "--lan <host IPv4>: connect directly to its native character lobby.\n"
                     "--realm-port <port> --game-port <port>: defaults 6113 / 4000.\n"
                     "--load <character.d2s> | --class <MPQ class>; --save <new character.d2s>\n"
                     "--debug-pipe <name>: opt-in local command interface; starts running.\n"
                     "pause/resume freeze client presentation only; the server and network keep running.\n"
                     "--online-character <name> --online-create-game <name> | --online-join-game <name>\n"
                     "--online-play <character> logs in once and creates a uniquely named normal game.\n"
                     "Quick entry uses remembered credentials and the normal server handshake once.\n"
                     "--hidden --frames N --screenshot <png> --pack <new.mpq>\n";
        return 0;
    }
    Archives archives;
    archives.mountDirectory(options.mpq);
    if (archives.names.empty())
        throw std::runtime_error("No MPQs found. Supply --mpq <Lord-of-Destruction-folder|archive>.");
    Platform platform(options.hidden);
    RenderTarget target;
    runFrontend(archives, target.handle, options);
    if (!options.screenshot.empty()) target.save(options.screenshot);
    std::filesystem::create_directories("artifacts");
    std::ofstream manifest("artifacts/mvp-manifest.txt");
    for (const auto &name : archives.used) manifest << name << '\n';
    if (!manifest) throw std::runtime_error("Cannot write resource manifest");
    if (!options.pack.empty()) archives.packUsed(options.pack);
    return 0;
}
} // namespace d2x
