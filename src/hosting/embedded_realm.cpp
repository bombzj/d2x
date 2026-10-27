#include "embedded_realm.hpp"
#include "automap_store.hpp"
#include "detail/realm_connection.hpp"
#include "detail/lan_realm.hpp"
#include "network/memory_transport.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
namespace d2x {
using namespace net::protocol;
struct EmbeddedRealm::Impl {
    inline static thread_local Impl *executingHost{};
    std::unique_ptr<Archives> archives;
    hosting::NativeRealmHost host;
    const std::filesystem::path defaultRoot;
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
    bool shutdownComplete{};
    bool callerResumed{};
    std::mutex requestsMutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> requests;
    std::atomic<bool> paused{};
    std::jthread worker;
    hosting::PerformanceObserver performanceObserver;
    std::mutex timingsMutex;
    std::vector<hosting::StepTiming> stepTimings;
    std::vector<hosting::PumpTiming> pumpTimings;
    template<class Operation> auto invoke(Operation &&operation) -> std::invoke_result_t<Operation> {
        if (executingHost == this || !worker.joinable())
            return std::forward<Operation>(operation)();
        auto task = std::make_shared<std::packaged_task<std::invoke_result_t<Operation>()>>(std::forward<Operation>(operation));
        auto result = task->get_future();
        {
            std::lock_guard lock(requestsMutex);
            if (requests.size() >= 64) throw std::runtime_error("Host control queue is full");
            requests.emplace_back([task] { (*task)(); });
        }
        wake.notify_one();
        return result.get();
    }
    void start() {
        if (worker.joinable()) return;
        worker = std::jthread([this](std::stop_token stop) {
            executingHost = this;
            auto previous = std::chrono::steady_clock::now();
            while (!stop.stop_requested()) {
                std::deque<std::function<void()>> pending;
                {
                    std::lock_guard lock(requestsMutex);
                    pending.swap(requests);
                }
                for (auto &operation : pending) operation();
                if (shutdownComplete) break;
                const auto now = std::chrono::steady_clock::now();
                try { pump(std::chrono::duration<double>(now - previous).count(), paused.load()); }
                catch (const std::exception &error) { failure = error.what(); }
                previous = now;
                std::unique_lock lock(requestsMutex);
                wake.wait_for(lock, std::chrono::milliseconds(5), [&] { return stop.stop_requested() || !requests.empty(); });
            }
            executingHost = nullptr;
        });
    }
    void stop() {
        if (!worker.joinable()) return;
        worker.request_stop(); wake.notify_all(); worker.join();
        callerResumed = true;
    }
    void deliverTimings() {
        std::vector<hosting::StepTiming> steps;
        std::vector<hosting::PumpTiming> pumps;
        {
            std::lock_guard lock(timingsMutex);
            steps.swap(stepTimings); pumps.swap(pumpTimings);
        }
        for (const auto &timing : steps) if (performanceObserver.step) performanceObserver.step(timing);
        for (const auto &timing : pumps) if (performanceObserver.pump) performanceObserver.pump(timing);
    }
    void saveReturn() {
        if (!returnedSave) return;
        auto &service = primary->connection->service;
        if (!service.lease || !service.store) throw std::runtime_error("TCP/IP local character lease has expired");
        service.store->saveBytes(*service.lease, *returnedSave);
        returnedSave.reset();
    }
    Impl(Archives &source, std::filesystem::path root, ContentPreparation preparation)
        : archives(source.independent()), host(*archives, root, preparation), defaultRoot(std::move(root)) { add(); }
    ~Impl() { stop(); }
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
EmbeddedRealm::EmbeddedRealm(Archives &archives, std::filesystem::path root, ContentPreparation preparation)
    : impl_(std::make_unique<Impl>(archives, std::move(root), preparation)) {}
EmbeddedRealm::~EmbeddedRealm() = default;
void EmbeddedRealm::observePerformance(hosting::PerformanceObserver observer) {
    impl_->invoke([&] {
        impl_->host.host.observeSteps(observer.step ? std::function<void(const hosting::StepTiming &)>{[state=impl_.get()](const auto &timing) {
            std::lock_guard lock(state->timingsMutex);
            if (state->stepTimings.size() < 4096) state->stepTimings.push_back(timing);
        }} : std::function<void(const hosting::StepTiming &)>{});
        impl_->host.performanceObserver = observer.pump ? std::function<void(const hosting::PumpTiming &)>{[state=impl_.get()](const auto &timing) {
            std::lock_guard lock(state->timingsMutex);
            if (state->pumpTimings.size() < 4096) state->pumpTimings.push_back(timing);
        }} : std::function<void(const hosting::PumpTiming &)>{};
    });
    impl_->performanceObserver = std::move(observer);
}
std::string EmbeddedRealm::prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass) {
    return impl_->invoke([&] { return impl_->primary->connection->service.prepareStartup(load, save, characterClass); });
}
EmbeddedRealm::Transports EmbeddedRealm::connect(bool defaultDirectory) {
    return impl_->invoke([&] {
    auto &o = *impl_; auto &peer = *o.primary; auto &service = peer.connection->service;
    o.saveReturn(); service.close(); peer.disconnect();
    if (defaultDirectory && !o.lan && o.host.root != o.defaultRoot) {
        if (o.lan || o.host.peers.size() > 1 || !o.host.games.empty()) throw std::runtime_error("Cannot change an active shared save repository");
        o.host.root = o.defaultRoot; service.store.reset();
    }
    if (defaultDirectory) service.startupFile.clear();
    peer.mcp = std::make_shared<net::MemoryChannel>(); peer.gs = std::make_shared<net::MemoryChannel>();
    peer.mcpGeneration = peer.gsGeneration = 0; peer.retired = false;
    return o.transports(peer);
    });
}
EmbeddedRealm::Transports EmbeddedRealm::attach() {
    return impl_->invoke([&] { auto &o = *impl_; return o.transports(o.add()); });
}
void EmbeddedRealm::listen(std::string address, uint16_t gamePort) {
    impl_->invoke([&] {
    auto &o = *impl_; if (o.lan) throw std::runtime_error("LAN listener is already active");
    auto listener = std::make_unique<hosting::LanRealm>(o.host); listener->listen(std::move(address), gamePort); o.lan = std::move(listener);
    });
}
void EmbeddedRealm::setTcpIpHost(bool enabled) {
    impl_->invoke([&] {
        if (impl_->shutdownComplete) throw std::runtime_error("The embedded host is shut down");
        if (!enabled && std::any_of(impl_->host.games.begin(), impl_->host.games.end(),
            [](const auto &entry) { return entry.second.tcpIp; }))
            throw std::runtime_error("Leave the TCP/IP game before switching host mode");
        impl_->primary->connection->service.tcpIpHost = enabled;
    });
    if (enabled) impl_->start();
    else impl_->stop();
}
Bytes EmbeddedRealm::selectedTcpIpSave() {
    return impl_->invoke([&] {
    auto &o = *impl_; auto &service = o.primary->connection->service;
    o.saveReturn();
    if (!service.lease || !service.selected || service.binding) throw std::runtime_error("Select a local character before TCP/IP Join");
    service.store->load(*service.lease);
    return service.lease->expected;
    });
}
std::shared_ptr<hosting::AutomapStore> EmbeddedRealm::selectedAutomapStore() const {
    return impl_->invoke([&]() -> std::shared_ptr<hosting::AutomapStore> {
    const auto &service=impl_->primary->connection->service;
    if(!service.lease || !service.selected) return {};
    return std::make_shared<hosting::AutomapStore>(service.lease->path);
    });
}
void EmbeddedRealm::receiveTcpIpSave(Bytes bytes) {
    impl_->invoke([&] {
    auto &o = *impl_;
    o.saveReturn(); // An earlier failed replacement cannot be discarded.
    o.returnedSave = std::move(bytes);
    o.saveReturn();
    });
}
void EmbeddedRealm::pump(double seconds, bool paused) {
    impl_->paused.store(paused);
    if (!impl_->worker.joinable()) {
        try {
            if (!impl_->shutdownComplete)
                impl_->pump(std::exchange(impl_->callerResumed, false) ? 0. : seconds, paused);
        }
        catch (const std::exception &error) { impl_->failure = error.what(); }
    }
    impl_->deliverTimings();
}
void EmbeddedRealm::observeGamePackets(hosting::GamePacketObserver observer) {
    impl_->invoke([&] { impl_->host.gamePacketObserver = std::move(observer); });
}
hosting::HostDiagnostics EmbeddedRealm::diagnostics() const {
    return impl_->invoke([&] {
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
    });
}
hosting::HostDiagnostics EmbeddedRealm::diagnostics(PlayerBinding binding) const {
    return impl_->invoke([&]() -> hosting::HostDiagnostics {
    for (auto *peer : impl_->host.peers) if (peer->binding && peer->binding->game == binding.game && peer->binding->player == binding.player) {
        auto result = peer->diagnostics();
        auto all = diagnostics(); result.rooms = std::move(all.rooms); result.participants = std::move(all.participants);
        return result;
    }
    throw std::invalid_argument("The host game/player generation has changed");
    });
}
std::optional<server::DiagnosticSnapshot> EmbeddedRealm::inspect(PlayerBinding binding, size_t limit, uint64_t since,
    uint64_t commandSince, std::optional<Vec> destination) const {
    return impl_->invoke([&] { return impl_->host.host.diagnostics(binding, limit, since, commandSince, destination); });
}
hosting::AdminResult EmbeddedRealm::administer(const hosting::AdminRequest &request) {
    return impl_->invoke([&]() -> hosting::AdminResult {
    if (impl_->lan) if (auto result = impl_->lan->administer(request)) return *result;
    for (auto *peer : impl_->host.peers) if (peer->binding && peer->binding->game == request.target.game && peer->binding->player == request.target.player)
        {
            auto result = peer->administer(request);
            if (result.applied() && request.operation == hosting::AdminOperation::Save)
                for (auto &endpoint : impl_->peers) if (&endpoint->connection->service == peer && endpoint->retired) peer->close(false);
            return result;
        }
    return {hosting::AdminStatus::InvalidTarget, "The host game/player generation has changed"};
    });
}
void EmbeddedRealm::close() {
    impl_->invoke([&] {
    auto &o = *impl_;
    // TCP/IP owner retirement flushes and ends its guests as well.
    o.saveReturn(); o.primary->connection->service.close(); o.primary->disconnect();
    });
}
void EmbeddedRealm::shutdown() {
    impl_->invoke([&] {
    auto &o = *impl_;
    o.saveReturn();
    // LAN shutdown checkpoints its TCP/IP peers once before ending them.
    for (auto *peer : o.host.peers) if (!peer->directTcpIp) peer->checkpoint();
    if (o.lan) o.lan->close();
    for (auto &peer : o.peers) { peer->connection->service.close(false); peer->disconnect(); }
    o.shutdownComplete = true;
    });
    impl_->stop();
    impl_->deliverTimings();
}
bool EmbeddedRealm::active() const {
    return impl_->invoke([&] { return impl_->primary->connection->service.binding.has_value(); });
}
std::string EmbeddedRealm::takeError() {
    return impl_->invoke([&] {
    auto error = std::exchange(impl_->failure, {});
    if (error.empty()) error = std::exchange(impl_->primary->connection->service.failure, {});
    return error;
    });
}
}
