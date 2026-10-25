#pragma once
#include "equipment_stats.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <map>

namespace d2x {
struct SkillFormulaCatalog;
struct NativeEquipmentInput {
    enum class MonsterBlock { RequireShield,Always,Never };
    const StatRuleCatalog &rules;
    // Actor snapshots for each selected active weapon; ID zero is unarmed.
    const std::map<EntityId,StatSnapshot> &hands,&items;
    const SkillFormulaCatalog &formulas;
    const CombatModifiers &procedures;
    int attackRatingClassFactor{};
    MonsterBlock monsterBlock{MonsterBlock::RequireShield};
    std::optional<EntityId> registeredWeapon{};
};
EquipmentStats deriveNativeEquipmentStats(const EquipmentLoadout &,const EquipmentActor &,const NativeEquipmentInput &);
}
