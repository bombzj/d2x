#pragma once
#include "server/movement_observation.hpp"
#include <memory>
#include <optional>
#include <string>

namespace d2x {
struct OnlineView;
class RemoteScene;
namespace debug {
class MovementDiff {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    explicit MovementDiff(const std::string &path);
    ~MovementDiff();
    bool enabled() const {return bool(impl_);}
    void record(std::optional<server::MovementObservation>,const OnlineView &,
        const RemoteScene *,std::optional<int> area,bool suspended) noexcept;
};
}
}
