#include "memory_transport.hpp"
#include <stdexcept>
#include <utility>
namespace d2x::net {
namespace { constexpr size_t limit = 2 * 1024 * 1024, events = 4096; }
void MemoryTransport::connect(Endpoint, std::chrono::milliseconds) {
    auto &c = *channel_; std::lock_guard lock(c.mutex_);
    ++c.generation_; c.open_ = true; c.accepted_ = false;
    c.client_.clear(); c.server_.clear(); c.clientBytes_ = c.serverBytes_ = 0;
    c.server_.push_back({StreamEventKind::Connected, {}, {}});
}
bool MemoryTransport::send(Bytes bytes) {
    auto &c = *channel_; std::lock_guard lock(c.mutex_);
    if (!c.open_ || bytes.size() > limit - c.clientBytes_ || c.client_.size() >= events) return false;
    c.clientBytes_ += bytes.size(); c.client_.push_back(std::move(bytes)); return true;
}
std::vector<StreamEvent> MemoryTransport::poll() {
    auto &c = *channel_; std::lock_guard lock(c.mutex_);
    c.serverBytes_ = 0; return std::exchange(c.server_, {});
}
void MemoryTransport::close() {
    auto &c = *channel_; std::lock_guard lock(c.mutex_);
    c.open_ = false; c.client_.clear(); c.server_.clear(); c.clientBytes_ = c.serverBytes_ = 0;
}
bool MemoryTransport::connected() const { auto &c = *channel_; std::lock_guard lock(c.mutex_); return c.open_; }
MemoryChannel::Input MemoryChannel::take() {
    std::lock_guard lock(mutex_);
    Input input{generation_, open_, open_ && !accepted_, std::exchange(client_, {})};
    accepted_ = open_; clientBytes_ = 0; return input;
}
bool MemoryChannel::send(uint64_t generation, Bytes bytes) {
    std::lock_guard lock(mutex_);
    if (!open_ || generation != generation_ || bytes.size() > limit - serverBytes_ || server_.size() >= events) return false;
    serverBytes_ += bytes.size(); server_.push_back({StreamEventKind::Data, std::move(bytes), {}}); return true;
}
void MemoryChannel::close(uint64_t generation, std::string error) {
    std::lock_guard lock(mutex_);
    if (!open_ || generation != generation_) return;
    open_ = false;
    client_.clear(); clientBytes_ = 0;
    // A failed connection cannot deliver a partial admission before its error.
    // Normal closure preserves already queued native replies (notably B0).
    if (!error.empty()) { server_.clear(); serverBytes_ = 0; }
    server_.push_back({error.empty() ? StreamEventKind::Closed : StreamEventKind::Error, {}, std::move(error)});
}
} // namespace d2x::net
