#include "system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include <algorithm>
namespace d2x::server::effects {
DomainResult<> System::avoidance(const ActorContext &actor,WeaponAvoidance result,EntityId attacker) {return ports_.skills.avoidance(actor,result,attacker);}
CharacterModifiers System::unitModifiers(EntityId id,uint64_t tick) const {
    const auto it=units_.find(id);auto result=it==units_.end()?CharacterModifiers{}:it->second.states.modifiers(tick);
    if(const auto *unit=ports_.monsters.find(id);unit && unit->hireling) mergeCharacterModifiers(result,unit->potionEffects.modifiers(tick));
    return result;
}
int System::unitDefense(EntityId id,uint64_t tick) const {
    const auto *m=ports_.monsters.find(id);if(!m) return 0;
    const auto mods=unitModifiers(id,tick);
    return int(std::clamp<int64_t>((int64_t(m->rule.defense)+mods.defense)*std::max(0,100+mods.combat.defensePercent)/100,0,INT32_MAX));
}
int System::unitResistance(EntityId id,DamageType type,uint64_t tick) const {
    const auto *m=ports_.monsters.find(id);if(!m) return 0;
    const auto mods=unitModifiers(id,tick);
    const std::array additions{mods.combat.physicalResist,mods.combat.magicResist,mods.fireResist,mods.lightningResist,mods.coldResist,mods.poisonResist};
    const auto channel=size_t(type);const int base=m->rule.resistances[channel];
    // Conviction stores its immunity penalty when the recipient effect is
    // installed; applying it again here would divide the reduction twice.
    const int resistance=std::max(-100,base+additions[channel]);
    if(m->hireling && channel==0) return std::min(resistance,50);
    if(m->hireling && channel==1) return std::min(resistance,std::clamp(75+m->petStats.attributes.combat.magicMaxResist+mods.combat.magicMaxResist,0,95));
    if(m->hireling && channel>=2) {
        const auto &equipment=m->petStats.attributes.combat;
        const std::array maximum{equipment.fireMaxResist+mods.combat.fireMaxResist,equipment.lightningMaxResist+mods.combat.lightningMaxResist,
            equipment.coldMaxResist+mods.combat.coldMaxResist,equipment.poisonMaxResist+mods.combat.poisonMaxResist};
        return std::min(resistance,std::clamp(75+maximum[channel-2],0,95));
    }
    return resistance;
}
ResolvedDamage System::unitDamage(EntityId id,int64_t amount,DamageType type,uint64_t tick) const {
    const auto *m=ports_.monsters.find(id);const int resistance=unitResistance(id,type,tick);
    if(!m || !m->hireling) return {mitigateMonsterDamage(float(amount)/256.f,resistance),0};
    CharacterAttributes stats;stats.combat=m->petStats.attributes.combat;mergeCombatModifiers(stats.combat,unitModifiers(id,tick).combat);
    stats.fireResist=unitResistance(id,DamageType::Fire,tick);stats.lightningResist=unitResistance(id,DamageType::Lightning,tick);
    stats.coldResist=unitResistance(id,DamageType::Cold,tick);stats.poisonResist=unitResistance(id,DamageType::Poison,tick);
    stats.combat.physicalResist=unitResistance(id,DamageType::Physical,tick);stats.combat.magicResist=unitResistance(id,DamageType::Magic,tick);
    return mitigatePlayerDamage(float(amount)/256.f,type,stats);
}
std::map<int,std::vector<std::pair<int,int64_t>>> System::unitStateStats(EntityId id,uint64_t tick) const {
    const auto it=units_.find(id);if(it==units_.end()) return {};
    auto values=it->second.nativeStats;std::erase_if(values,[&](const auto &value){return !it->second.states.hasState(value.first,tick);});return values;
}
DomainResult<> System::amazonMagic(const ActorContext &actor,const SkillCastSpec &skill,std::optional<EntityId> emitter) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || !skill.amazonMagic || p->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.amazonMagic;auto next=units_;
    const auto *pet=emitter?ports_.monsters.find(*emitter):nullptr;
    if(emitter && (!pet || !pet->hireling || pet->owner!=actor.player || pet->area!=actor.area || pet->life<=0 || skill.manaCost || skill.charge)) return {DomainStatus::InvalidActor,{}};
    const Vec origin=pet?pet->position:p->position;const EntityId source=pet?pet->id:actor.actor;
    transactions::CharacterEdit debit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};debit.player.mana-=skill.manaCost;debit.charge=skill.charge;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        if(!m.enemyTarget() || m.life<=0 || m.area!=actor.area || !(program.filter&2) || !area->definition.activation.nearby(origin,m.position)) continue;
        const auto delta=m.position-origin;if(delta.x*delta.x+delta.y*delta.y>float(program.radius*program.radius)) continue;
        if((program.filter&0x200) && !area->definition.collision.missileSegment(origin,m.position,{4,1})) continue;
        if(program.state.curse && m.rule.enchantment && m.rule.enchantment->has(38)) continue;
        auto effect=amazonMagicEffect(program,source,skill.sourceId,skill.rank,unitModifiers(id,actor.tick).combat.curseResistance);
        if(!effect) continue;
        auto &target=next[id];target.area=actor.area;if(target.states.size()>=128) return {DomainStatus::Capacity,{}};
        if(!target.states.apply(std::move(*effect),actor.tick).accepted) continue;
        target.nativeStats[program.state.id]={{program.stat,program.defenseReduction?-int64_t(program.defenseReduction):program.slowPercent}};
        debit.publicFacts.emplace_back(StateFact{id,1,actor.area,program.state.id,true,target.nativeStats.at(program.state.id)});
    }
    auto plan=ports_.transactions.prepare(std::move(debit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) units_.swap(next);return result;
}
}
