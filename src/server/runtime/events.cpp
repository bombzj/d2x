#include "events.hpp"
#include <limits>

namespace d2x::server {
DomainResult<> FrameFacts::append(DomainFact fact) {
    if (values_.size() >= 4096) return {DomainStatus::Capacity, {}};
    values_.push_back(std::move(fact));
    return {DomainStatus::Applied, std::monostate{}};
}
bool EventOutbox::hasCapacity(size_t count) const {
    return count && count <= 4096 - facts_ && batches_.size() < 256 && next_ != std::numeric_limits<uint64_t>::max();
}
DomainResult<uint64_t> EventOutbox::publish(EventBatch batch) {
    if (batch.facts.empty()) return {DomainStatus::InvalidRequest, std::nullopt};
    if (!hasCapacity(batch.facts.size())) return {DomainStatus::Capacity, std::nullopt};
    const auto sequence = next_;
    batch.sequence = sequence;
    const auto count = batch.facts.size();
    batches_.push_back(std::move(batch));
    facts_ += count; ++next_;
    return {DomainStatus::Applied, sequence};
}
bool EventOutbox::acknowledge(uint64_t sequence) {
    if (sequence == acknowledged_) return true;
    if (sequence < acknowledged_ || batches_.empty() || sequence > batches_.back().sequence) return false;
    while (!batches_.empty() && batches_.front().sequence <= sequence) {
        facts_ -= batches_.front().facts.size(); batches_.pop_front();
    }
    acknowledged_ = sequence;
    return true;
}
}
