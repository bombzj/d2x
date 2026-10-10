#include "tcp_listener.hpp"
#include <asio.hpp>
#include <deque>
#include <map>
#include <stdexcept>
namespace d2x::net {
struct TcpListener::Impl {
    struct Connection {
        asio::ip::tcp::socket socket;
        uint64_t id{};
        uint8_t listener{};
        std::array<uint8_t, 8192> buffer{};
        std::deque<std::shared_ptr<Bytes>> writes;
        size_t queued{};
        bool stopped{}, closing{};
        asio::steady_timer closeTimer;
        explicit Connection(asio::io_context &io) : socket(io), closeTimer(io) {}
    };
    asio::io_context io;
    std::array<std::unique_ptr<asio::ip::tcp::acceptor>, 2> listeners;
    std::map<uint64_t, std::shared_ptr<Connection>> connections;
    std::vector<Event> events;
    uint64_t next = 1;
    size_t received{};
    static constexpr size_t byteLimit = 4 * 1024 * 1024, eventLimit = 4096, connectionLimit = 128;
    void stop(const std::shared_ptr<Connection> &c) {
        c->stopped = true;
        asio::error_code ignored; c->closeTimer.cancel(); c->socket.close(ignored);
        c->writes.clear(); c->queued = 0; connections.erase(c->id);
    }
    void finish(const std::shared_ptr<Connection> &c, std::string error = {}) {
        if (c->stopped) return;
        events.push_back({c->id, c->listener, {error.empty() ? StreamEventKind::Closed : StreamEventKind::Error, {}, std::move(error)}, {}});
        stop(c);
    }
    void read(const std::shared_ptr<Connection> &c) {
        c->socket.async_read_some(asio::buffer(c->buffer), [this, c](const asio::error_code &error, size_t count) {
            if (c->stopped) return;
            if (count && !c->closing) {
                if (events.size() >= eventLimit || count > byteLimit - received) { finish(c, "TCP listener receive capacity exceeded"); return; }
                received += count;
                events.push_back({c->id, c->listener, {StreamEventKind::Data, Bytes(c->buffer.begin(), c->buffer.begin() + count), {}}, {}});
            }
            if (error) finish(c, error == asio::error::eof ? std::string{} : "TCP listener read failed: " + error.message());
            else read(c);
        });
    }
    void finishWrites(const std::shared_ptr<Connection> &c) {
        // Closing with unread client bytes can send RST and discard the final
        // character save on Windows. FIN the output, drain input until EOF,
        // and bound peers which do not finish their half of the connection.
        asio::error_code ignored;
        c->socket.shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
        c->closeTimer.expires_after(std::chrono::seconds(2));
        c->closeTimer.async_wait([this, c](const asio::error_code &error) { if (!error && !c->stopped) finish(c); });
    }
    void write(const std::shared_ptr<Connection> &c) {
        auto bytes = c->writes.front();
        asio::async_write(c->socket, asio::buffer(*bytes), [this, c, bytes](const asio::error_code &error, size_t) {
            if (c->stopped) return;
            if (error) { finish(c, "TCP listener write failed: " + error.message()); return; }
            c->queued -= bytes->size(); c->writes.pop_front();
            if (!c->writes.empty()) write(c);
            else if (c->closing) finishWrites(c);
        });
    }
    void accept(uint8_t listener) {
        auto c = std::make_shared<Connection>(io);
        listeners[listener]->async_accept(c->socket, [this, c, listener](const asio::error_code &error) {
            if (error == asio::error::operation_aborted || !listeners[listener] || !listeners[listener]->is_open()) return;
            if (!error && connections.size() < connectionLimit && next != UINT64_MAX && events.size() < eventLimit) {
                asio::error_code endpointError;
                const auto endpoint = c->socket.local_endpoint(endpointError);
                if (endpointError) {
                    asio::error_code ignored; c->socket.close(ignored);
                    accept(listener); return;
                }
                c->id = next++; c->listener = listener;
                asio::error_code ignored; c->socket.set_option(asio::ip::tcp::no_delay(true), ignored);
                connections.emplace(c->id, c);
                events.push_back({c->id, listener, {StreamEventKind::Connected, {}, {}},
                                  endpoint.address().to_string()}); read(c);
            } else { asio::error_code ignored; c->socket.close(ignored); }
            accept(listener);
        });
    }
};
TcpListener::TcpListener() : impl_(std::make_unique<Impl>()) {}
TcpListener::~TcpListener() { shutdown(); }
void TcpListener::listen(Endpoint endpoint) {
    shutdown();
    try {
        if (!endpoint.port) throw std::invalid_argument("Zero listener port");
        auto acceptor = std::make_unique<asio::ip::tcp::acceptor>(impl_->io);
        acceptor->open(asio::ip::tcp::v4());
        acceptor->bind({asio::ip::make_address_v4(endpoint.host), endpoint.port});
        acceptor->listen(); impl_->listeners[0] = std::move(acceptor); impl_->accept(0);
    } catch (...) { shutdown(); throw; }
}
void TcpListener::listen(std::array<Endpoint, 2> endpoints) {
    shutdown(); auto &p = *impl_;
    try {
        for (uint8_t i = 0; i < 2; ++i) {
            const auto &e = endpoints[i]; if (!e.port) throw std::invalid_argument("Zero listener port");
            const auto address = asio::ip::make_address_v4(e.host);
            auto acceptor = std::make_unique<asio::ip::tcp::acceptor>(p.io);
            acceptor->open(asio::ip::tcp::v4());
            // No reuse_address: two host processes must never share an endpoint.
            acceptor->bind({address, e.port}); acceptor->listen(); p.listeners[i] = std::move(acceptor);
        }
        for (uint8_t i = 0; i < 2; ++i) p.accept(i);
    } catch (...) { shutdown(); throw; }
}
std::vector<TcpListener::Event> TcpListener::poll() {
    auto &p = *impl_; p.io.restart();
    for (size_t count = 0; count < 256 && p.io.poll_one(); ++count) {}
    auto events = std::move(p.events); p.events.clear(); p.received = 0; return events;
}
bool TcpListener::send(uint64_t id, Bytes bytes) {
    auto &p = *impl_; const auto found = p.connections.find(id);
    if (found == p.connections.end()) return false;
    const auto c = found->second;
    if (c->closing) return false;
    if (bytes.size() > p.byteLimit - c->queued) { p.finish(c, "TCP listener send capacity exceeded"); return false; }
    if (bytes.empty()) return true;
    const bool idle = c->writes.empty(); c->queued += bytes.size();
    c->writes.push_back(std::make_shared<Bytes>(std::move(bytes))); if (idle) p.write(c); return true;
}
void TcpListener::close(uint64_t id) { if (const auto found = impl_->connections.find(id); found != impl_->connections.end()) impl_->finish(found->second); }
void TcpListener::closeAfterWrites(uint64_t id) {
    const auto found = impl_->connections.find(id); if (found == impl_->connections.end()) return;
    auto c = found->second; c->closing = true;
    if (c->writes.empty()) impl_->finishWrites(c);
}
void TcpListener::shutdown() {
    auto &p = *impl_;
    for (auto &listener : p.listeners) if (listener) { asio::error_code ignored; listener->close(ignored); }
    // Keep acceptors alive until cancelled handlers have been drained; callbacks
    // also retain their socket/buffer values past close.
    while (!p.connections.empty()) p.stop(p.connections.begin()->second);
    p.io.restart(); while (p.io.poll_one()) {}
    for (auto &listener : p.listeners) listener.reset();
    p.events.clear(); p.received = 0;
}
}
