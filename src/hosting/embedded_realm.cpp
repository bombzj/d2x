#include "embedded_realm.hpp"
#include "detail/realm_connection.hpp"
#include "detail/lan_realm.hpp"
#include "network/memory_transport.hpp"
#include <algorithm>
#include <utility>
namespace d2x {
using namespace net::protocol;
struct EmbeddedRealm::Impl {
    hosting::NativeRealmHost host;
    struct MemoryPeer {
        std::shared_ptr<net::MemoryChannel> mcp = std::make_shared<net::MemoryChannel>(), gs = std::make_shared<net::MemoryChannel>();
        uint64_t mcpGeneration{}, gsGeneration{};
        bool wasOpen{}, retired{}, everConnected{};
        std::unique_ptr<hosting::RealmConnection> connection;
        void disconnect(std::string error = {}) { mcp->close(mcpGeneration, error); gs->close(gsGeneration, std::move(error)); wasOpen = false; }
    };
    std::vector<std::unique_ptr<MemoryPeer>> peers;
    MemoryPeer *primary{};
    std::unique_ptr<hosting::LanRealm> lan;
    std::string failure;
    std::optional<Bytes> returnedSave;
    void saveReturn() {
        if (!returnedSave) return;
        auto &service = primary->connection->service;
        if (!service.lease || !service.store) throw std::runtime_error("TCP/IP local character lease has expired");
        service.store->saveBytes(*service.lease, *returnedSave);
        returnedSave.reset();
    }
    Impl(Archives &archives, std::filesystem::path root) : host(archives, std::move(root)) { add(); }
    MemoryPeer &add() {
        if (peers.size() >= 64) throw std::runtime_error("Memory host connection capacity exhausted");
        auto peer = std::make_unique<MemoryPeer>(); auto *target = peer.get();
        peer->connection = std::make_unique<hosting::RealmConnection>(host,
            [target](uint8_t id, Bytes body) {
                if (!target->mcp->send(target->mcpGeneration, frame(Framing::Mcp, id, body))) throw std::runtime_error("Realm output queue closed or full");
            }, [target](Bytes bytes) {
                if (!target->gs->send(target->gsGeneration, std::move(bytes))) throw std::runtime_error("Game output queue closed or full");
            });
        peers.push_back(std::move(peer)); if (!primary) primary = target; return *target;
    }
    Transports transports(MemoryPeer &peer) {
        peer.connection->service.initialize();
        return {std::make_unique<net::MemoryTransport>(peer.mcp), std::make_unique<net::MemoryTransport>(peer.gs)};
    }
    void input(MemoryPeer &peer, bool paused) {
        if (peer.retired) return;
        auto &connection = *peer.connection;
        auto realmInput = peer.mcp->take();
        if (realmInput.connected) { peer.everConnected = true; peer.mcpGeneration = realmInput.generation; connection.openRealm(); }
        for (const auto &chunk : realmInput.bytes) connection.realm(chunk);
        auto gameInput = peer.gs->take();
        if (gameInput.connected) { peer.gsGeneration = gameInput.generation; connection.openGame(); }
        connection.service.setPaused(paused);
        for (const auto &chunk : gameInput.bytes) {
            connection.game(chunk);
            if (connection.service.peer.phase == hosting::GamePhase::Closed) { peer.gs->close(peer.gsGeneration); break; }
        }
        if (peer.wasOpen && !gameInput.open && connection.service.binding) connection.service.close();
        peer.wasOpen = gameInput.open;
        if (&peer != primary && peer.everConnected && !realmInput.open && !gameInput.open && !connection.service.binding) peer.retired = true;
    }
    void pump(double seconds, bool paused) {
        if (lan) lan->poll();
        for (auto &peer : peers) {
            try { input(*peer, peer.get() == primary && paused); }
            catch (const std::exception &error) {
                peer->connection->fail(error.what()); peer->disconnect(error.what()); peer->retired = true;
                try { peer->connection->service.close(); }
                catch (const std::exception &saveError) { peer->connection->service.counters.lastFailure = saveError.what(); }
                failure = error.what();
            }
        }
        host.advance(seconds);
        for (auto &peer : peers) if (peer->connection->service.peer.phase == hosting::GamePhase::Closed && peer->connection->service.binding && !peer->connection->service.ticket) {
            peer->disconnect(peer->connection->service.failure);
        }
        std::erase_if(peers, [&](const auto &peer) { return peer.get() != primary && peer->retired && !peer->connection->service.binding; });
    }
};
EmbeddedRealm::EmbeddedRealm(Archives &archives, std::filesystem::path root) : impl_(std::make_unique<Impl>(archives, std::move(root))) {}
EmbeddedRealm::~EmbeddedRealm() = default;
std::string EmbeddedRealm::prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass) {
    return impl_->primary->connection->service.prepareStartup(load, save, characterClass);
}
EmbeddedRealm::Transports EmbeddedRealm::connect(bool defaultDirectory) {
    auto &o = *impl_; auto &peer = *o.primary; auto &service = peer.connection->service;
    o.saveReturn(); service.close(); peer.disconnect();
    if (defaultDirectory && !o.lan && o.host.root != std::filesystem::path("saves")) {
        if (o.lan || o.host.peers.size() > 1 || !o.host.games.empty()) throw std::runtime_error("Cannot change an active shared save repository");
        o.host.root = "saves"; service.store.reset();
    }
    if (defaultDirectory) service.startupFile.clear();
    peer.mcp = std::make_shared<net::MemoryChannel>(); peer.gs = std::make_shared<net::MemoryChannel>();
    peer.mcpGeneration = peer.gsGeneration = 0; peer.retired = false;
    return o.transports(peer);
}
EmbeddedRealm::Transports EmbeddedRealm::attach() { auto &o = *impl_; return o.transports(o.add()); }
void EmbeddedRealm::listen(std::string address, uint16_t gamePort) {
    auto &o = *impl_; if (o.lan) throw std::runtime_error("LAN listener is already active");
    auto listener = std::make_unique<hosting::LanRealm>(o.host); listener->listen(std::move(address), gamePort); o.lan = std::move(listener);
}
void EmbeddedRealm::setTcpIpHost(bool enabled) { impl_->primary->connection->service.tcpIpHost = enabled; }
Bytes EmbeddedRealm::selectedTcpIpSave() {
    auto &o = *impl_; auto &service = o.primary->connection->service;
    o.saveReturn();
    if (!service.lease || !service.selected || service.binding) throw std::runtime_error("Select a local character before TCP/IP Join");
    service.store->load(*service.lease);
    return service.lease->expected;
}
void EmbeddedRealm::receiveTcpIpSave(Bytes bytes) {
    auto &o = *impl_;
    o.saveReturn(); // An earlier failed replacement cannot be discarded.
    o.returnedSave = std::move(bytes);
    o.saveReturn();
}
void EmbeddedRealm::pump(double seconds, bool paused) {
    try { impl_->pump(seconds, paused); }
    catch (const std::exception &error) { impl_->failure = error.what(); }
}
hosting::HostDiagnostics EmbeddedRealm::diagnostics() const {
    const auto &o = *impl_; auto result = o.primary->connection->service.diagnostics();
    for (const auto &[key, room] : o.host.games) {
        (void)key; result.rooms.push_back({room.handle, room.name, room.capacity, unsigned(o.host.host.participants(room.handle).size())});
    }
    for (auto *peer : o.host.peers) if (peer->binding) {
        const auto view = o.host.host.read(*peer->binding);
        result.participants.push_back({*peer->binding, peer->selectedName, peer->diagnostics().phase, peer->counters.lastFailure,
            view ? view->actor.region : RegionId{}, view && view->entered});
    }
    return result;
}
hosting::HostDiagnostics EmbeddedRealm::diagnostics(PlayerBinding binding) const {
    for (auto *peer : impl_->host.peers) if (peer->binding && peer->binding->game == binding.game && peer->binding->player == binding.player) {
        auto result = peer->diagnostics();
        auto all = diagnostics(); result.rooms = std::move(all.rooms); result.participants = std::move(all.participants);
        return result;
    }
    throw std::invalid_argument("The host game/player generation has changed");
}
std::optional<server::DiagnosticSnapshot> EmbeddedRealm::inspect(PlayerBinding binding, size_t limit, uint64_t since,
    uint64_t commandSince, std::optional<Vec> destination) const {
    return impl_->host.host.diagnostics(binding, limit, since, commandSince, destination);
}
hosting::AdminResult EmbeddedRealm::administer(const hosting::AdminRequest &request) {
    if (impl_->lan) if (auto result = impl_->lan->administer(request)) return *result;
    for (auto *peer : impl_->host.peers) if (peer->binding && peer->binding->game == request.target.game && peer->binding->player == request.target.player)
        {
            auto result = peer->administer(request);
            if (result.applied() && request.operation == hosting::AdminOperation::Save)
                for (auto &endpoint : impl_->peers) if (&endpoint->connection->service == peer && endpoint->retired) peer->close(false);
            return result;
        }
    return {hosting::AdminStatus::InvalidTarget, "The host game/player generation has changed"};
}
void EmbeddedRealm::close() {
    auto &o = *impl_;
    // TCP/IP owner retirement flushes and ends its guests as well.
    o.saveReturn(); o.primary->connection->service.close(); o.primary->disconnect();
}
void EmbeddedRealm::shutdown() {
    auto &o = *impl_;
    o.saveReturn();
    for (auto *peer : o.host.peers) peer->checkpoint();
    if (o.lan) o.lan->close();
    for (auto &peer : o.peers) { peer->connection->service.close(false); peer->disconnect(); }
}
bool EmbeddedRealm::active() const { return impl_->primary->connection->service.binding.has_value(); }
std::string EmbeddedRealm::takeError() {
    auto error = std::exchange(impl_->failure, {});
    if (error.empty()) error = std::exchange(impl_->primary->connection->service.failure, {});
    return error;
}
}
