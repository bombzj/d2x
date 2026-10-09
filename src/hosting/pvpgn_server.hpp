#pragma once
#include "network/byte_transport.hpp"
#include <filesystem>
#include <functional>

namespace d2x {
class Archives;
struct PvpgnServerOptions {
    net::Endpoint d2cs{"127.0.0.1", 6113}, d2dbs{"127.0.0.1", 6114}, listen{"0.0.0.0", 4000};
    std::filesystem::path recovery;
    std::string realm;
    uint32_t version{}, maximumGames{16};
};
class PvpgnServer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    PvpgnServer(Archives &, PvpgnServerOptions, std::function<void(std::string_view)> log);
    ~PvpgnServer();
    void start();
    void pump(double seconds);
    bool stopRequested() const;
    void shutdown();
};
}