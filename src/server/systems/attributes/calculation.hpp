#pragma once
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/contracts.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/units/resources.hpp"
#include <optional>
#include <functional>
#include <span>
#include <set>
namespace d2x { struct PersistentCharacter; }
namespace d2x::server::attributes {
struct AuraSourceObservation {
    std::vector<ModifierStatSource> sources;
    std::set<int> states;
    std::shared_ptr<const CombatEffectSet> effects;
    std::optional<ResourceValues> resources; // A direct unit-resource write at this boundary.
    std::vector<int> withdrawnPassiveStates{}; // BasicAura withdraws this separate cached list after metadata.
    struct ItemWrite {EntityId item;StatValue stat;};
    std::vector<ItemWrite> itemWrites{}; // A forced do program's ordered item SetBaseStat.
};
using AuraSourceObserver=std::function<void(AuraSourceObservation)>;
using AuraSourceReader=std::function<SkillFormulaSnapshot()>;
// Runs inside one stat callback on unpublished unit candidates. The observer
// must be called after each source withdrawal/post/write, in native order.
using AuraMutation=std::function<void(const SkillAuraChange &,const AuraSourceObserver &,std::vector<ItemAuraTimer> &,const AuraSourceReader &)>;
using InitialAuraMutation=std::function<void(const AuraSourceObserver &,const AuraSourceReader &)>;
enum class PassiveCacheRefresh { Refresh,Retain };
struct Totals {
    CharacterAttributes character;
    EquipmentStats equipment;
    std::map<int, int> skillRanks;
    std::set<EntityId> activeEquipment;
    uint64_t sourceRevision{};
    std::vector<ChargedSkill> chargedSkills;
    std::vector<EquipmentContribution> contributions;
    std::shared_ptr<const StatList> statSources;
    StatSnapshot nativeStats;
    StatSnapshot nativeFullStats; // Stored raw FullStats, before MinAccr queries.
    std::map<EntityId,StatSnapshot> handStats;
    std::map<EntityId,StatSnapshot> itemStats;
    std::map<EntityId,StatSnapshot> itemFullStats; // Each posted item's stored FullStats.
    std::optional<StatTimeContext> time; // Input snapshot used for these totals.
    std::vector<ResourceMaximums> resourceMaximums; // Native source-write order.
    std::vector<SkillPetLimitChange> petLimitChanges; // This calculation's callback order; never serialized.
    std::map<EntityId,bool> equipmentNoEquip;
    std::vector<std::string> equipmentSetSlots; // Native unit has six ITEMSET state slots.
    std::vector<NativeSkillSlot> skillOrder;
    std::vector<NativeSkillSlot> clearedSkillSelections;
    std::vector<SkillUseRemoval> clearedSkillUses;
    std::vector<SkillAuraChange> itemAuraChanges; // This calculation's ordered callbacks, not a persistent aura total.
    std::vector<ItemAuraTimer> itemAuraTimers; // Ordered runtime registrations, never serialized to D2S.
    std::vector<ItemStatEvent> itemEvents; // Published registration order; handlers are still domain-owned.
    std::optional<TransientAttributes> auraTransient; // Produced only by a bound synchronous candidate mutation.
    std::optional<ResourceValues> auraResources; // Final candidate values, not a replayable callback ledger.
    bool equipmentRefreshed{};
    EntityId registeredWeapon; // Candidate value published into the equipment container.
};
EquipmentLoadout loadout(const PersistentCharacter &, const ItemCatalog &, const EquipmentRules &);
// previous borrows cached passive lists for this synchronous calculation;
// it is never retained or mutated. Native temporal sources replace the legacy
// aggregate when supplied, including an explicitly empty source collection.
// resourceBase is the maximum at the supplied current-resource sample point;
// transactions pass it after replaying earlier state/source callbacks.
Totals calculate(const CharacterDefinition &, const PersistentCharacter &, const ItemCatalog &,
    const EquipmentRules &, const CharacterRules &, EntityId excluded = {}, const CharacterModifiers &temporary = {}, const std::set<int> &states = {},
    std::optional<std::span<const ModifierStatSource>> temporarySources={},const Totals *previous=nullptr,std::optional<StatTimeContext> time={},bool refreshEquipment=false,
    std::optional<ResourceMaximums> resourceBase={},std::span<const EntityId> registeredInsertions={},const AuraMutation &auraMutation={},const InitialAuraMutation &initialAura={},PassiveCacheRefresh passiveRefresh=PassiveCacheRefresh::Refresh,const PersistentCharacter *previousState=nullptr,const InitialAuraMutation &finalAura={},
    const std::function<void(const SkillPetLimitChange &)> &petLimitChanged={},bool refreshPetLists=false);
}
