#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "gameplay/items/skill_sources.hpp"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <list>
#include <map>
#include "gameplay/events/frame_queue.hpp"
#include <string>

namespace d2x {struct StatSnapshot;}
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
    monsters::System &monsters;
    effects::System &effects;
    FrameEventQueue &frameEvents;
};
class ItemTriggers {
    const ItemTriggerPorts ports_;
    std::list<ItemTrigger> pending_;
    std::map<FrameEventHandle,ItemEventPlan> scheduled_;
    std::string deferred_;
    StepStatus advanceJobs(std::list<ItemTrigger> &,uint64_t tick);
    DomainResult<> advancePd2ItemHeal(ItemTrigger &,uint64_t tick);
  public:
    explicit ItemTriggers(ItemTriggerPorts ports);
    ~ItemTriggers();
    ItemTriggers(const ItemTriggers &) = delete;
    ItemTriggers &operator=(const ItemTriggers &) = delete;
    size_t pending() const;
    bool involves(EntityId) const;
    const std::string &deferred() const { return deferred_; }
    DomainResult<ItemEventPlan> prepare(const ActorContext &, std::initializer_list<ItemSkillEvent>, EntityId, Vec,const StatSnapshot *weaponStats=nullptr) const;
    void commit(ItemEventPlan plan, bool terminal) noexcept;
    void append(ItemEventPlan &,ItemEventPlan) const noexcept;
    StepStatus advance(uint64_t tick);
    StepStatus advance(ItemEventPlan &,uint64_t tick);
    StepStatus advanceContact(ItemEventPlan &,uint64_t tick,bool beforeDamage,bool terminal=false);
    DomainResult<> frameEvent(const FrameEvent &,uint64_t tick);
    void schedulePd2Block(EntityId blocker,EntityId attacker,uint64_t tick);
};
}
