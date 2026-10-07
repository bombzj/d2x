#pragma once
#include "network/byte_transport.hpp"
#include "administration.hpp"
#include <filesystem>

namespace d2x {
class Archives;
// Composition root for an in-process native MCP/D2GS server. No client models,
// frontend callbacks or decoded messages cross the memory transports.
class EmbeddedRealm {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct Transports { std::unique_ptr<net::IByteTransport> realm, game; };
    EmbeddedRealm(Archives &, std::filesystem::path saves);
    ~EmbeddedRealm();
    Transports connect(bool defaultDirectory = false);
    std::string prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass);
    void pump(double seconds, bool paused);
    // Host administration, never a proprietary client protocol. Failure keeps
    // the instance and file lease available for a later retry.
    hosting::HostDiagnostics diagnostics() const;
    hosting::AdminResult administer(const hosting::AdminRequest &);
    void close();
    bool active() const;
    std::string takeError();
};
}
