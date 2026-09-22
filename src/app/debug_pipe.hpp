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
    void poll(const std::function<std::string(const std::string &)> &handler);
};
} // namespace d2x