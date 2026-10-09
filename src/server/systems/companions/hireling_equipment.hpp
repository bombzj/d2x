#pragma once
#include "preparation.hpp"
#include "server/player_state.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_stats.hpp"

namespace d2x::server::companions {
struct HirelingEquipment {
    EquipmentActor actor;
    CharacterModifiers modifiers;
    EquipmentStats equipment;
    int life{};
};
inline EquipmentLoadout hirelingLoadout(const PersistentCharacter &state, const ItemCatalog &catalog, const EquipmentRules &rules) {
    EquipmentLoadout loadout;
    const int level=state.player.hireling.level;
    loadout.requirementPercent=[&rules,level](const ItemInstance &item) {
        int total=0;for(const auto &stat:rules.at(item.id,level).stats) if(!stat.layer && stat.effect=="item_req_percent") total+=stat.value;
        return total;
    };
    loadout.maximumDurability=[&rules,level](const ItemInstance &item){return rules.at(item.id,level).maximumDurability;};
    for(const auto &[id,item]:state.inventory.items) {
        (void)id;const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(!at || at->container!=state.containers.hirelingEquipment || at->cell.x<0 || at->cell.x>=int(EquipmentSlot::Count)) continue;
        loadout.equipped[size_t(at->cell.x)]={&item,catalog.find(item.definition)};
    }
    return loadout;
}
inline HirelingEquipment calculateHirelingEquipment(const PersistentCharacter &state, const ItemCatalog &catalog,
    const EquipmentRules &rules, const PreparedHireling &prepared, EntityId excluded={}) {
    auto loadout=hirelingLoadout(state,catalog,rules);
    HirelingEquipment result;
    result.actor={"",prepared.strength,prepared.dexterity,state.player.hireling.level,0,0,true};
    EquipmentContributionSource source{rules.sets,
        [&](const ItemInstance &item,int level){return rules.at(item.id,level).stats;},
        [&](size_t set,size_t bonus,int level){return rules.setBonuses.at({set,bonus}).at(size_t(level));},
        [&](EntityId item,size_t index,int level){return rules.at(item,level).setStats.at(index);}};
    std::set<EntityId> active;
    result.modifiers=deriveEquipmentModifiers(loadout,result.actor,source,excluded,&active);
    result.actor.strength+=result.modifiers.strength;result.actor.dexterity+=result.modifiers.dexterity;
    for(auto &item:loadout.equipped) if(item && !active.contains(item.instance->id)) item={};
    auto combat=result.modifiers.combat;
    combat.minimumDamage+=prepared.rule.minimumDamage;combat.maximumDamage+=prepared.rule.maximumDamage;
    result.equipment=deriveEquipmentStats(loadout,result.actor,prepared.rule.defense+result.modifiers.defense,combat,
        prepared.rule.attackRating+5*result.actor.dexterity+result.modifiers.attackRating);
    auto &weapon=result.equipment.weapons[0];
    if(!weapon.item) {
        weapon.ranged=true;weapon.weaponClass="bow";
        weapon.projectileMinimum=std::max(0,combat.minimumDamage)*256;
        weapon.projectileMaximum=std::max(combat.minimumDamage,combat.maximumDamage)*256;
        weapon.projectileDamagePercent=std::max(-90,result.actor.dexterity+combat.damagePercent+std::max(combat.minimumDamagePercent,combat.maximumDamagePercent));
        weapon.minimum=int(int64_t(weapon.projectileMinimum)*(100+weapon.projectileDamagePercent)/100);
        weapon.maximum=int(int64_t(weapon.projectileMaximum)*(100+weapon.projectileDamagePercent)/100);
        weapon.fasterAttack=combat.fasterAttack;
    }
    result.life=std::max(1,int(int64_t(prepared.rule.minimumLife+result.modifiers.maxLife)*(100+combat.lifePercent)/100));
    return result;
}
}