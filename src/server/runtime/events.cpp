#include "events.hpp"
#include <limits>
#include <type_traits>
#include <algorithm>

namespace d2x::server {
DomainResult<> FrameFacts::append(DomainFact fact) {
    if (values_.size() >= 4096) return {DomainStatus::Capacity, {}};
    values_.push_back(std::move(fact));
    return {DomainStatus::Applied, std::monostate{}};
}
bool EventOutbox::hasCapacity(size_t count, size_t batches) const {
    return count && batches && count <= 4096 - facts_ && batches <= 256 - batches_.size() && batches <= std::numeric_limits<uint64_t>::max() - next_;
}
DomainResult<uint64_t> EventOutbox::publish(EventBatch batch) {
    if (batch.facts.empty()) return {DomainStatus::InvalidRequest, std::nullopt};
    if (!hasCapacity(batch.facts.size())) return {DomainStatus::Capacity, std::nullopt};
    const auto sequence = next_;
    batch.sequence = sequence;
    const auto count = batch.facts.size();
    batches_.push_back(std::move(batch));
    facts_ += count; ++next_;
    observe(batches_.back());
    return {DomainStatus::Applied, sequence};
}
DomainResult<uint64_t> EventOutbox::publishGroup(std::vector<EventBatch> group) {
    size_t count = 0;
    for (const auto &batch : group) {
        if (batch.facts.empty() || batch.facts.size() > 4096 - count) return {DomainStatus::InvalidRequest, {}};
        count += batch.facts.size();
    }
    if (!hasCapacity(count, group.size())) return {DomainStatus::Capacity, {}};
    const auto previous = batches_.size();
    try {
        for (auto &batch : group) {
            batch.sequence = next_ + batches_.size() - previous;
            batches_.push_back(std::move(batch));
        }
    } catch (...) {
        while (batches_.size() > previous) batches_.pop_back();
        throw;
    }
    const auto first = next_;
    next_ += group.size(); facts_ += count;
    for (size_t i = previous; i < batches_.size(); ++i) observe(batches_[i]);
    return {DomainStatus::Applied, first};
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
void EventOutbox::observe(const EventBatch &batch) noexcept {
    for (const auto &fact : batch.facts) {
        if (historyNext_ == UINT64_MAX) return;
        DiagnosticEvent event;
        event.sequence = historyNext_++; event.batch = batch.sequence; event.tick = batch.tick;
        event.transaction = batch.transaction.value; event.type = fact.index(); event.area = batch.audience.area;
        std::visit([&]<class T>(const T &value) {
            if constexpr (std::is_same_v<T, AttackFact>) {
                event.actor = value.actor; event.target = value.target; event.value = value.skill; event.secondary = value.rank; event.position = value.destination;
            } else if constexpr (std::is_same_v<T, HitFact>) {
                event.target = value.target; event.value = value.life; event.secondary = value.killed; event.position = value.position;
            } else if constexpr (std::is_same_v<T, LifeFact>) { event.actor = value.actor; event.value = int64_t(value.life * 256.f); }
            else if constexpr (std::is_same_v<T, ManaFact>) { event.actor = value.actor; event.value = int64_t(value.mana * 256.f); }
            else if constexpr (std::is_same_v<T, SoundFact>) {event.actor=value.actor;event.value=value.sound;}
            else if constexpr (std::is_same_v<T, ItemTargetingFact>) {event.actor=value.source;event.value=value.cursor;event.secondary=value.skill;}
            else if constexpr (std::is_same_v<T, OverlayFact>) {event.actor=value.actor;event.value=value.overlay;}
            else if constexpr (std::is_same_v<T, StateFact>) { event.actor = value.actor; event.value = value.state; event.secondary = value.enabled; }
            else if constexpr (std::is_same_v<T, SkillPulseFact>) { event.actor=value.owner; event.target=value.target; event.value=value.skill; event.secondary=value.rank; event.position=value.position; }
            else if constexpr (std::is_same_v<T, ItemSkillFact>) {event.actor=value.owner;event.target=value.target;event.value=value.skill;event.secondary=value.rank;event.position=value.position;}
            else if constexpr (std::is_same_v<T, GroundRemoveFact>) {event.target=value.item;}
            else if constexpr (std::is_same_v<T, GroundRestoredFact>) {event.target=value.item;event.secondary=int64_t(value.revision);}
            else if constexpr (std::is_same_v<T, NpcServiceFact>) {event.target=value.npc;event.value=value.result;}
            else if constexpr (std::is_same_v<T, MissileFact>) { event.actor = value.owner; event.value = value.definition; event.secondary = value.rank; event.position = value.position; }
            else if constexpr (std::is_same_v<T, RepositionFact>) { event.actor = value.actor; event.position = value.position; }
            else if constexpr (std::is_same_v<T, CharacterFact>) {
                event.actor = value.after.id; event.value = int64_t(value.after.experience); event.secondary = value.after.level;
            } else if constexpr (std::is_same_v<T, InventoryFact>) { event.actor = value.projection.player.id; event.value = int64_t(value.changes.size()); }
            else if constexpr (std::is_same_v<T, TradeFact>) {event.target=value.partner;event.value=value.action;}
            else if constexpr (std::is_same_v<T, TradeItemsFact>) {event.actor=value.projection.player.id;event.value=int64_t(value.projection.inventory.items.size());event.secondary=int64_t(value.removed.size());}
            else if constexpr (std::is_same_v<T, ChatRelationFact>) {event.actor=value.from;event.target=value.to;event.value=value.flags;}
            else if constexpr (std::is_same_v<T, PlayerMessageFact>) {event.value=value.type;}
            else if constexpr (std::is_same_v<T, PartyNoticeFact>) {event.actor=value.player;event.value=value.action;event.secondary=int64_t(value.recipient.value);}
            else if constexpr (std::is_same_v<T, GroundDropFact>) {
                event.target = value.item.id; event.value = value.item.quantity;
                const auto &at = std::get<GroundLocation>(value.item.location);
                event.area = at.region; event.position = at.position;
            }
            else if constexpr (std::is_same_v<T, TravelFact>) { event.actor = value.actor; event.value = int(value.from); event.secondary = int(value.to); event.position = value.position; }
        }, fact);
        history_[(event.sequence - 1) % history_.size()] = event;
    }
}
std::vector<DiagnosticEvent> EventOutbox::history(uint64_t since, size_t limit) const {
    std::vector<DiagnosticEvent> result;
    if (since == UINT64_MAX) return result;
    for (auto sequence = std::max(historyFirst(), since + 1); sequence < historyNext_ && result.size() < limit; ++sequence)
        result.push_back(history_[(sequence - 1) % history_.size()]);
    return result;
}
}
