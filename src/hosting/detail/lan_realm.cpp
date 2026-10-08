#include "lan_realm.hpp"
#include <charconv>
#include <algorithm>
namespace d2x::hosting {
using namespace net::protocol;
namespace {
constexpr auto admissionTimeout = std::chrono::seconds(15);
std::array<uint8_t, 4> ipv4(std::string_view address) {
    std::array<uint8_t, 4> result{};
    for (size_t index = 0; index < 4; ++index) {
        const auto dot = address.find('.'); const auto part = address.substr(0, dot);
        unsigned value{}; const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size() || value > 255 || (index < 3) != (dot != address.npos))
            throw std::invalid_argument("LAN host requires an explicit IPv4 interface address");
        result[index] = uint8_t(value); if (dot != address.npos) address.remove_prefix(dot + 1);
    }
    if (!result[0] || result[0] >= 224) throw std::invalid_argument("LAN host address must be a unicast interface");
    return result;
}
}
LanRealm::~LanRealm() { network_.shutdown(); }
void LanRealm::listen(std::string address, uint16_t realmPort, uint16_t gamePort) {
    // UI hosting accepts loopback and LAN interfaces together. Game replies
    // advertise the concrete interface of each accepted MCP socket.
    if (address != "0.0.0.0") (void)ipv4(address);
    host_.initialize(); network_.listen({net::Endpoint{address, realmPort}, net::Endpoint{address, gamePort}});
}
void LanRealm::fail(Peer &peer, std::string error) {
    peer.connection->fail(std::move(error)); peer.abandoned = true;
    network_.close(peer.realmSocket); network_.close(peer.gameSocket);
}
std::optional<AdminResult> LanRealm::administer(const AdminRequest &request) {
    for (auto &peer : peers_) {
        const auto &binding = peer->connection->service.binding;
        if (!binding || binding->game != request.target.game || binding->player != request.target.player) continue;
        auto result = peer->connection->service.administer(request);
        if (result.applied() && request.operation == AdminOperation::Save) peer->cleanupAttempted = false;
        return result;
    }
    return {};
}
void LanRealm::collect() {
    const auto now = std::chrono::steady_clock::now();
    for (auto &peer : peers_) {
        auto &service = peer->connection->service;
        if (peer->abandoned || (peer->realmClosed && !peer->gameSocket && now >= peer->deadline)) {
            if (!peer->abandoned) fail(*peer, "Native game admission timed out");
            // Save failure retains the peer, instance and file lease for host
            // administration; never discard authority state silently.
            if (peer->cleanupAttempted) continue;
            peer->cleanupAttempted = true;
            try { service.close(); }
            catch (const std::exception &error) { service.counters.lastFailure = error.what(); }
        }
        if (peer->gameSocket && service.peer.phase == GamePhase::Closed) {
            network_.closeAfterWrites(peer->gameSocket);
            if (service.binding) { peer->abandoned = true; service.failGame(); }
        }
        if (!peer->gameSocket && !service.authenticated && !peer->abandoned && now >= peer->deadline) fail(*peer, "MCP session idle timeout");
        if (peer->gameSocket && service.peer.phase != GamePhase::Entered && now >= peer->deadline) fail(*peer, "Native game logon timed out");
    }
    std::erase_if(peers_, [&](const auto &peer) {
        if (!peer->abandoned || peer->connection->service.binding) return false;
        if (std::any_of(realms_.begin(), realms_.end(), [&](const auto &e) { return e.second == peer.get(); }) ||
            std::any_of(games_.begin(), games_.end(), [&](const auto &e) { return e.second.peer == peer.get(); })) return false;
        return true;
    });
    for (auto it = games_.begin(); it != games_.end();) {
        if (!it->second.peer && now >= it->second.deadline) { network_.close(it->first); it = games_.erase(it); }
        else ++it;
    }
}
void LanRealm::poll() {
    for (auto &event : network_.poll()) {
        const auto id = event.connection;
        try {
            if (event.stream.kind == net::StreamEventKind::Connected) {
                if (event.listener == 0) {
                    if (peers_.size() >= 64) { network_.close(id); continue; }
                    auto peer = std::make_unique<Peer>(); peer->realmSocket = id; peer->deadline = std::chrono::steady_clock::now() + admissionTimeout;
                    auto *target = peer.get();
                    peer->connection = std::make_unique<RealmConnection>(host_,
                        [this, target](uint8_t message, Bytes body) {
                            if (!network_.send(target->realmSocket, frame(Framing::Mcp, message, body))) throw std::runtime_error("LAN MCP output queue closed or full");
                        }, [this, target](Bytes bytes) {
                            if (!network_.send(target->gameSocket, std::move(bytes))) throw std::runtime_error("LAN D2GS output queue closed or full");
                        });
                    peer->connection->service.gameAddress = ipv4(event.localAddress);
                    peer->connection->service.multiplayerEndpoint = true;
                    peer->connection->openRealm(); realms_[id] = target; peers_.push_back(std::move(peer));
                } else {
                    if (games_.size() >= 64) { network_.close(id); continue; }
                    games_.try_emplace(id, GameSocket{{}, nullptr, std::chrono::steady_clock::now() + admissionTimeout});
                    if (!network_.send(id, {0xAF, 0})) network_.close(id);
                }
                continue;
            }
            if (event.stream.kind == net::StreamEventKind::Data) {
                if (event.listener == 0) {
                    const auto found = realms_.find(id);
                    if (found != realms_.end() && !found->second->abandoned) {
                        found->second->connection->realm(event.stream.data);
                        found->second->deadline = std::chrono::steady_clock::now() + admissionTimeout;
                    }
                } else {
                    auto found = games_.find(id); if (found == games_.end()) continue;
                    auto &socket = found->second;
                    if (socket.peer && socket.peer->connection->service.peer.phase == GamePhase::Closed) continue;
                    socket.packets.append(event.stream.data); Bytes packet;
                    while (socket.packets.next(packet)) {
                        if (!socket.peer) {
                            if (packet[0] != uint8_t(ClientMessage::Logon)) throw ProtocolError("First game request must be native logon");
                            net::protocol::Reader in(packet); in.u8(); const auto hash = in.u32(); const auto token = in.u16();
                            auto *service = host_.ticket(hash, token);
                            if (!service) throw ProtocolError("Unknown or consumed native game ticket");
                            Peer *target = nullptr;
                            for (auto &candidate : peers_) if (&candidate->connection->service == service && !candidate->abandoned && !candidate->gameSocket) target = candidate.get();
                            if (!target) throw ProtocolError("Ticket is not owned by a pending LAN peer");
                            socket.peer = target; target->gameSocket = id;
                            target->deadline = std::chrono::steady_clock::now() + admissionTimeout;
                            target->connection->openGame(false);
                        }
                        socket.peer->connection->service.game(packet);
                        if (socket.peer->connection->service.peer.phase == GamePhase::Closed) { network_.closeAfterWrites(id); break; }
                    }
                }
                continue;
            }
            if (event.listener == 0) {
                const auto found = realms_.find(id);
                if (found != realms_.end()) {
                    auto &peer = *found->second; peer.realmClosed = true; peer.realmSocket = 0;
                    peer.deadline = std::chrono::steady_clock::now() + admissionTimeout;
                    realms_.erase(found);
                    if (!peer.connection->service.binding) { peer.abandoned = true; peer.connection->service.close(); }
                }
            } else {
                const auto found = games_.find(id);
                if (found != games_.end()) {
                    auto *peer = found->second.peer;
                    games_.erase(found);
                    if (peer) {
                        peer->gameSocket = 0; peer->abandoned = true; peer->cleanupAttempted = true;
                        peer->connection->service.failGame();
                        try { peer->connection->service.close(); }
                        catch (const std::exception &error) { peer->connection->service.counters.lastFailure = error.what(); }
                    }
                }
            }
        } catch (const std::exception &error) {
            if (const auto found = realms_.find(id); found != realms_.end()) fail(*found->second, error.what());
            else if (const auto found = games_.find(id); found != games_.end() && found->second.peer) fail(*found->second.peer, error.what());
            else network_.close(id);
        }
    }
    collect();
}
void LanRealm::close() {
    // Every checkpoint must finish before listener/leases are released.
    for (auto &peer : peers_) peer->connection->service.checkpoint();
    for (auto &peer : peers_) peer->connection->service.close(false);
    network_.shutdown(); games_.clear(); realms_.clear(); peers_.clear();
}
}
