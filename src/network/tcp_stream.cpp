#include "network/tcp_stream.hpp"
#include <asio.hpp>
#include <array>
#include <deque>
#include <stdexcept>
#include <utility>

namespace d2x::net {
ByteStream::ByteStream() : transport_(std::make_unique<TcpStream>()) {}
struct TcpStream::Impl {
    struct Connection {
        explicit Connection(asio::io_context &io) : resolver(io), socket(io), timer(io) {}
        asio::ip::tcp::resolver resolver;
        asio::ip::tcp::socket socket;
        asio::steady_timer timer;
        std::array<uint8_t, 8192> readBuffer{};
        std::deque<std::shared_ptr<Bytes>> writes;
        size_t sendBytes{};
        bool online{}, stopped{};
    };
    asio::io_context io;
    StreamLimits limits;
    std::shared_ptr<Connection> current;
    std::vector<StreamEvent> events;
    size_t receiveBytes{};
    explicit Impl(StreamLimits value) : limits(value) {
        if (!limits.queuedEvents || !limits.handlersPerPoll || !limits.queuedSendBytes ||
            !limits.queuedReceiveBytes) throw std::invalid_argument("Zero TCP stream limit");
    }
    bool active(const std::shared_ptr<Connection> &c) const { return c == current && !c->stopped; }
    void stop(const std::shared_ptr<Connection> &c) {
        c->stopped = true;
        c->online = false;
        c->resolver.cancel();
        asio::error_code ignored;
        c->timer.cancel(ignored);
        c->socket.close(ignored);
        c->writes.clear(); // In-flight writes retain their own shared buffer.
        c->sendBytes = 0;
    }
    void fail(const std::shared_ptr<Connection> &c, std::string message) {
        if (!active(c)) return;
        stop(c);
        // Preserve preceding data unless the consumer itself exceeded queue limits.
        events.push_back({StreamEventKind::Error, {}, std::move(message)});
    }
    bool push(const std::shared_ptr<Connection> &c, StreamEvent event) {
        if (!active(c)) return false;
        if (events.size() >= limits.queuedEvents ||
            event.data.size() > limits.queuedReceiveBytes - receiveBytes) {
            events.clear();
            receiveBytes = 0;
            fail(c, "TCP receive queue limit exceeded");
            return false;
        }
        receiveBytes += event.data.size();
        events.push_back(std::move(event));
        return true;
    }
    void read(const std::shared_ptr<Connection> &c) {
        c->socket.async_read_some(asio::buffer(c->readBuffer),
            [this, c](const asio::error_code &ec, size_t count) {
                if (!active(c)) return;
                if (count && !push(c, {StreamEventKind::Data,
                    Bytes(c->readBuffer.begin(), c->readBuffer.begin() + count), {}})) return;
                if (ec == asio::error::eof) {
                    if (push(c, {StreamEventKind::Closed, {}, {}})) stop(c);
                } else if (ec) fail(c, "TCP read failed: " + ec.message());
                else read(c);
            });
    }
    void write(const std::shared_ptr<Connection> &c) {
        auto bytes = c->writes.front();
        asio::async_write(c->socket, asio::buffer(*bytes),
            [this, c, bytes](const asio::error_code &ec, size_t) {
                if (!active(c)) return;
                if (ec) { fail(c, "TCP write failed: " + ec.message()); return; }
                c->sendBytes -= bytes->size();
                c->writes.pop_front();
                if (!c->writes.empty()) write(c);
            });
    }
};
TcpStream::TcpStream(StreamLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
TcpStream::~TcpStream() { close(); }
void TcpStream::connect(Endpoint endpoint, std::chrono::milliseconds timeout) {
    if (endpoint.host.empty() || !endpoint.port || timeout.count() <= 0)
        throw std::invalid_argument("Invalid TCP endpoint or timeout");
    close();
    auto &p = *impl_;
    auto c = std::make_shared<Impl::Connection>(p.io);
    p.current = c;
    c->timer.expires_after(timeout);
    c->timer.async_wait([&p, c](const asio::error_code &ec) {
        if (!ec) p.fail(c, "TCP connect timeout");
    });
    c->resolver.async_resolve(endpoint.host, std::to_string(endpoint.port),
        [&p, c](const asio::error_code &ec, asio::ip::tcp::resolver::results_type results) {
            if (!p.active(c)) return;
            if (ec) { p.fail(c, "TCP resolve failed: " + ec.message()); return; }
            asio::async_connect(c->socket, results,
                [&p, c](const asio::error_code &error, const asio::ip::tcp::endpoint &) {
                    if (!p.active(c)) return;
                    if (error) { p.fail(c, "TCP connect failed: " + error.message()); return; }
                    asio::error_code ignored;
                    c->timer.cancel(ignored);
                    c->online = true;
                    asio::error_code optionError;
                    c->socket.set_option(asio::ip::tcp::no_delay(true), optionError);
                    if (p.push(c, {StreamEventKind::Connected, {}, {}})) p.read(c);
                });
        });
}
bool TcpStream::send(Bytes bytes) {
    auto &p = *impl_;
    auto c = p.current;
    if (!c || !p.active(c) || !c->online) return false;
    if (bytes.empty()) return true;
    if (bytes.size() > p.limits.queuedSendBytes - c->sendBytes) {
        p.fail(c, "TCP send queue limit exceeded");
        return false;
    }
    c->sendBytes += bytes.size();
    const bool wasEmpty = c->writes.empty();
    c->writes.push_back(std::make_shared<Bytes>(std::move(bytes)));
    if (wasEmpty) p.write(c);
    return true;
}
std::vector<StreamEvent> TcpStream::poll() {
    auto &p = *impl_;
    p.io.restart();
    for (size_t i = 0; i < p.limits.handlersPerPoll && p.io.poll_one(); ++i) {}
    auto events = std::move(p.events);
    p.events.clear();
    p.receiveBytes = 0;
    return events;
}
void TcpStream::close() {
    if (impl_->current) impl_->stop(impl_->current);
    impl_->current.reset();
    impl_->events.clear();
    impl_->receiveBytes = 0;
}
bool TcpStream::connected() const {
    return impl_->current && impl_->active(impl_->current) && impl_->current->online;
}
} // namespace d2x::net
