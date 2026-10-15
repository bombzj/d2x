#pragma once
#include "equipment_contributions.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <functional>
#include <map>
namespace d2x {
class ItemCatalog;
struct ItemInstance;
using NativeItemLookup=std::function<const ItemInstance &(EntityId)>;
struct NativeEquipmentSources {
    std::vector<std::shared_ptr<const StatList>> contributions;
    std::map<EntityId,std::shared_ptr<StatList>> items;
};
NativeEquipmentSources prepareNativeEquipmentSources(const ItemCatalog &,const StatRuleCatalog &,
    std::span<const EquipmentContribution>,EntityId owner,const NativeItemLookup &,EntityId weapon = {},StatOwnerKind ownerKind=StatOwnerKind::Player,
    const std::map<EntityId,StatSnapshot> *itemFullStats=nullptr);
}
