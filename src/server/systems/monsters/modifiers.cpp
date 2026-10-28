#include "system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/combat/participants.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "system_detail.hpp"
namespace d2x::server::monsters {
DomainResult<> System::prepareBirthModifiers(EntityId id,MonsterRank rank,std::vector<int> modifiers,
    std::span<const EntityId> minions,uint64_t tick,std::optional<std::pair<uint64_t,uint64_t>> random,bool promoteMinions,std::optional<uint16_t> nameSeed) {
    const auto *actor=find(id);
    if(!actor || (!actor->alive() && !actor->restorationPending) || modifiers.size()>9 ||
        (rank!=MonsterRank::Unique && rank!=MonsterRank::Champion && rank!=MonsterRank::SuperUnique &&
         !(nameSeed && (rank==MonsterRank::Minion || actor->restorationPending))))
        return {DomainStatus::InvalidRequest,{}};
    if(modifierPending_) return {DomainStatus::Conflict,{}};
    if(nextModifierToken_==UINT64_MAX || actor->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(random && actor->combatRandom!=random->first) return {DomainStatus::Stale,{}};
    ModifierPreparation request;request.token=nextModifierToken_;request.tick=tick;request.area=actor->area;
    request.promoteMinions=promoteMinions;
    request.generation=ports_.areas.at(actor->area).generation;request.expectedRandom=actor->combatRandom;
    request.nextRandom=random?random->second:actor->combatRandom;
    const auto currentLevel=[&](const Actor &unit) {
        attributes::MonsterResourceTrace resources(unit,tick);
        return resources.values(ports_.effects.unitStatSources(unit.id,tick)).get({unit.rule.stats->names.at("level"),0});
    };
    request.leader={id,actor->revision,actor->identity,currentLevel(*actor),actor->rule.nativeClass};request.leader.identity.rank=rank;
    request.selection.ids=std::move(modifiers);
    const bool unique=rank==MonsterRank::Unique || rank==MonsterRank::Champion || rank==MonsterRank::SuperUnique;
    const auto rolledName=unique?uint16_t(rollRandom(request.nextRandom)):uint16_t{};
    request.selection.nameSeed=nameSeed.value_or(rolledName);
    if(actor->restorationPending && unique && nameSeed) request.initialNameSeed=rolledName;
    for(const auto child:minions) {
        const auto *minion=find(child);
        if(!minion || minion->area!=actor->area || !minion->alive() || child==id) return {DomainStatus::Stale,{}};
        if(minion->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        request.minions.push_back({child,minion->revision,minion->identity,currentLevel(*minion),minion->rule.nativeClass,minion->rule.nativeModifiers});
    }
    modifierPending_=std::move(request);++nextModifierToken_;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::prepareModifiers(PreparedModifiers prepared) {
    if(!modifierPending_ || modifierPrepared_ || prepared.token!=modifierPending_->token) return {DomainStatus::Stale,{}};
    if(prepared.deferred.empty()) {
        const auto &request=*modifierPending_;
        const auto valid=[&](const MonsterRule &rule,const ModifierPreparation::Unit &unit) {
            return rule.nativeClass==unit.nativeClass && rule.difficulty==ports_.settings.difficulty &&
                rule.stats && rule.formulas && rule.enchantment &&
                rule.level==unit.level+rule.enchantment->levelBonus;
        };
        if(!valid(prepared.leader,request.leader) || prepared.leader.enchantment->ids!=request.selection.ids ||
            prepared.leader.enchantment->nameSeed!=request.selection.nameSeed || prepared.minions.size()!=request.minions.size())
            return {DomainStatus::InvalidRequest,{}};
        if(bool(prepared.initialAura)!=(request.initialNameSeed.has_value() && prepared.leader.enchantment->aura.has_value()))
            return {DomainStatus::InvalidRequest,{}};
        for(const auto &unit:request.minions) if(!prepared.minions.contains(unit.id) || !valid(prepared.minions.at(unit.id),unit))
            return {DomainStatus::InvalidRequest,{}};
    }
    modifierPrepared_=std::move(prepared);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::finishModifiers(TickContext context) {
    if(!modifierPending_) return StepStatus::Complete;
    if(!modifierPrepared_) return StepStatus::Blocked;
    if(!modifierPrepared_->deferred.empty()) return StepStatus::NotImplemented;
    const auto &request=*modifierPending_;const auto id=request.leader.id;const auto tick=context.tick;
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || ports_.areas.at(request.area).generation!=request.generation)
        throw std::logic_error("Native modifier recipient disappeared during preparation");
    auto &actor=found->second;
    if(!modifierCommitted_) {
    if(actor.revision!=request.leader.revision || actor.combatRandom!=request.expectedRandom)
        throw std::logic_error("Native modifier recipient changed during preparation");
    for(const auto &unit:request.minions) {
        const auto *current=find(unit.id);
        if(!current || current->revision!=unit.revision) throw std::logic_error("Native modifier minion changed during preparation");
    }
    auto rule=modifierPrepared_->leader;
    playerCountBonus(rule,actor.admittedPlayerCount);
    const auto upgrade=[&](const Actor &current,MonsterRule next,MonsterRank rank,bool unique) {
        auto candidate=current;candidate.rule=std::move(next);candidate.identity.rank=rank;
        candidate.rule.baseSkillRanks=current.rule.baseSkillRanks;
        candidate.rule.noTreasure=current.rule.noTreasure;candidate.rule.noExperience=current.rule.noExperience;
        candidate.rule.selfResurrection=current.rule.selfResurrection;candidate.rule.restore=current.rule.restore;
        candidate.rule.neverCount=current.rule.neverCount;
        candidate.rule.flying=current.rule.flying;
        candidate.rule.restoreNpcCommands=current.rule.restoreNpcCommands;
        candidate.rule.mapAi=current.rule.mapAi;
        candidate.rule.spawnCollision=current.rule.spawnCollision;
        candidate.rule.collisionPattern=current.rule.collisionPattern;
        candidate.rule.nativeSpawnFloor=current.rule.nativeSpawnFloor;
        candidate.rule.spawnAuxiliary=current.rule.spawnAuxiliary;
        candidate.rule.spawnAuxiliaryOnAdmissionOnly=current.rule.spawnAuxiliaryOnAdmissionOnly;
        const auto &mods=*candidate.rule.enchantment;
        attributes::MonsterResourceTrace resources(candidate,tick);
        const auto sources=ports_.effects.unitStatSources(current.id,tick);
        resources.sampleSources(sources);
        auto maximum=resources.read().maximum.raw();
        maximum=nativeAdd(maximum,nativePercentage(maximum,mods.lifePercent,100));
        resources.writeBaseStat("maxhp",maximum,sources);
        if(unique || rank==MonsterRank::Minion || mods.lifePercent)
            resources.seedLife(Fixed8::fromRaw(current.alive()?maximum:0));
        if(mods.stopRegeneration) resources.writeBaseStat("hpregen",0,sources);
        const auto value=[&](std::string_view name) {return resources.values(sources).get({candidate.rule.stats->names.at(std::string(name)),0});};
        resources.writeBaseStat("level",nativeAdd(value("level"),mods.levelBonus),sources);
        constexpr std::array<std::string_view,6> names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
        if(unique) for(const int modifier:mods.ids) {
            if(modifier==28) resources.writeBaseStat("armorclass",nativeMultiply(value("armorclass"),2),sources);
            if(modifier==37) resources.writeBaseStat("item_armor_percent",-70,sources);
            if(modifier==36) resources.writeBaseStat("damageresist",80,sources);
            if(modifier!=8 && modifier!=9 && modifier!=17 && modifier!=18 && modifier!=23 && modifier!=25 && modifier!=27 && modifier!=28) continue;
            std::array<int32_t,6> resistances;
            for(size_t channel=0;channel<names.size();++channel) resistances[channel]=value(names[channel]);
            int immunities=int(std::count_if(resistances.begin(),resistances.end(),[](int32_t resistance){return resistance>=100;}));
            const auto addResistance=[&](size_t channel,int amount) {
                const auto nextValue=nativeAdd(resistances[channel],amount);
                resources.writeBaseStat(names[channel],nextValue,sources);
                if(resistances[channel]<100 && nextValue>=100) ++immunities;
            };
            if(immunities>=2) continue;
            if(modifier==8 || modifier==27) {
                for(const size_t channel:{size_t{4},size_t{2},size_t{3}})
                    if(immunities<2 && resistances[channel]<(modifier==8?100:75)) addResistance(channel,modifier==8?40:20);
            } else {
                const size_t channel=modifier==9?2:modifier==17?3:modifier==18?4:modifier==23?5:modifier==25?1:0;
                addResistance(channel,modifier==25?20:modifier==28?50:75);
            }
        }
        if(mods.lifeScalePercent!=100) {
            maximum=resources.read().maximum.raw();
            maximum=nativeAdd(maximum,nativePercentage(maximum,nativeSubtract(mods.lifeScalePercent,100),100));
            resources.writeBaseStat("maxhp",maximum,sources);resources.seedLife(Fixed8::fromRaw(current.alive()?maximum:0));
        }
        auto change=resources.take();candidate.nativeStats=std::move(change.intrinsic);
        candidate.life=change.life.raw();candidate.maximumLife=change.maximum.raw();++candidate.revision;
        if(unique && mods.has(26)) {
            const auto *effects=ports_.effects.unitEffects(current.id);
            attributes::assignMonsterSkill(candidate,effects?*effects:CombatEffectSet{},184,1,tick,{});
        }
        return candidate;
    };
    std::map<EntityId,Actor> candidates;
    const auto rank=request.leader.identity.rank;
    const bool unique=rank==MonsterRank::Unique || rank==MonsterRank::Champion || rank==MonsterRank::SuperUnique;
    auto leader=upgrade(actor,std::move(rule),rank,unique);
    if(unique) leader.identity.ownerSpawnKey.clear();
    leader.combatRandom=request.nextRandom;
    leader.rule.nativeModifiers=request.selection.ids;
    candidates.emplace(id,std::move(leader));
    for(const auto &unit:request.minions) {
        const auto &minion=state_.actors.at(unit.id);
        auto next=modifierPrepared_->minions.at(unit.id);playerCountBonus(next,minion.admittedPlayerCount);
        next.nativeModifiers=next.enchantment->ids;
        candidates.emplace(unit.id,upgrade(minion,std::move(next),request.promoteMinions?MonsterRank::Minion:minion.identity.rank,false));
    }
    auto publication=prepareActorPublication(std::move(candidates),0);
    if(!validateActors(publication)) return StepStatus::Blocked;
    publishActors(publication);
    modifierCommitted_=true;
    }
    if(actor.alive() && modifierPrepared_->initialAura && !modifierInitialAuraCommitted_) {
        const auto &aura=*modifierPrepared_->initialAura;
        const auto assigned=ports_.effects.assignUnitAura(id,aura,tick,aura.rank);
        if(!assigned) return isDeferred(assigned.status) || assigned.status==DomainStatus::Conflict
            ?StepStatus::Blocked:StepStatus::NotImplemented;
        modifierInitialAuraCommitted_=true;
    }
    if(actor.alive() && actor.rule.enchantment->aura) {
        const auto &aura=*actor.rule.enchantment->aura;
        const auto assigned=ports_.effects.assignUnitAura(id,aura,tick,aura.rank);
        if(!assigned) return isDeferred(assigned.status) || assigned.status==DomainStatus::Conflict
            ?StepStatus::Blocked:StepStatus::NotImplemented;
    }
    modifierPending_.reset();modifierPrepared_.reset();modifierCommitted_=modifierInitialAuraCommitted_=false;
    return StepStatus::Complete;
}
} // namespace d2x::server::monsters
