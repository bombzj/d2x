#pragma once
#include "network/byte_transport.hpp"
#include "administration.hpp"
#include "server/runtime/diagnostics.hpp"
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
    Transports attach(); // An additional independent memory client, sharing this host.
    void listen(std::string address, uint16_t gamePort = 4000);
    void setTcpIpHost(bool);
    Bytes selectedTcpIpSave();
    void receiveTcpIpSave(Bytes);
    std::string prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass);
    void pump(double seconds, bool paused);
    // Host administration, never a proprietary client protocol. Failure keeps
    // the instance and file lease available for a later retry.
    hosting::HostDiagnostics diagnostics() const;
    hosting::HostDiagnostics diagnostics(PlayerBinding) const;
    std::optional<server::DiagnosticSnapshot> inspect(PlayerBinding, size_t limit, uint64_t since, uint64_t commandSince,
                                                     std::optional<Vec> destination = {}) const;
    hosting::AdminResult administer(const hosting::AdminRequest &);
    void close(); // Disconnect only the primary memory client.
    void shutdown(); // Checkpoint all participants before closing the host.
    bool active() const;
    std::string takeError();
};
}
