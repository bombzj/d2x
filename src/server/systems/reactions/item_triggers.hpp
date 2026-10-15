#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "gameplay/items/skill_sources.hpp"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <list>
#include <string>

namespace d2x::server::reactions {
struct ItemTrigger;
class ItemEventPlan {
        friend class ItemTriggers;
        std::list<ItemTrigger> jobs;
    public:
        ItemEventPlan();
        ~ItemEventPlan();
        ItemEventPlan(ItemEventPlan &&) noexcept;
        ItemEventPlan &operator=(ItemEventPlan &&) noexcept;
        ItemEventPlan(const ItemEventPlan &) = delete;
        ItemEventPlan &operator=(const ItemEventPlan &) = delete;
};
struct ItemTriggerPorts {
    const PlayerStore &players;
    const AreaStore &areas;
    skills::System &skills;
    transactions::System &transactions;
    uint64_t &random;
    EventOutbox &events;
};
class ItemTriggers {
    const ItemTriggerPorts ports_;
    std::list<ItemTrigger> pending_;
    std::string deferred_;
  public:
    explicit ItemTriggers(ItemTriggerPorts ports);
    ~ItemTriggers();
    ItemTriggers(const ItemTriggers &) = delete;
    ItemTriggers &operator=(const ItemTriggers &) = delete;
    size_t pending() const;
    const std::string &deferred() const { return deferred_; }
    DomainResult<ItemEventPlan> prepare(const ActorContext &, std::initializer_list<ItemSkillEvent>, EntityId, Vec) const;
    void commit(ItemEventPlan plan, bool terminal) noexcept;
    StepStatus advance(uint64_t tick);
};
}