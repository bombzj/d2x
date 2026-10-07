#include "embedded_realm.hpp"
#include "detail/native_realm_service.hpp"
#include "protocol/client_stream.hpp"
#include "network/memory_transport.hpp"
#include <utility>

namespace d2x {
using namespace net::protocol;
struct EmbeddedRealm::Impl {
    std::shared_ptr<net::MemoryChannel> mcp, gs;
    uint64_t mcpGeneration{}, gsGeneration{};
    PacketStream realmPackets{Framing::Mcp};
    hosting::ClientPacketStream gamePackets;
    bool selector{}, wasOpen{};
    hosting::NativeRealmService realm;

    Impl(Archives &archives, std::filesystem::path root)
        : realm(archives, std::move(root),
            [this](uint8_t id, Bytes body) {
                if (!mcp || !mcp->send(mcpGeneration, frame(Framing::Mcp, id, body)))
                    throw std::runtime_error("Realm output queue closed or full");
            }, [this](Bytes bytes) {
                if (!gs || !gs->send(gsGeneration, std::move(bytes)))
                    throw std::runtime_error("Game output queue closed or full");
            }) {}
    void pump(double seconds, bool paused) {
        if (!mcp || !gs) return;
        auto realmInput = mcp->take();
        if (realmInput.connected) {
            mcpGeneration = realmInput.generation; realmPackets.reset(); selector = false;
            realm.resetRealm();
        }
        for (const auto &chunk : realmInput.bytes) {
            std::span<const uint8_t> data(chunk);
            if (!selector && !data.empty()) {
                if (data[0] != 1) throw ProtocolError("Invalid MCP selector");
                selector = true; data = data.subspan(1);
            }
            realmPackets.append(data); Packet packet;
            while (realmPackets.next(packet)) realm.realm(packet);
        }
        auto gameInput = gs->take();
        if (gameInput.connected) {
            gsGeneration = gameInput.generation; gamePackets.reset(); realm.connectGame();
        }
        realm.setPaused(paused);
        for (const auto &chunk : gameInput.bytes) {
            gamePackets.append(chunk); Bytes packet;
            while (gamePackets.next(packet)) {
                realm.game(packet);
                if (realm.peer.phase == hosting::GamePhase::Closed) break;
            }
            if (realm.peer.phase == hosting::GamePhase::Closed) {
                // Save/leave retires this generation, including queued heartbeats.
                gs->close(gsGeneration); gamePackets.reset(); break;
            }
        }
        if (wasOpen && !gameInput.open && realm.binding) realm.close();
        wasOpen = gameInput.open;
        realm.advance(seconds, paused);
    }
    void disconnect() {
        if (gs) gs->close(gsGeneration);
        if (mcp) mcp->close(mcpGeneration);
        wasOpen = false; selector = false;
        realmPackets.reset(); gamePackets.reset();
    }
};
EmbeddedRealm::EmbeddedRealm(Archives &a, std::filesystem::path root) : impl_(std::make_unique<Impl>(a, std::move(root))) {}
EmbeddedRealm::~EmbeddedRealm() = default;
std::string EmbeddedRealm::prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass) {
    return impl_->realm.prepareStartup(load, save, characterClass);
}
EmbeddedRealm::Transports EmbeddedRealm::connect(bool defaultDirectory) {
    auto &o = *impl_; o.realm.close(); o.disconnect();
    auto &realm = o.realm;
    if (defaultDirectory) {
        realm.root = "saves"; realm.startupFile.clear();
        if (realm.content) realm.store = std::make_unique<CharacterStore>(realm.root, *realm.content);
    }
    realm.initialize();
    o.mcp = std::make_shared<net::MemoryChannel>(); o.gs = std::make_shared<net::MemoryChannel>();
    return {std::make_unique<net::MemoryTransport>(o.mcp), std::make_unique<net::MemoryTransport>(o.gs)};
}
void EmbeddedRealm::pump(double seconds, bool paused) {
    try { impl_->pump(seconds, paused); }
    catch (const std::exception &e) {
        auto &o = *impl_; o.realm.failure = e.what(); o.realm.failGame();
        ++o.realm.counters.failures; o.realm.counters.lastFailure = e.what();
        o.wasOpen = false;
        // Retain failed saves and their lease; administration can retry.
        if (o.gs) o.gs->close(o.gsGeneration, o.realm.failure);
        if (o.mcp) o.mcp->close(o.mcpGeneration, o.realm.failure);
    }
}
hosting::HostDiagnostics EmbeddedRealm::diagnostics() const { return impl_->realm.diagnostics(); }
hosting::AdminResult EmbeddedRealm::administer(const hosting::AdminRequest &request) { return impl_->realm.administer(request); }
void EmbeddedRealm::close() { impl_->realm.close(); impl_->disconnect(); }
bool EmbeddedRealm::active() const { return impl_->realm.binding.has_value(); }
std::string EmbeddedRealm::takeError() { return std::exchange(impl_->realm.failure, {}); }
}
