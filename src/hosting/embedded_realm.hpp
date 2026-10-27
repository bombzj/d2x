#pragma once
#include "network/byte_transport.hpp"
#include "game_packet_observer.hpp"
#include "performance.hpp"
#include "administration.hpp"
#include "server/runtime/diagnostics.hpp"
#include "content/content_preparation.hpp"
#include <filesystem>

namespace d2x {
class Archives;
namespace hosting { class AutomapStore; }
// Public methods belong to one caller thread. TCP/IP mode dispatches host work
// to its service thread; single-player mode advances in the caller's pump.
class EmbeddedRealm {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct Transports { std::unique_ptr<net::IByteTransport> realm, game; };
    EmbeddedRealm(Archives &, std::filesystem::path saves, ContentPreparation = ContentPreparation::OriginalRules);
    ~EmbeddedRealm();
    Transports connect(bool defaultDirectory = false);
    Transports attach(); // An additional independent memory client, sharing this host.
    void listen(std::string address, uint16_t gamePort = 4000);
    void setTcpIpHost(bool);
    Bytes selectedTcpIpSave();
    std::shared_ptr<hosting::AutomapStore> selectedAutomapStore() const;
    void receiveTcpIpSave(Bytes);
    std::string prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass);
    void pump(double seconds, bool paused);
    // Borrowed packet bytes are observed synchronously on the host thread.
    void observeGamePackets(hosting::GamePacketObserver);
    // Timings are delivered on the caller thread by pump/shutdown.
    void observePerformance(hosting::PerformanceObserver);
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
