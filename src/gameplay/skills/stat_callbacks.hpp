#pragma once
#include "formula_parameters.hpp"
#include "core/id.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>
namespace d2x {
enum class SkillStatRefreshKind { None,Skill,Passives,Charges,Aura };
struct SkillStatRefresh {SkillStatRefreshKind kind{};int skill{};};
// One synchronous item-aura stat callback, in original source-write order.
// source identifies the timer custom ID; rank is the unit's aggregate layer.
// rank == 0 deactivates; every nonzero rank activates, including negatives.
struct SkillAuraChange {
    EntityId source;
    int skill{},rank{},state{-1},period{};
    bool immediate{};
    std::shared_ptr<const SkillFormulaSnapshot> formula; // Callback point, never the final equipment total.
};
// PeriodicStats custom ID is the source GUID, not (GUID, skill). Native
// activation replaces all timers of that source and appends the new timer.
struct ItemAuraTimer {
    EntityId source;
    int skill{},state{-1};
    uint64_t next{};
    uint64_t order{};bool continuous{};
};
enum class ItemAuraCallbackKind { RemoveState,CancelTimers,ScheduleTimer,ExecuteImmediate };
struct ItemAuraCallbackInstruction {
    ItemAuraCallbackKind kind;
    SkillAuraChange change;
    uint64_t due{}; // Used only by ScheduleTimer.
    bool continuous{};
};
std::vector<ItemAuraCallbackInstruction> itemAuraCallbackInstructions(const SkillAuraChange &,uint64_t tick);
struct ItemAuraCandidateHooks {
    std::function<void(const SkillAuraChange &)> removeState,executeImmediate;
    std::function<uint64_t()> reserveEventOrder;
};
// All mutations target the caller's unpublished unit candidate. Hooks run
// synchronously, may nest stat callbacks, and must not publish live state.
void executeItemAuraCallback(std::vector<ItemAuraTimer> &,const SkillAuraChange &,uint64_t tick,const ItemAuraCandidateHooks &);
void refreshItemAuraTimers(std::vector<ItemAuraTimer> &,std::span<const SkillAuraChange>,uint64_t tick);
// The firing callback samples the current aggregate layer, not the rank that
// scheduled the timer. Nonpositive ranks make the native callback cancel it.
int32_t itemAuraTimerRank(const StatRuleCatalog &,const StatSnapshot &,const ItemAuraTimer &);
// Queue 2 subscription: both native custom ID and GUID1 are the packed key,
// GUID2 is zero. Entries retain the newest-first native linked-list order.
struct ItemStatEvent {
    StatKey key;
    StatItemEventBinding binding;
};
void refreshItemStatEvents(std::vector<ItemStatEvent> &,const StatRuleCatalog &,StatKey,int32_t value);
struct SkillStatOwner {
    StatOwnerKind type{StatOwnerKind::Other};
    int playerClass{-1};
    bool hireling{};
    std::optional<int> convertedPlayerClass;
};
// ItemMode::ServerStatlistCallback classification, using prepared identities.
SkillStatRefresh skillStatRefresh(const StatRuleCatalog &,const SkillFormulaCatalog &,StatKey,SkillStatOwner);
}
