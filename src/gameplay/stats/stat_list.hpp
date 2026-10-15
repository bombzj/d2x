#pragma once
#include "core/id.hpp"
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct StatKey {
    uint16_t id{},layer{};
    auto operator<=>(const StatKey &) const = default;
    constexpr uint32_t packed() const {return (uint32_t(id)<<16)|layer;}
};
struct StatValue {
    StatKey key;
    int32_t value{};
    StatValue()=default;
    StatValue(StatKey stat,int32_t raw):key(stat),value(raw) {}
    StatValue(int stat,int64_t raw,uint16_t layer=0);
    auto operator<=>(const StatValue &) const = default;
};
enum class StatOwnerKind { Player,Monster,Object,Missile,Item,Other };
enum class StatSourceKind { Base,Equipment,Set,Passive,Effect,Mode,State };
struct StatSource {
    StatSourceKind kind{StatSourceKind::Base};
    EntityId owner;
    int definition{-1};
    uint64_t occurrence{};
};
struct StatOperation {
    uint8_t code{},shift{};
    uint16_t source{};
    std::optional<uint16_t> base;
};
struct StatItemEventBinding {
    int16_t event{-1},function{};
};
struct StatRule {
    uint16_t id{};
    std::string name;
    uint8_t valueShift{};
    int32_t minimum{};
    bool hasMinimum{},keepZero{},damageRelated{};
    std::vector<StatOperation> operations;
    std::optional<StatOperation> producer;
    std::vector<uint16_t> targets;
    std::vector<uint16_t> baseDependents; // Native ItemStatCost.unk0x5E, in stat-ID order.
    std::array<StatItemEventBinding,2> itemEvents;
    bool direct{};
    std::optional<uint16_t> maximumStat;
};
struct StatRuleCatalog {
    std::map<uint16_t,StatRule> definitions;
    std::map<std::string,uint16_t,std::less<>> names;
};
// Lists keep their own base values and source identity. A source is withdrawn
// by omitting its list when the owning domain prepares the next snapshot.
struct StatTimeContext {int period{},baseTime{};};
struct StatList {
    StatSource source;
    StatOwnerKind ownerKind{StatOwnerKind::Other};
    bool hasOwner{},unitRoot{},extended{true},excludeDamage{},hasAct{},handWeapon{},hasUnit{};
    std::map<StatKey,int32_t> base;
    std::vector<std::shared_ptr<const StatList>> contributions;
    // Immutable input of the last native source calculation. The act clock
    // remains world-owned; querying a cached list does not advance that clock.
    std::optional<StatTimeContext> sampledActTime;
    // Stored raw FullStats of this immutable list, not a query memo. A value
    // candidate invalidates/replaces it when its own sources change.
    std::optional<std::map<StatKey,int32_t>> fullValues;
};
struct StatCharacterContext {
    int lifePerVitality{},manaPerEnergy{},staminaPerVitality{};
    uint16_t maximumStaminaStat{};
};
struct StatSnapshot;
struct StatQueryContext {
    const StatList *unit{};
    std::optional<StatCharacterContext> character;
    std::optional<StatTimeContext> time;
    const StatList *storedRoot{};
    const StatSnapshot *storedValues{}; // FullStats at the current mutation point.
};
struct StatSnapshot {
    std::map<StatKey,int32_t> values;
    int32_t get(StatKey key) const;
};
int32_t statByTimeAdjustment(int32_t packed,StatTimeContext time);
int32_t baseStatValue(const StatRuleCatalog &,const StatList &,StatKey);
// Base Stats never retain zero, even when the derived FullStats use KeepZero.
// Returns whether the stored value changed; unknown identities are rejected.
bool writeNativeBaseStat(std::map<StatKey,int32_t> &,const StatRuleCatalog &,StatKey,int32_t);
StatSnapshot evaluateStatList(const StatRuleCatalog &,const StatList &,StatQueryContext = {});
StatSnapshot evaluateStoredStatList(const StatRuleCatalog &,const StatList &,StatQueryContext = {});
StatSnapshot projectNativeStatValues(const StatRuleCatalog &,const StatList &,const StatSnapshot &fullStats);
std::shared_ptr<const StatList> statListWithFullValues(const StatList &,const StatSnapshot &);
using StatValueChanged=std::function<void(StatKey,int32_t,int32_t)>;
using StatRootRead=std::function<std::shared_ptr<const StatList>()>;
// The source graph has already been changed. Propagate one native base/list
// delta into an extended parent's FullStats, including ordered reverse ops.
void propagateNativeStatDelta(const StatRuleCatalog &,const StatList &,StatSnapshot &,
    StatValue,StatQueryContext={},const StatValueChanged &changed={},const StatRootRead &read={});
// Explicit sub_64A0 refresh used for item-applying ops on post/expire, without
// pretending that the unchanged source coefficient received an AddStat.
void refreshNativeStatValue(const StatRuleCatalog &,const StatList &,StatSnapshot &,
    StatKey,StatQueryContext={},const StatValueChanged &changed={});
StatSnapshot itemStatsAfterUnitChange(const StatRuleCatalog &,const StatList &,const StatSnapshot *previous,
    StatQueryContext parentContext={});
// Re-select the contributing hand without changing any authoritative source.
std::shared_ptr<const StatList> statListWithWeapon(const StatList &,EntityId,const StatRuleCatalog &,StatQueryContext={});
std::shared_ptr<const StatList> statListWithOwner(const StatList &,EntityId);
std::shared_ptr<const StatList> statListWithOwner(const StatList &,EntityId,const std::map<EntityId,EntityId> &itemIds);
}
