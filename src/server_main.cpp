#include "hosting/pvpgn_server.hpp"
#include "resources/archive.hpp"
#include <nlohmann/json.hpp>
#include <csignal>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <ctime>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void stopSignal(int) { interrupted = 1; }
void logLine(std::string_view message) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " " << message << std::endl;
}
}
int main(int argc, char **argv) {
    d2x::Archives archives;
    std::unique_ptr<d2x::PvpgnServer> server;
    try {
        if (argc != 3 || std::string_view(argv[1]) != "--config") {
            std::cout << "D2X PvPGN game server\nUsage: d2x_server --config <server.json>\n";
            return 2;
        }
        const auto configPath = std::filesystem::absolute(argv[2]);
        std::ifstream input(configPath);
        if (!input) throw std::runtime_error("Cannot open server configuration");
        const auto config = nlohmann::json::parse(input);
        const auto path = [&](std::string_view key) {
            auto value = std::filesystem::path(config.at(std::string(key)).get<std::string>());
            if (value.empty()) throw std::runtime_error("Empty server path");
            return value.is_absolute() ? value : configPath.parent_path() / value;
        };
        const auto endpoint = [&](std::string_view key) {
            const auto &value = config.at(std::string(key));
            const auto port = value.at("port").get<int>();
            if (port < 1 || port > 65535) throw std::runtime_error("Server port is outside 1-65535");
            return d2x::net::Endpoint{value.at("host").get<std::string>(), uint16_t(port)};
        };
        d2x::PvpgnServerOptions options;
        options.d2cs = endpoint("d2cs"); options.d2dbs = endpoint("d2dbs"); options.listen = endpoint("listen");
        options.realm = config.value("realm", std::string{});
        options.version = config.value("version", uint32_t{});
        options.maximumGames = config.value("maximumGames", uint32_t{16});
        options.recovery = path("recovery");
        const auto stopFile = path("stopFile");
        if (std::filesystem::exists(stopFile)) throw std::runtime_error("Stale stop request exists; remove it before starting");
        std::filesystem::create_directories(stopFile.parent_path());
        std::signal(SIGINT, stopSignal); std::signal(SIGTERM, stopSignal);
        logLine("Loading original MPQ content; no window or audio device will be created");
        archives.mountDirectory(path("mpq"));
        if (archives.names.empty()) throw std::runtime_error("No original MPQs mounted");
        server = std::make_unique<d2x::PvpgnServer>(archives, std::move(options), logLine);
        server->start();
        auto previous = std::chrono::steady_clock::now();
        while (!interrupted && !server->stopRequested() && !std::filesystem::exists(stopFile)) {
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - previous).count(); previous = now;
            server->pump(seconds);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        logLine("Graceful shutdown requested"); server->shutdown();
        std::error_code ignored; std::filesystem::remove(stopFile, ignored);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Server error: " << error.what() << std::endl;
        if (server) {
            try { server->shutdown(); }
            catch (const std::exception &saveError) { std::cerr << "Shutdown incomplete: " << saveError.what() << "; retain recovery files and inspect D2DBS locks before restart." << std::endl; }
        }
        return 1;
    }
}