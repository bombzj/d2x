#include "events.hpp"
#include "diagnostic_event.hpp"
#include "frame_facts.hpp"
#include "server/runtime/environment_fact.hpp"
#include "server/runtime/facts/character.hpp"
#include "server/runtime/facts/combat.hpp"
#include "server/runtime/facts/command.hpp"
#include "server/runtime/facts/effects.hpp"
#include "server/runtime/facts/inventory.hpp"
#include "server/runtime/facts/map.hpp"
#include "server/runtime/facts/npc.hpp"
#include "server/runtime/facts/object.hpp"
#include "server/runtime/facts/player_message.hpp"
#include "server/runtime/facts/quest.hpp"
#include "server/runtime/facts/quest_signals.hpp"
#include "server/runtime/facts/resources.hpp"
#include "server/runtime/facts/skills.hpp"
#include "server/runtime/facts/social.hpp"
#include "server/runtime/facts/travel.hpp"
#include "server/runtime/facts/ui.hpp"
#include "server/runtime/facts/unit.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <type_traits>


namespace d2x::server {
struct EventOutbox::Storage {
    struct UnitSound {EventBatch batch;uint64_t order{};};
    std::map<EntityId,UnitSound> unitSounds;
    uint64_t nextSoundOrder{1};
    std::deque<EventBatch> batches;
    size_t facts{};
    uint64_t next = 1, acknowledged{};
    std::array<DiagnosticEvent,1024> history{};
    uint64_t historyNext = 1;
};
EventOutbox::EventOutbox() : storage_(std::make_unique<Storage>()) {}
EventOutbox::~EventOutbox() = default;
bool EventOutbox::hasStagedSounds() const { return !storage_->unitSounds.empty(); }
void EventOutbox::discardUnitSound(EntityId actor) noexcept { storage_->unitSounds.erase(actor); }
const std::deque<EventBatch> &EventOutbox::pending() const { return storage_->batches; }
uint64_t EventOutbox::historyFirst() const {
    return storage_->historyNext > storage_->history.size() ? storage_->historyNext - storage_->history.size() : 1;
}
uint64_t EventOutbox::historyLast() const { return storage_->historyNext - 1; }
DomainResult<> FrameFacts::append(DomainFact fact) {
    if (!fact.hasValue()) return {DomainStatus::InvalidRequest, {}};
    if (values_.size() >= 4096) return {DomainStatus::Capacity, {}};
    values_.push_back(std::move(fact));
    return {DomainStatus::Applied, std::monostate{}};
}
bool EventOutbox::hasCapacity(size_t count, size_t batches) const {
    return count && batches && count <= 4096 - storage_->facts && batches <= 256 - storage_->batches.size() && batches <= std::numeric_limits<uint64_t>::max() - storage_->next;
}
DomainResult<uint64_t> EventOutbox::publish(EventBatch batch) {
    if (batch.facts.empty() || std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact){return !fact.hasValue();}))
        return {DomainStatus::InvalidRequest, std::nullopt};
    if(std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact) {
        const auto *sound=factIf<SoundFact>(&fact);return sound && !sound->directSound;
    })) {
        std::vector<EventBatch> group;group.push_back(std::move(batch));return publishGroup(std::move(group));
    }
    if (!hasCapacity(batch.facts.size())) return {DomainStatus::Capacity, std::nullopt};
    const auto sequence = storage_->next;
    batch.sequence = sequence;
    const auto count = batch.facts.size();
    storage_->batches.push_back(std::move(batch));
    storage_->facts += count; ++storage_->next;
    observe(storage_->batches.back());
    return {DomainStatus::Applied, sequence};
}
DomainResult<uint64_t> EventOutbox::publishGroup(std::vector<EventBatch> group) {
    const bool attached=std::any_of(group.begin(),group.end(),[](const auto &batch) {
        return std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact) {
            const auto *sound=factIf<SoundFact>(&fact);return sound && !sound->directSound;
        });
    });
    if(!attached) return publishReadyGroup(std::move(group));
    auto sounds=storage_->unitSounds;auto nextOrder=storage_->nextSoundOrder;
    for(auto &batch:group) {
        if(batch.facts.empty() || std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact){return !fact.hasValue();}))
            return {DomainStatus::InvalidRequest,{}};
        std::erase_if(batch.facts,[&](const auto &fact) {
            const auto *sound=factIf<SoundFact>(&fact);if(!sound || sound->directSound) return false;
            auto found=sounds.find(sound->actor);
            if(found==sounds.end()) {
                if(nextOrder==UINT64_MAX) return false;
                found=sounds.emplace(sound->actor,Storage::UnitSound{{},nextOrder++}).first;
            }
            found->second.batch={0,batch.tick,batch.transaction,batch.audience,{*sound}};return true;
        });
        // An exhausted order must reject the entire originating operation.
        if(std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact) {
            const auto *sound=factIf<SoundFact>(&fact);return sound && !sound->directSound;
        })) return {DomainStatus::Capacity,{}};
    }
    std::erase_if(group,[](const auto &batch){return batch.facts.empty();});
    const auto sent=group.empty()?DomainResult<uint64_t>{DomainStatus::Applied,0}:publishReadyGroup(std::move(group));
    if(sent) {storage_->unitSounds.swap(sounds);storage_->nextSoundOrder=nextOrder;}
    return sent;
}
DomainResult<uint64_t> EventOutbox::publishReadyGroup(std::vector<EventBatch> group) {
    size_t count = 0;
    for (const auto &batch : group) {
        if (batch.facts.empty() || batch.facts.size() > 4096 - count ||
            std::any_of(batch.facts.begin(),batch.facts.end(),[](const auto &fact){return !fact.hasValue();}))
            return {DomainStatus::InvalidRequest, {}};
        count += batch.facts.size();
    }
    if (!hasCapacity(count, group.size())) return {DomainStatus::Capacity, {}};
    const auto previous = storage_->batches.size();
    try {
        for (auto &batch : group) {
            batch.sequence = storage_->next + storage_->batches.size() - previous;
            storage_->batches.push_back(std::move(batch));
        }
    } catch (...) {
        while (storage_->batches.size() > previous) storage_->batches.pop_back();
        throw;
    }
    const auto first = storage_->next;
    storage_->next += group.size(); storage_->facts += count;
    for (size_t i = previous; i < storage_->batches.size(); ++i) observe(storage_->batches[i]);
    return {DomainStatus::Applied, first};
}
bool EventOutbox::hasPendingSound(EntityId actor) const {
    return storage_->unitSounds.contains(actor);
}
DomainResult<uint64_t> EventOutbox::flushUnitSounds(uint64_t tick,const std::function<std::optional<RegionId>(const SoundFact &)> &region) {
    std::vector<const Storage::UnitSound *> ordered;ordered.reserve(storage_->unitSounds.size());
    for(const auto &[actor,sound]:storage_->unitSounds) {(void)actor;ordered.push_back(&sound);}
    std::sort(ordered.begin(),ordered.end(),[](const auto *left,const auto *right){return left->order<right->order;});
    uint64_t sequence=0;
    for(const auto *entry:ordered) {
        auto batch=entry->batch;auto &sound=factGet<SoundFact>(batch.facts.front());
        const auto actor=sound.actor;
        const auto area=region(sound);if(!area) {storage_->unitSounds.erase(actor);continue;}
        sound.area=*area;batch.tick=tick;
        if(batch.audience.kind==AudienceKind::Area) batch.audience.area=*area;
        std::vector<EventBatch> group;group.push_back(std::move(batch));
        const auto sent=publishReadyGroup(std::move(group));if(!sent) return sent;
        sequence=*sent.value;storage_->unitSounds.erase(actor);
    }
    storage_->nextSoundOrder=1;return {DomainStatus::Applied,sequence};
}
bool EventOutbox::acknowledge(uint64_t sequence) {
    if (sequence == storage_->acknowledged) return true;
    if (sequence < storage_->acknowledged || storage_->batches.empty() || sequence > storage_->batches.back().sequence) return false;
    while (!storage_->batches.empty() && storage_->batches.front().sequence <= sequence) {
        storage_->facts -= storage_->batches.front().facts.size(); storage_->batches.pop_front();
    }
    storage_->acknowledged = sequence;
    return true;
}
void EventOutbox::observe(const EventBatch &batch) noexcept {
    for (const auto &fact : batch.facts) {
        if (storage_->historyNext == UINT64_MAX) return;
        DiagnosticEvent event;
        event.sequence = storage_->historyNext++; event.batch = batch.sequence; event.tick = batch.tick;
        event.transaction = batch.transaction.value; event.type = fact.index(); event.area = batch.audience.area;
        auto observeValue = [&]<class T>(const T &value) {
            if constexpr (std::is_same_v<T, AttackFact>) {
                event.actor = value.actor; event.target = value.target; event.value = value.skill; event.secondary = value.rank; event.position = value.destination;
            } else if constexpr (std::is_same_v<T, HitFact>) {
                event.target = value.target; event.value = value.life; event.secondary = value.killed; event.position = value.position;
            } else if constexpr (std::is_same_v<T, DeathFact>) {
                event.actor = value.victim; event.target = value.killer; event.area = value.area; event.value = value.goldLost;
            } else if constexpr (std::is_same_v<T, LifeFact>) { event.actor = value.actor; event.value = int64_t(value.life * 256.f); }
            else if constexpr (std::is_same_v<T, ManaFact>) { event.actor = value.actor; event.value = int64_t(value.mana.raw()); }
            else if constexpr (std::is_same_v<T, ResourcesFact>) {event.actor=value.actor;event.value=value.after.life.raw();event.secondary=value.after.mana.raw();}
            else if constexpr (std::is_same_v<T, ObjectFact>) {event.actor=value.actor;event.target=value.object;event.area=value.area;event.value=value.mode;event.secondary=value.shrine.value_or(-1);event.position=value.position;}
            else if constexpr (std::is_same_v<T, UnitRefreshFact>) {event.actor=value.actor;event.area=value.area;event.position=value.position;event.value=value.type;}
            else if constexpr (std::is_same_v<T, QuestClueFact>) {event.value=value.quest;}
            else if constexpr (std::is_same_v<T, SoundFact>) {event.actor=value.actor;event.value=value.sound;}
            else if constexpr (std::is_same_v<T, ItemTargetingFact>) {event.actor=value.source;event.value=value.cursor;event.secondary=value.skill;}
            else if constexpr (std::is_same_v<T, OverlayFact>) {event.actor=value.actor;event.value=value.overlay;}
            else if constexpr (std::is_same_v<T, QuestStatusFact>) {event.value=int(value.quest);event.secondary=value.status;}
            else if constexpr (std::is_same_v<T, QuestEffectFact>) event.value=value.effect;
            else if constexpr (std::is_same_v<T, UiFact>) {event.value=value.action;event.secondary=int(value.kind);}
            else if constexpr (std::is_same_v<T, MonsterClassHintFact>) {event.actor=value.actor;event.value=value.nativeClass;}
            else if constexpr (std::is_same_v<T, QuestAlertFact>) {event.actor=value.actor;event.value=value.type;}
            else if constexpr (std::is_same_v<T, StateFact>) { event.actor = value.actor; event.value = value.state; event.secondary = value.enabled; }
            else if constexpr (std::is_same_v<T, SkillPulseFact>) { event.actor=value.owner; event.target=value.target; event.value=value.skill; event.secondary=value.rank; event.position=value.position; }
            else if constexpr (std::is_same_v<T, SkillEndFact>) {event.actor=value.actor;event.value=value.skill;}
            else if constexpr (std::is_same_v<T, ItemSkillFact>) {event.actor=value.owner;event.target=value.target;event.value=value.skill;event.secondary=value.rank;event.position=value.position;}
            else if constexpr (std::is_same_v<T, GroundRemoveFact>) {event.target=value.item;}
            else if constexpr (std::is_same_v<T, GroundRestoredFact>) {event.target=value.item;event.secondary=int64_t(value.revision);}
            else if constexpr (std::is_same_v<T, NpcServiceFact>) {event.target=value.npc;event.value=value.result;}
            else if constexpr (std::is_same_v<T, MissileFact>) { event.actor = value.owner; event.value = value.definition; event.secondary = value.rank; event.position = value.position; }
            else if constexpr (std::is_same_v<T, RepositionFact>) { event.actor = value.actor; event.position = value.position; }
            else if constexpr (std::is_same_v<T, CharacterFact>) {
                event.actor = value.after.id; event.value = int64_t(value.after.experience); event.secondary = value.after.level;
            } else if constexpr (std::is_same_v<T, InventoryFact>) { event.actor = value.projection.player.id; event.value = int64_t(value.changes.size()); }
            else if constexpr (std::is_same_v<T, TradeFact>) {event.target=value.partner;event.value=value.action?int64_t(*value.action):-1;}
            else if constexpr (std::is_same_v<T, TradeItemsFact>) {event.actor=value.projection.player.id;event.value=int64_t(value.projection.inventory.items.size());event.secondary=int64_t(value.removed.size());}
            else if constexpr (std::is_same_v<T, InventoryResetFact>) {event.actor=value.projection.player.id;event.value=int64_t(value.projection.inventory.items.size());}
            else if constexpr (std::is_same_v<T, ChatRelationFact>) {event.actor=value.from;event.target=value.to;event.value=value.flags;}
            else if constexpr (std::is_same_v<T, PlayerMessageFact>) {event.value=value.type;}
            else if constexpr (std::is_same_v<T, PartyNoticeFact>) {event.actor=value.player;event.value=value.action;event.secondary=int64_t(value.recipient.value);}
            else if constexpr (std::is_same_v<T, GroundDropFact>) {
                event.target = value.item.id; event.value = value.item.quantity;
                const auto &at = std::get<GroundLocation>(value.item.location);
                event.area = at.region; event.position = at.position;
            }
            else if constexpr (std::is_same_v<T, TravelFact>) { event.actor = value.actor; event.value = int(value.from); event.secondary = int(value.to); event.position = value.position; }
        };
        switch (fact.kind()) {
#define D2X_FACT(Type) case FactKind::Type: observeValue(factGet<Type>(fact)); break;
#include "fact_catalog.inc"
#undef D2X_FACT
        case FactKind::Count: break; // A moved-from value is never published.
        }
        storage_->history[(event.sequence - 1) % storage_->history.size()] = event;
    }
}
std::vector<DiagnosticEvent> EventOutbox::history(uint64_t since, size_t limit) const {
    std::vector<DiagnosticEvent> result;
    if (since == UINT64_MAX) return result;
    for (auto sequence = std::max(historyFirst(), since + 1); sequence < storage_->historyNext && result.size() < limit; ++sequence)
        result.push_back(storage_->history[(sequence - 1) % storage_->history.size()]);
    return result;
}
}
