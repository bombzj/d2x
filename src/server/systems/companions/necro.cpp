#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::companions {
StepStatus System::refreshNecro(Companion &pet,TickContext tick) {
    const auto *owner=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
    if(!owner || !body || !pet.sourceSkill || body->implementation==MonsterKind::BoneWall || !owner->rules.skills) return StepStatus::Complete;
    if(pet.ownerLevel==owner->persistent.player.level && pet.effectiveRanks==owner->totals.skillRanks && pet.hardRanks==owner->persistent.player.skillRanks) return StepStatus::Complete;
    if(body->revived()) {
        // SrvDo058 installs the owner's contributions on cast. The skill
        // update callback changes PetMax, not the existing pet's life roll.
        pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;
        return StepStatus::Complete;
    }
    auto skill=skills::evaluate(*owner,*pet.sourceSkill,pet.rank);if(!skill.summon) return StepStatus::Complete;
    {
        if(ports_.skills.busy(owner->actor,tick.tick)) return StepStatus::Complete;
        const auto &area=ports_.areas.at(owner->area);ActorContext actor{owner->player,owner->actor,owner->area,area.generation,UINT64_MAX-body->id.value-2,tick.tick};
        const auto waiting=pending_.find(owner->actor);
        if(waiting!=pending_.end() && waiting->second.refresh!=body->id) return StepStatus::Complete;
        if(waiting==pending_.end()) {
            Preparation source{actor,owner->inventoryRevision,body->combatRandom,skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,*skill.summon,body->rule};
            source.refresh=body->id;source.material=body->necroMaterial;pending_.emplace(owner->actor,std::move(source));return StepStatus::Complete;
        }
        const auto ready=prepared_.find(owner->actor);if(ready==prepared_.end()) return StepStatus::Complete;
        if(!ready->second.deferred.empty()) {cancel(owner->actor);return StepStatus::Complete;}
        auto candidate=ports_.monsters.prepareNecro(actor,ready->second.rule,body->position,ready->second.summon,ready->second.random,0,ready->second.equipmentSources);
        if(!candidate) return candidate.status==DomainStatus::Capacity?StepStatus::Blocked:StepStatus::Complete;
        auto updated=std::move(candidate.value->begin()->second);
        if(body->necroMaterial && updated.nativeStats && updated.nativeStats->sources) {
            auto replacement=*body;replacement.rule=updated.rule;replacement.petStats=updated.petStats;replacement.necroPet=updated.necroPet;
            if(replacement.revision==UINT64_MAX) return StepStatus::Blocked;
            ++replacement.revision;
            replacement.petWeapon=ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt;
            const auto target=statListWithOwner(*updated.nativeStats->sources,body->id);
            std::map<EntityId,monsters::Actor> actors;actors.emplace(body->id,std::move(replacement));
            auto units=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*units,std::move(actors),0);
            ports_.effects.stageSummonEvents(*units,body->id,skill.sourceId,skill.rank,*owner->rules.skills->formulas,tick.tick);
            ports_.effects.stageAuraSource(*units,body->id,[target,tick=tick.tick]
                (monsters::Actor &unit,const CombatEffectSet &states,const effects::AuraUnitPublication::SourceMutation &mutation) {
                    attributes::refreshMonsterStatSources(unit,target,states,tick,mutation);
                    unit.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(unit.nativeStats->values,*unit.rule.stats,"maxhp"));
                    unit.petStats.attributes.maxLife=std::max(1,unit.petStats.attributes.maxLifeFixed.raw()>>8);
                    const auto intrinsic=evaluateStatListWithSources(*unit.rule.stats,*unit.nativeStats->sources,{});
                    unit.petStats.attributes.combat=nativeCombatModifiers(intrinsic,*unit.rule.stats,unit.petStats.attributes.combat);
                });
            transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
            auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);
            if(!plan) return plan.status==DomainStatus::Capacity?StepStatus::Blocked:StepStatus::Complete;
            const auto result=ports_.transactions.commit(std::move(*plan.value));
            if(!result) return result.status==DomainStatus::Capacity?StepStatus::Blocked:StepStatus::Complete;
        } else {
            effects::AuraUnitPublication events;
            ports_.effects.stageSummonEvents(events,body->id,skill.sourceId,skill.rank,*owner->rules.skills->formulas,tick.tick);
            ports_.monsters.refreshNecro(body->id,std::move(updated.rule),std::move(updated.petStats),std::move(updated.necroPet),ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt,std::move(updated.nativeStats),tick.tick);
            ports_.effects.prepareStagedSkillEvents(events,tick.tick);ports_.effects.publishAuraUnits(events);
        }
        cancel(owner->actor);
    }
    pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;return StepStatus::Complete;
}
DomainResult<> System::revive(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    if(!ports_.monsters.consumableCorpse(id,actor.area,actor.tick,true)) return {DomainStatus::Unavailable,{}};
    const auto &owner=*ports_.players.find(actor.player);auto body=*ports_.monsters.find(id);
    const auto &spec=*skill.summon->necro;
    if(!skill.summon->native || !body.nativeStats) return {DomainStatus::Unavailable,{}};
    body.nativeStats=attributes::prepareReviveStatSources(body,*skill.summon,actor.tick,ports_.effects.unitStatSources(id,actor.tick));
    body.owner=actor.player;body.necroSummon=true;body.necroPet=skill.summon->necro;body.hostile=false;++body.allegianceRevision;++body.revision;
    body.rule.noTreasure=body.rule.noExperience=true;body.rule.corpseSelectable=false;body.rule.petType=skill.summon->petType;
    body.petRegistrationOrder=0;body.inactivePet=false;
    body.collisionPattern=nativeUnitCollisionPattern(body.rule.size,!body.rule.curseAiInteract);
    body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
    body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
    body.petStats.attributes.combat=nativeCombatModifiers(body.nativeStats->values,*body.rule.stats,body.petStats.attributes.combat);
    body.petStats.damageRegen=body.rule.damageRegen;
    body.deathTick=body.deathOccurrence=0;body.killer={};body.corpseUnavailable=true;body.rewardComplete=true;
    body.busyUntil=actor.tick;body.neutralAt.reset();body.route.clear();body.poison.reset();body.wound.reset();body.curseControl={};body.attractionTarget={};body.attractionUntil=0;
    body.aiResetTimers.clear();body.nativeAiResetTracked=false;body.specialTargetOrder=0;body.detachedMinion=true;body.aiFunctionRevision=0;body.aiThinkRevision=0;body.aiThinkAt=0;body.tauntTarget={};body.mainOwnerCleared=false;
    body.chilledUntil=body.frozenUntil=body.stunnedUntil=body.knockedUntil=body.reactionUntil=0;
    std::map<EntityId,monsters::Actor> actors;actors.emplace(id,std::move(body));auto next=state_;
    Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=spec.lifetimeFrames>0?actor.tick+uint64_t(spec.lifetimeFrames):UINT64_MAX;pet.ownerTeleport=owner.teleportRevision;pet.nextDecision=actor.tick+15;pet.ownerLevel=owner.persistent.player.level;pet.effectiveRanks=owner.totals.skillRanks;pet.hardRanks=owner.persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;for(auto &[key,existing]:next.companions) if(count>size_t(skill.summon->limit) && key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {const auto *old=ports_.monsters.find(key);if(old) {existing.removeAt=actor.tick+uint64_t(old->rule.deathTicks);retire.push_back(key);--count;}}
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.player.mana-=skill.manaCost;edit.charge=skill.charge;
    edit.publicFacts.emplace_back(StateFact{id,1,actor.area,spec.reviveState.id,true});
    const auto visual=spec.reviveVisuals.at(size_t(std::clamp(actors.at(id).rule.size,1,3)-1));edit.publicFacts.emplace_back(MissileFact{id,1,actor.area,visual.first,skill.rank,0,actors.at(id).position,actors.at(id).position});
    auto events=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*events,std::move(actors),0);
    ports_.effects.stageSummonEvents(*events,id,skill.sourceId,skill.rank,*owner.rules.skills->formulas,actor.tick);
    const auto retired=ports_.monsters.stageRetirements(*events,retire,actor.tick);if(!retired) return retired;
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},events);if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    state_.companions.swap(next.companions);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::synchronizeIronGolems(TickContext tick) {
    bool blocked=false;
    for(const auto &[key,owner]:ports_.players.all()) {
        (void)key;if(!owner.entered || !owner.persistent.ironGolem || !owner.rules.skills) continue;
        const auto &area=ports_.areas.at(owner.area);const ActorContext actor{owner.player,owner.actor,owner.area,area.generation,UINT64_MAX-1,tick.tick};
        bool present=false,dead=owner.persistent.player.hp<=0;
        for(const auto &[id,pet]:state_.companions) if(pet.owner==owner.player && pet.kind==Kind::IronGolem) {
            const auto *body=ports_.monsters.find(id);
            if(body && body->necroMaterial && body->necroMaterial->id==owner.persistent.ironGolem->id) {present=true;dead|=body->life<=0 || bool(pet.removeAt);}
        }
        if(dead) {
            transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.ironGolem=std::optional<ItemInstance>{};
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan || !ports_.transactions.commit(std::move(*plan.value))) blocked=true;continue;
        }
        if(present || ports_.skills.busy(owner.actor,tick.tick)) continue;
        const auto rank=owner.totals.skillRanks.find(90);if(rank==owner.totals.skillRanks.end() || rank->second<=0 || !owner.rules.skills->definitions.contains(90)) continue;
        auto skill=skills::evaluate(owner,90,rank->second);skill.manaCost=skill.startMana=0;skill.charge.reset();
        if(necro(actor,skill,owner.position,{},true).status==DomainStatus::Capacity) blocked=true;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
DomainResult<> System::necro(const ActorContext &actor,const SkillCastSpec &original,Vec target,EntityId corpse,bool restoring) {
    auto skill=original;
    const auto *owner=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(owner && skill.bone && skill.bone->barrier) {
        SummonCastSpec wall;wall.monster="bonewall";wall.kind=MonsterKind::BoneWall;wall.corpse=false;wall.limit=INT32_MAX;
        wall.stats=skill.bone->barrierStats.at(size_t(owner->persistent.difficulty));
        if(!skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
        wall.native=*skill.bone->barrierNative;wall.native->base=wall.stats;skill.summon=std::move(wall);
    }
    if(!owner || !owner->entered || owner->actor!=actor.actor || owner->area!=actor.area || owner->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || !skill.summon || skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &summon=*skill.summon;
    if(summon.necro && summon.necro->kind==NecroSummonKind::Revive) return revive(actor,skill,corpse);
    const bool iron=summon.necro && summon.necro->kind==NecroSummonKind::Iron;
    const auto material=iron?(restoring?owner->persistent.ironGolem:ports_.inventory.ironGolemMaterial(actor,target,corpse)):std::optional<ItemInstance>{};
    if(iron && !material) return {DomainStatus::Unavailable,{}};
    if(summon.corpse && !ports_.monsters.consumableCorpse(corpse,actor.area,actor.tick)) return {DomainStatus::Unavailable,{}};
    auto prior=pending_.find(actor.actor);
    if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
    if(prior==pending_.end()) {
        auto random=ports_.random;Preparation source{actor,owner->inventoryRevision,childRandom(random),skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,summon,{}};
        source.material=material;
        pending_.emplace(actor.actor,std::move(source));ports_.random=random;return {DomainStatus::Capacity,{}};
    }
    if(prior->second.inventoryRevision!=owner->inventoryRevision || prior->second.ownerLevel!=owner->persistent.player.level || prior->second.hardRanks!=owner->persistent.player.skillRanks || prior->second.actor.area!=actor.area || prior->second.actor.areaGeneration!=actor.areaGeneration) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    if(iron && (!prior->second.material || prior->second.material->id!=material->id || prior->second.material->revision!=material->revision)) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    const auto ready=prepared_.find(actor.actor);if(ready==prepared_.end()) return {DomainStatus::Capacity,{}};
    if(!ready->second.deferred.empty()) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
    const auto &rule=ready->second.rule;std::vector<Vec> positions;
    const auto place=[&](Vec center) {std::optional<Vec> position;
    for(int radius=0;radius<=4 && !position;++radius) for(int x=-radius;x<=radius && !position;++x) for(int y=-radius;y<=radius && !position;++y) {
        if(std::max(std::abs(x),std::abs(y))!=radius) continue;
        const Vec at{std::floor(center.x)+x+.5f,std::floor(center.y)+y+.5f};
        if(!area->definition.collision.walkable(at,rule.spawnCollision)) continue;
        bool occupied=false;
        for(const auto &[id,m]:ports_.monsters.read().actors) {(void)id;if(m.life>0 && m.area==actor.area && (m.position-at).length()<float((m.rule.size+rule.size)/2)) {occupied=true;break;}}
        if(!occupied) for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.persistent.player.hp>0 && p.area==actor.area && (p.position-at).length()<float((2+rule.size)/2)) {occupied=true;break;}}
        if(!occupied) for(const auto planned:positions) if((planned-at).length()<float(rule.size)) {occupied=true;break;}
        if(!occupied) position=at;
    }
    if(position) positions.push_back(*position);};
    if(skill.bone && skill.bone->prison) {
        constexpr Vec offsets[]{{-1,-4},{1,-4},{3,-3},{4,-1},{4,1},{3,3},{-1,4},{1,4},{-3,3},{-4,-1},{-4,1},{-3,-3}};
        for(const auto offset:offsets) place(target+offset);
    } else place(target);
    if(positions.empty() && !(skill.bone && skill.bone->prison)) return {DomainStatus::Unavailable,{}};
    std::map<EntityId,monsters::Actor> actors;auto random=ready->second.random;
    for(const auto position:positions) {auto candidate=ports_.monsters.prepareNecro(actor,rule,position,ready->second.summon,childRandom(random),actors.size(),ready->second.equipmentSources);if(!candidate) return {candidate.status,{}};
        if(iron) {auto &body=candidate.value->begin()->second;body.necroMaterial=material;body.petWeapon=ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt;}
        actors.merge(*candidate.value);}
    const EntityId id=actors.empty()?EntityId{}:actors.begin()->first;
    if(id && skill.bone && skill.bone->prison) {
        auto &root=actors.at(id);
        for(auto &[key,body]:actors) if(key!=id) {body.nativeMinionOwner=id;root.nativeMinions.insert(root.nativeMinions.begin(),key);}
    }
    const uint64_t expires=skill.bone && skill.bone->barrier?actor.tick+uint64_t(rule.summonLifeFrames):UINT64_MAX;
    auto next=state_;
    for(const auto &[key,body]:actors) {(void)body;Companion pet{key,actor.player,iron?Kind::IronGolem:Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=expires;pet.nextDecision=actor.tick+(skill.bone && skill.bone->barrier?25:1);pet.random=random;pet.ownerTeleport=owner->teleportRevision;pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;next.companions.emplace(key,std::move(pet));}
    const auto sameType=[&](const Companion &existing) {if(existing.sourceSkill==uint16_t(skill.sourceId)) return true;if(!summon.golem || !existing.sourceSkill) return false;const auto found=owner->rules.skills->definitions.find(*existing.sourceSkill);return found!=owner->rules.skills->definitions.end() && found->second.spec.summon && found->second.spec.summon->golem;};
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && sameType(existing) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) {
        if(count<=size_t(summon.limit)) break;
        if(existing.owner!=actor.player || !sameType(existing) || existing.removeAt) continue;
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);--count;
    }
    auto auraUnits=std::make_shared<effects::AuraUnitPublication>();
    ports_.effects.stageAuraActors(*auraUnits,std::move(actors),positions.size());
    for(const auto &[eventId,body]:auraUnits->actors->actors) {
        (void)body;ports_.effects.stageSummonEvents(*auraUnits,eventId,skill.sourceId,skill.rank,*owner->rules.skills->formulas,actor.tick);
    }
    if(iron) {
        const auto sourceProgram=ready->second.summon;
        const auto materialSources=ready->second.equipmentSources;
        const auto time=ports_.areas.statTime(actor.area);
        ports_.effects.stageAuraSource(*auraUnits,id,[sourceProgram,materialSources,time,tick=actor.tick]
            (monsters::Actor &body,const CombatEffectSet &,const effects::AuraUnitPublication::SourceMutation &mutation) {
                StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};binding.contributions=materialSources;
                const auto bound=statListWithOwner(binding,body.id);
                body.nativeStats=attributes::prepareSummonStatSources(body,sourceProgram,bound->contributions,time,tick,mutation);
                body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
                body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
                const auto intrinsic=evaluateStatListWithSources(*body.rule.stats,*body.nativeStats->sources,{});
                body.petStats.attributes.combat=nativeCombatModifiers(intrinsic,*body.rule.stats,body.petStats.attributes.combat);
            });
    }
    const auto retired=ports_.monsters.stageRetirements(*auraUnits,retire,actor.tick);if(!retired) return retired;
    std::shared_ptr<missiles::NativeBirthPublication> makers;
    if(skill.bone && skill.bone->barrier && !skill.bone->prison && skill.bone->sideCount>1) {
        auto direction=missileWallDirection(owner->position,positions.front());if(!direction.length()) direction={0,1};
        std::array<missiles::NativeSpawn,2> births;
        for(size_t index=0;index<births.size();++index) {
            auto &birth=births[index];birth.owner=actor.actor;birth.area=actor.area;birth.generation=actor.areaGeneration;birth.tick=actor.tick;
            birth.position=positions.front();birth.target=birth.position+direction*(index?-1.f:1.f);
            birth.definition=skill.missileId;birth.skill=skill.sourceId;birth.rank=skill.rank;
            birth.parameters={int32_t(uint32_t(id.value)),skill.bone->sideCount};
        }
        auto prepared=ports_.missiles.prepareNativeBirth(births,positions.size());if(!prepared) return {prepared.status,{}};
        makers=std::move(*prepared.value);
    }
    DomainResult<> debit;
    if(iron && !restoring) {auto cost=ports_.inventory.ironGolemCost(actor,skill,*material,auraUnits);if(!cost) return {cost.status,{}};debit=ports_.transactions.commit(std::move(*cost.value));}
    else {transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};edit.player.mana-=skill.manaCost;edit.charge=skill.charge;if(summon.golem && !iron && owner->persistent.ironGolem) edit.ironGolem=std::optional<ItemInstance>{};auto cost=ports_.transactions.prepare(std::move(edit),nullptr,{},auraUnits);if(!cost) return {cost.status,{}};cost.value->missileBirth=std::move(makers);debit=ports_.transactions.commit(std::move(*cost.value));}
    if(!debit) return debit;
    state_.companions.swap(next.companions);
    if(summon.corpse) ports_.monsters.consumeCorpse(corpse);
    cancel(actor.actor);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::necroStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->implementation==MonsterKind::BoneWall || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick) || body->stunnedUntil>tick.tick) return StepStatus::Complete;
    const auto &area=ports_.areas.at(owner->area);const ActorContext context{owner->player,owner->actor,owner->area,area.generation,0,tick.tick};
    if(body->area!=owner->area || missileDistance(body->position,owner->position)>50 || pet.ownerTeleport!=owner->teleportRevision) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            if(ports_.monsters.warpPet(pet.actor,context,{std::floor(owner->position.x)+x+.5f,std::floor(owner->position.y)+y+.5f})) {pet.ownerTeleport=owner->teleportRevision;pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(body->necroPet && body->necroPet->aura && !ports_.effects.hirelingAuraActive(body->id,body->necroPet->aura->skill,body->necroPet->aura->rank))
        if(ports_.effects.hirelingAura(body->id,*body->necroPet->aura,tick.tick).status==DomainStatus::Capacity) return StepStatus::Blocked;
    if(tick.tick<pet.nextDecision) return StepStatus::Complete;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId target;int nearest=36;
    for(const auto id:participants.enemies(body->id,body->area)) {
        const auto candidate=participants.find(id);
        const int distance=missileDistance(body->position,candidate.position());
        if(distance<nearest && area.definition.collision.segment(body->position,candidate.position())) {nearest=distance;target=id;}
    }
    if(target) {
        if(body->revived()) return StepStatus::Complete; // Its prepared original family AI owns combat decisions.
        const auto enemy=participants.find(target);
        const bool mage=body->necroPet && body->necroPet->kind==NecroSummonKind::Mage;
        if((mage && missileDistance(body->position,enemy.position())<=25) || meleeDistance(body->position,body->rule.size,enemy.position(),enemy.size())<=body->rule.meleeRange) {
            ports_.monsters.stop(pet.actor);
            const uint16_t skill=body->necroPet && body->necroPet->kind==NecroSummonKind::Mage && !body->rule.skillActions.empty()?body->rule.skillActions.begin()->first:0;
            const auto result=ports_.skills.requestCast({pet.actor,skill,UnitTarget{target,0,uint8_t(enemy.player?0:1)},tick.tick});
            if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
        } else ports_.monsters.requestMove({pet.actor,{body->area,area.generation,enemy.position()},target,body->rule.meleeRange,75,false});
    } else if(missileDistance(body->position,owner->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,owner->position},owner->actor,4,75,false});
    pet.nextDecision=tick.tick+uint64_t(std::max(1,body->rule.decisionTicks));return StepStatus::Complete;
}
DomainResult<bool> System::growBoneWall(EntityId source,EntityId root,int skillId,int rank,RegionId region,Vec position,uint64_t tick) {
    const auto *owner=ports_.players.findActor(source);const auto *parent=ports_.monsters.find(root);const auto *area=ports_.areas.find(region);
    if(!owner || !parent || !area || parent->implementation!=MonsterKind::BoneWall || !owner->rules.skills)
        return {DomainStatus::Unavailable,{}};
    const auto skill=skills::evaluate(*owner,skillId,rank);
    if(!skill.bone || !skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
    const auto &rule=parent->rule;
    position={std::floor(position.x)+.5f,std::floor(position.y)+.5f};
    const auto global=position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(owner->area,owner->position,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room || room->area!=region) return {DomainStatus::Applied,false};
    if(!area->definition.collision.walkable(position,rule.spawnCollision)) return {DomainStatus::Applied,false};
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        (void)id;if(unit.area==region && unit.alive() && missileUnitIntersection(position,position,rule.size,unit.position,unit.rule.size))
            return {DomainStatus::Applied,false};
    }
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;if(player.entered && player.area==region && player.persistent.player.hp>0 &&
            missileUnitIntersection(position,position,rule.size,player.position,2)) return {DomainStatus::Applied,false};
    }
    if(parent->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    SummonCastSpec summon;summon.monster="bonewall";summon.kind=MonsterKind::BoneWall;summon.limit=INT32_MAX;summon.petType=rule.petType;
    summon.stats=skill.bone->barrierStats.at(size_t(owner->persistent.difficulty));summon.native=*skill.bone->barrierNative;summon.native->base=summon.stats;
    ActorContext context{owner->player,source,region,area->generation,0,tick};
    auto random=ports_.random,seed=childRandom(random);limitedRandom(seed,1);
    auto prepared=ports_.monsters.prepareNecro(context,rule,position,summon,seed);if(!prepared) return {prepared.status,{}};
    const auto id=prepared.value->begin()->first;auto &body=prepared.value->begin()->second;body.nativeMinionOwner=root;
    auto updated=*parent;++updated.revision;updated.nativeMinions.insert(updated.nativeMinions.begin(),id);
    prepared.value->emplace(root,std::move(updated));
    auto next=state_;Companion pet{id,owner->player,Kind::Summon,uint16_t(skillId)};
    pet.rank=rank;pet.expires=tick+uint64_t(rule.summonLifeFrames);pet.nextDecision=tick+25;
    pet.ownerTeleport=owner->teleportRevision;pet.ownerLevel=owner->persistent.player.level;
    pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    effects::AuraUnitPublication publication;ports_.effects.stageAuraActors(publication,std::move(*prepared.value),1);
    ports_.effects.stageSummonEvents(publication,id,skillId,rank,*owner->rules.skills->formulas,tick);
    ports_.effects.prepareStagedSkillEvents(publication,tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(publication.facts)});if(!sent) return {sent.status,{}};}
    ports_.effects.publishAuraUnits(publication);state_.companions.swap(next.companions);ports_.random=random;
    return {DomainStatus::Applied,true};
}
}
