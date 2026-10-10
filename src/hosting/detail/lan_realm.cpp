#include "lan_realm.hpp"
#include <algorithm>
#include <thread>
namespace d2x::hosting {
namespace {
constexpr auto admissionTimeout = std::chrono::seconds(15);
}
LanRealm::~LanRealm() { network_.shutdown(); }
void LanRealm::listen(std::string address, uint16_t gamePort) {
    host_.initialize(); network_.listen(net::Endpoint{std::move(address), gamePort});
}
void LanRealm::retire(Peer &peer) {
    if (peer.cleanupAttempted) return;
    peer.cleanupAttempted = true;
    // TCP/IP has no disk-save acknowledgment. Keep an isolated host recovery
    // copy, including the last queued save if transport closes after retirement.
    try { peer.service->recoverTcpIpSave(); peer.service->close(false); }
    catch (const std::exception &error) { peer.service->counters.lastFailure = error.what(); }
}
void LanRealm::fail(Peer &peer, std::string error) {
    peer.service->failure = error; peer.service->counters.lastFailure = std::move(error);
    ++peer.service->counters.failures; peer.service->failGame();
    peer.closed = true; network_.close(peer.socket); retire(peer);
}
std::optional<AdminResult> LanRealm::administer(const AdminRequest &request) {
    for (auto &[id, peer] : peers_) {
        (void)id; const auto &binding = peer->service->binding;
        if (!binding || binding->game != request.target.game || binding->player != request.target.player) continue;
        if (peer->closed && request.operation == AdminOperation::Save) {
            try { peer->service->recoverTcpIpSave(); peer->service->close(false); return AdminResult{AdminStatus::Applied, "TCP/IP host recovery saved"}; }
            catch (const std::exception &error) { return AdminResult{AdminStatus::Failed, error.what()}; }
        }
        return peer->service->administer(request);
    }
    return {};
}
void LanRealm::collect() {
    const auto now = std::chrono::steady_clock::now();
    for (auto &[id, peer] : peers_) {
        (void)id;
        if (peer->closed) continue;
        if (peer->service->peer.phase == GamePhase::Closed) {
            if (!peer->closing) { peer->closing = true; peer->deadline = now + admissionTimeout; network_.closeAfterWrites(peer->socket); }
            if (now >= peer->deadline) fail(*peer, "TCP/IP save output timed out");
        } else if (peer->service->peer.phase != GamePhase::Entered && now >= peer->deadline)
            fail(*peer, "Native TCP/IP character admission timed out");
    }
    std::erase_if(peers_, [](const auto &entry) { return entry.second->closed && !entry.second->service->binding; });
}
void LanRealm::poll() {
    for (auto &event : network_.poll()) {
        const auto id = event.connection;
        try {
            if (event.stream.kind == net::StreamEventKind::Connected) {
                if (peers_.size() >= 64) { network_.close(id); continue; }
                auto peer = std::make_unique<Peer>(); peer->socket = id; peer->deadline = std::chrono::steady_clock::now() + admissionTimeout;
                peer->service = std::make_unique<NativeRealmService>(host_,
                    [](uint8_t, Bytes) { throw net::protocol::ProtocolError("TCP/IP does not use MCP"); },
                    [this, id](Bytes bytes) { if (!network_.send(id, std::move(bytes))) throw std::runtime_error("TCP/IP game output queue closed or full"); });
                peer->service->connectTcpIp(); peers_.emplace(id, std::move(peer));
                continue;
            }
            const auto found = peers_.find(id); if (found == peers_.end()) continue;
            auto &peer = *found->second;
            if (event.stream.kind == net::StreamEventKind::Data) {
                if (peer.closed || peer.closing) continue;
                peer.packets.append(event.stream.data); Bytes packet;
                while (peer.packets.next(packet)) {
                    peer.service->game(packet);
                    peer.deadline = std::chrono::steady_clock::now() + admissionTimeout;
                    if (peer.service->peer.phase == GamePhase::Closed) break;
                }
            } else {
                peer.closed = true; peer.service->failGame(); retire(peer);
            }
        } catch (const std::exception &error) {
            if (const auto found = peers_.find(id); found != peers_.end()) fail(*found->second, error.what());
            else network_.close(id);
        }
    }
    collect();
}
void LanRealm::close() {
    for (auto &[id, peer] : peers_) { (void)id; if (!peer->closed) peer->service->checkpoint(); }
    for (auto &[id, peer] : peers_) {
        (void)id;
        if (!peer->closed) {
            peer->service->sendGame({0x06}); peer->service->close(false);
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::any_of(peers_.begin(), peers_.end(), [](const auto &entry) { return !entry.second->closed; }) && std::chrono::steady_clock::now() < deadline) {
        poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto &[id, peer] : peers_) {
        (void)id; peer->service->recoverTcpIpSave(); peer->service->close(false);
    }
    network_.shutdown(); peers_.clear();
}
}
