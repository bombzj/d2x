#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/monsters/native_spawn.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
#include <cmath>
#include "core/random.hpp"
namespace d2x::server::companions {
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for(const auto &[id,source]:pending_) if(!prepared_.contains(id)) result.push_back(source);
    return result;
}
void System::cancel(EntityId actor) {pending_.erase(actor);prepared_.erase(actor);}
DomainResult<> System::install(Prepared prepared) {
    const auto it=pending_.find(prepared.source.actor.actor);
    if(it==pending_.end() || !prepared.source.token || it->second.token!=prepared.source.token ||
        it->second.actor.sequence!=prepared.source.actor.sequence || it->second.seed!=prepared.source.seed) return {DomainStatus::Stale,{}};
    const auto &source=it->second;
    if(prepared.deferred.empty() && (source.summon.amazon || source.summon.druid ||
        (source.summon.assassin && !source.summon.assassin->definition->warp))) {
        if(!prepared.components || !prepared.summon.native || !source.summon.native) return {DomainStatus::InvalidRequest,{}};
        auto random=source.seed;
        const auto components=chooseMonsterComponents(prepared.rule.componentCounts,{},random);
        int minimum=source.summon.native->base.attributes.maxLifeFixed.raw()>>8,maximum=minimum;
        if(source.summon.amazon) {minimum=source.summon.amazon->lifeMinimum;maximum=source.summon.amazon->lifeMaximum;}
        else if(source.summon.druid) {
            const auto range=source.summon.druid->definition->lifeRanges.at(size_t(source.difficulty));
            minimum=range.first;maximum=range.second;
        }
        if(minimum<0 || maximum<minimum) return {DomainStatus::InvalidRequest,{}};
        const int life=rollMonsterLife(minimum,maximum,random);
        if(source.summon.amazon && !source.summon.amazon->decoy) {
            const auto &equipment=source.summon.amazon->equipment;
            size_t first=0;while(first<equipment.size() && equipment[first].rank>source.rank) ++first;
            for(size_t index=first;index<equipment.size();++index) limitedRandom(random,1);
        }
        if(components!=*prepared.components || random!=prepared.random || prepared.summon.native->initialLife.raw()!=nativeShiftLeft(std::min(life,(1<<23)-1),8))
            return {DomainStatus::InvalidRequest,{}};
    }
    if(prepared.deferred.empty() && it->second.ownerRandom) {
        if(!prepared.ownerRandom || !prepared.components || !prepared.componentPalettes || !it->second.componentPalettes)
            return {DomainStatus::InvalidRequest,{}};
        auto random=*it->second.ownerRandom;
        if(it->second.summon.necro && it->second.summon.necro->kind==NecroSummonKind::Mage) {
            const auto variant=int(rollRandom(random)&3);
            if(prepared.rule.petVariant!=variant) return {DomainStatus::InvalidRequest,{}};
        }
        if(random!=*prepared.ownerRandom) return {DomainStatus::InvalidRequest,{}};
        auto palettes=*it->second.componentPalettes;
        auto unitRandom=it->second.seed;
        const auto components=chooseRegionMonsterComponents(prepared.rule.componentCounts,prepared.rule.nativeClass,prepared.rule.totalPieces,palettes,unitRandom);
        limitedRandom(unitRandom,1);
        if(components!=*prepared.components || palettes!=*prepared.componentPalettes || unitRandom!=prepared.random)
            return {DomainStatus::InvalidRequest,{}};
    }
    if(prepared.deferred.empty() && prepared.equipmentRandom) {
        if(!it->second.birthRandom || prepared.allocatedEquipment!=prepared.equipment.inventory.items.size()) return {DomainStatus::InvalidRequest,{}};
        auto random=it->second.birthRandom->second;
        for(size_t index=0;index<prepared.allocatedEquipment;++index) {rollNativeItemSeeds(random);}
        if(random!=*prepared.equipmentRandom) return {DomainStatus::InvalidRequest,{}};
    } else if(prepared.deferred.empty() && (prepared.allocatedEquipment || !prepared.equipment.inventory.items.empty())) return {DomainStatus::InvalidRequest,{}};
    prepared_.insert_or_assign(it->first,std::move(prepared));return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::amazon(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    const auto *owner=ports_.players.find(actor.player);
    if(!owner || !owner->entered || owner->actor!=actor.actor || owner->area!=actor.area || owner->persistent.player.hp<=0 ||
        !skill.summon || !skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
    auto summon=*skill.summon;const auto &program=*summon.amazon;
    const auto prepared=owner->rules.skills->amazonPetRules.find(skill.sourceId);
    if(prepared==owner->rules.skills->amazonPetRules.end()) return {DomainStatus::Unavailable,{}};
    // SkillAma::SrvDo015 creates no MonEquip inventory. Decoy's native
    // disguise/owner information is separate from Valkyrie's real equipment.
    std::shared_ptr<PersistentCharacter> equipment;
    auto rule=prepared->second;
    WeaponDamage weapon;uint64_t petRandom{};
    std::optional<uint64_t> gameRandom;
    std::vector<std::shared_ptr<const StatList>> equipmentSources;
    {
        auto prior=pending_.find(actor.actor);
        if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
        if(prior==pending_.end()) {
            if(nextPreparation_==UINT64_MAX) return {DomainStatus::Capacity,{}};
            auto random=ports_.random;Preparation source{actor,owner->inventoryRevision,childRandom(random),skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,summon,rule};
            source.birthRandom=std::pair{ports_.random,random};
            source.effectiveRanks=owner->totals.skillRanks;source.token=nextPreparation_++;
            pending_.emplace(actor.actor,std::move(source));return {DomainStatus::Capacity,{}};
        }
        if(prior->second.rank!=skill.rank || prior->second.effectiveRanks!=owner->totals.skillRanks) {cancel(actor.actor);return amazon(actor,skill,target);}
        if(prior->second.inventoryRevision!=owner->inventoryRevision || prior->second.ownerLevel!=owner->persistent.player.level || prior->second.hardRanks!=owner->persistent.player.skillRanks || prior->second.actor.area!=actor.area || prior->second.actor.areaGeneration!=actor.areaGeneration) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
        if(prior->second.birthRandom && prior->second.birthRandom->first!=ports_.random) {cancel(actor.actor);return amazon(actor,skill,target);}
        const auto ready=prepared_.find(actor.actor);if(ready==prepared_.end()) return {DomainStatus::Capacity,{}};
        if(!ready->second.deferred.empty()) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
        if(!program.decoy) equipment=std::make_shared<PersistentCharacter>(ready->second.equipment);
        summon=ready->second.summon;rule=ready->second.rule;weapon=ready->second.weapon;petRandom=ready->second.random;equipmentSources=ready->second.equipmentSources;
        gameRandom=ready->second.equipmentRandom?ready->second.equipmentRandom:
            prior->second.birthRandom?std::optional{prior->second.birthRandom->second}:std::nullopt;
    }
    const auto global=target+area.definition.origin;
    const auto room=ports_.areas.roomAtPosition(actor.area,owner->position,int(global.x),int(global.y));
    if(!room || room->area!=actor.area) return {DomainStatus::Unavailable,{}};
    auto roomRandom=ports_.areas.roomRandom(*room);if(!roomRandom) return {roomRandom.status,{}};
    const auto collision=ports_.spatial.nativeCollisionSample();
    monsters::Admission admission;admission.identity.monster=summon.monster;admission.identity.spawnKey="amazon.search";
    admission.area=actor.area;admission.position=target;admission.rule=rule;admission.tick=actor.tick;
    auto point=ports_.monsters.searchRoomSpawn(admission,*roomRandom.value,-1,collision);
    if(!point) return {point.status,{}};
    if(!*point.value) point=ports_.monsters.searchRoomSpawn(admission,*roomRandom.value,4,collision);
    if(!point) return {point.status,{}};
    if(!*point.value) {
        const auto consumed=ports_.transactions.consumeRoomRandom(*roomRandom.value);
        return {consumed?DomainStatus::Unavailable:consumed.status,{}};
    }
    const Vec position=**point.value;
    size_t itemCount=0;
    const auto count=[&](auto &&self,const ItemInstance &item)->void {++itemCount;for(const auto &child:item.socketedItems) self(self,child);};
    if(equipment) for(const auto &[id,item]:equipment->inventory.items) {(void)id;count(count,item);}
    // IDs and candidate maps are prepared before the mana transaction.
    auto idCandidate=ports_.monsters.prepareNativePet(actor,rule,position,summon,equipment,itemCount,program.decoy?std::optional<WeaponDamage>{}:weapon,equipmentSources,petRandom);
    if(!idCandidate) return {idCandidate.status,{}};
    auto actors=std::move(*idCandidate.value);const auto id=actors.begin()->first;
    if(const auto ready=prepared_.find(actor.actor);ready!=prepared_.end() && ready->second.components)
        actors.at(id).components=*ready->second.components;
    auto next=state_;Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};
    pet.rank=skill.rank;pet.expires=program.decoy?actor.tick+uint64_t(std::max(1,program.lifetimeFrames)):UINT64_MAX;pet.nextDecision=actor.tick+(program.decoy?25:20);
    next.companions.emplace(id,std::move(pet));
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) if(key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);
    }
    std::vector<std::shared_ptr<const StatList>> boundEquipment;
    for(const auto &source:actors.at(id).nativeStats->sources->contributions)
        if(source && (source->source.kind==StatSourceKind::Equipment || source->source.kind==StatSourceKind::Set)) boundEquipment.push_back(source);
    auto units=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*units,std::move(actors),itemCount+1);
    ports_.effects.stageAuraSource(*units,id,[summon,boundEquipment,time=ports_.areas.statTime(actor.area),tick=actor.tick]
        (monsters::Actor &unit,const CombatEffectSet &,const effects::AuraUnitPublication::SourceMutation &mutation) {
            unit.nativeStats=attributes::prepareSummonStatSources(unit,summon,boundEquipment,time,tick,mutation);
            unit.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(unit.nativeStats->values,*unit.rule.stats,"maxhp"));
            unit.petStats.attributes.maxLife=std::max(1,unit.petStats.attributes.maxLifeFixed.raw()>>8);
            const auto intrinsic=evaluateStatListWithSources(*unit.rule.stats,*unit.nativeStats->sources,{});
            unit.petStats.attributes.combat=nativeCombatModifiers(intrinsic,*unit.rule.stats,unit.petStats.attributes.combat);
        });
    const auto retired=ports_.monsters.stageRetirements(*units,retire,actor.tick);if(!retired) return retired;
    transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
    edit.player.mana-=skill.manaCost;edit.charge=skill.charge;
    auto cost=ports_.transactions.prepare(std::move(edit),nullptr,{},units);if(!cost) return {cost.status,{}};
    cost.value->roomRandom=std::make_shared<const RoomRandomLoan>(*roomRandom.value);
    if(gameRandom) cost.value->gameRandom=transactions::UnitRandomEdit{pending_.at(actor.actor).birthRandom->first,*gameRandom};
    const auto debit=ports_.transactions.commit(std::move(*cost.value));if(!debit) return debit;
    state_.companions.swap(next.companions);
    cancel(actor.actor);
    return {DomainStatus::Applied,std::monostate{}};
}
}
