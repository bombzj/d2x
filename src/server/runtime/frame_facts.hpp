#pragma once
#include "contracts.hpp"
#include "domain_fact.hpp"
#include <span>
#include <vector>

namespace d2x::server {
// Scratch facts for later phases in the SAME step, not a deferred event bus.
// Work for a phase that has already run must stay in its owner's pending state.
class FrameFacts {
    std::vector<DomainFact> values_;
  public:
    std::span<const DomainFact> read() const { return values_; }
    DomainResult<> append(DomainFact);
};
}
