#pragma once
#include "calculation.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <span>
namespace d2x::server::attributes {
struct PassiveStatContribution {
    int skill{-1};
    CharacterModifiers modifiers;
    // Native passive callbacks SetStat directly, without ItemStatCost.ValShift.
    std::optional<std::map<StatKey,int32_t>> nativeValues;
};
struct NativeAttributeSources {
    std::shared_ptr<const StatList> root;
    StatSnapshot values;
    std::map<EntityId,StatSnapshot> items;
};
enum class NativeAttributeSampling { Full, SourcesOnly };
NativeAttributeSources prepareNativeAttributeSources(const CharacterDefinition &,const PersistentCharacter &,
    const ItemCatalog &,const StatRuleCatalog &,
    std::span<const EquipmentContribution>,std::span<const PassiveStatContribution>,const CharacterModifiers &,
    EntityId weapon = {},std::optional<StatTimeContext> time = {},std::optional<std::span<const ModifierStatSource>> temporarySources={},
    const std::map<EntityId,StatSnapshot> *itemFullStats=nullptr,NativeAttributeSampling sampling=NativeAttributeSampling::Full);
}
