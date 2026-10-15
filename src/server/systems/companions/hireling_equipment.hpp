#pragma once
#include "preparation.hpp"
#include "server/player_state.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/systems/attributes/monster_sources.hpp"

namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::companions {
struct HirelingEquipment {
    EquipmentActor actor;
    CharacterModifiers modifiers;
    EquipmentStats equipment;
    Fixed8 life;
    std::shared_ptr<const StatList> statSources;
    StatSnapshot nativeStats;
    std::map<int,int> skillRanks;
};
EquipmentLoadout hirelingLoadout(const PersistentCharacter &,const ItemCatalog &,const EquipmentRules &);
HirelingEquipment calculateHirelingEquipment(const PersistentCharacter &,const ItemCatalog &,
    const EquipmentRules &,const PreparedHireling &,EntityId excluded={},const std::set<int> &states={},EntityId unit={},
    std::optional<Fixed8> currentLife={});
void prepareHirelingEquipmentSources(monsters::Actor &,const HirelingEquipment &,uint64_t tick,
    std::span<const ModifierStatSource>,bool initial=false,const attributes::MonsterAuraMutation &mutation={});
monsters::Actor prepareHirelingBody(const monsters::Actor &,const PersistentCharacter &,const PreparedHireling &,
    const ItemCatalog &,const EquipmentRules &,uint64_t inventoryRevision,uint64_t characterRevision,
    const std::set<int> &,std::span<const ModifierStatSource>,uint64_t tick,bool initial=false,
    const attributes::MonsterAuraMutation &mutation={});
}
