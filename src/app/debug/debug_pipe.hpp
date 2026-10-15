#pragma once
#include <functional>
#include <memory>
#include <string>

namespace d2x {
class DebugPipe {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    explicit DebugPipe(const std::string &name);
    ~DebugPipe();
    // The IO worker only transfers bounded text. Execute at most one claimed
    // request here, on the owning app/host thread; no game state crosses threads.
    void poll(const std::function<std::string(const std::string &)> &handler);
    bool responsePending() const;
};
} // namespace d2x
