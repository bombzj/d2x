#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
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
    auto skill=skills::evaluate(*owner,*pet.sourceSkill,pet.rank);if(!skill.summon) return StepStatus::Complete;
    if(body->revived()) {
        auto stats=body->petStats;auto rule=body->rule;
        stats.attributes.maxLife=int(std::clamp<int64_t>(body->maximumLife*(100+skill.summon->necro->lifePercent)/(100+body->necroPet->lifePercent)/256,1,INT32_MAX));
        stats.attributes.combat.damagePercent=skill.summon->necro->damagePercent;
        ports_.monsters.refreshNecro(body->id,std::move(rule),std::move(stats),skill.summon->necro,{});
    } else {
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
        auto candidate=ports_.monsters.prepareNecro(actor,ready->second.rule,body->position,ready->second.summon,ready->second.random);
        if(!candidate) return candidate.status==DomainStatus::Capacity?StepStatus::Blocked:StepStatus::Complete;
        auto updated=std::move(candidate.value->begin()->second);
        ports_.monsters.refreshNecro(body->id,std::move(updated.rule),std::move(updated.petStats),std::move(updated.necroPet),ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt);
        cancel(owner->actor);
    }
    pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;return StepStatus::Complete;
}
DomainResult<> System::revive(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    if(!ports_.monsters.usableCorpse(id,actor.area,actor.tick,true)) return {DomainStatus::Unavailable,{}};
    const auto &owner=*ports_.players.find(actor.player);auto body=*ports_.monsters.find(id);auto random=body.combatRandom;
    const auto &spec=*skill.summon->necro;
    const auto base=body.rule.minimumLife+limitedRandom(random,uint32_t(std::max(0,body.rule.maximumLife-body.rule.minimumLife)+1));
    int64_t life=int64_t(base)*256;if(body.rule.level>owner.persistent.player.level) life=life*owner.persistent.player.level/body.rule.level;
    body.owner=actor.player;body.necroSummon=true;body.necroPet=skill.summon->necro;body.hostile=false;++body.allegianceRevision;++body.revision;
    body.life=body.maximumLife=std::max<int64_t>(256,life*(100+spec.lifePercent)/100);body.rule.level=std::min(body.rule.level,owner.persistent.player.level);
    body.rule.noTreasure=true;body.rule.corpseSelectable=false;body.rule.petType=skill.summon->petType;
    body.petStats.attributes.combat.damagePercent=spec.damagePercent;body.petStats.damageRegen=body.rule.damageRegen;
    body.deathTick=body.deathOccurrence=0;body.killer={};body.corpseUnavailable=true;body.rewardComplete=true;body.combatRandom=random;
    body.busyUntil=actor.tick;body.route.clear();body.poison.reset();body.wound.reset();body.slowed.reset();body.curseControl={};body.attractionTarget={};body.attractionUntil=0;
    body.chilledUntil=body.frozenUntil=body.stunnedUntil=body.knockedUntil=body.reactionUntil=0;
    std::map<EntityId,monsters::Actor> actors;actors.emplace(id,std::move(body));auto next=state_;
    Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=actor.tick+uint64_t(spec.lifetimeFrames);pet.ownerTeleport=owner.teleportRevision;pet.nextDecision=actor.tick+15;pet.ownerLevel=owner.persistent.player.level;pet.effectiveRanks=owner.totals.skillRanks;pet.hardRanks=owner.persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;for(auto &[key,existing]:next.companions) if(count>size_t(skill.summon->limit) && key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {const auto *old=ports_.monsters.find(key);if(old) {existing.removeAt=actor.tick+uint64_t(old->rule.deathTicks);retire.push_back(key);--count;}}
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.player.mana-=skill.manaCost;edit.charge=skill.charge;
    edit.publicFacts.emplace_back(StateFact{id,1,actor.area,spec.reviveState.id,true});
    const auto visual=spec.reviveVisuals.at(size_t(std::clamp(actors.at(id).rule.size,1,3)-1));edit.publicFacts.emplace_back(MissileFact{id,1,actor.area,visual.first,skill.rank,0,actors.at(id).position,actors.at(id).position});
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    ports_.monsters.commitRevive(std::move(actors));state_.companions.swap(next.companions);for(auto key:retire) ports_.monsters.retire(key,actor.tick);
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
        wall.stats=skill.bone->barrierStats.at(size_t(owner->persistent.difficulty));wall.stats.level=owner->persistent.player.level;
        wall.stats.attributes.maxLife=std::max(1,int(int64_t(wall.stats.attributes.maxLife)*(100+skill.bone->lifePercent)/100));skill.summon=wall;
    }
    if(!owner || !owner->entered || owner->actor!=actor.actor || owner->area!=actor.area || owner->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || !skill.summon || skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &summon=*skill.summon;
    if(summon.necro && summon.necro->kind==NecroSummonKind::Revive) return revive(actor,skill,corpse);
    const bool iron=summon.necro && summon.necro->kind==NecroSummonKind::Iron;
    const auto material=iron?(restoring?owner->persistent.ironGolem:ports_.inventory.ironGolemMaterial(actor,target,corpse)):std::optional<ItemInstance>{};
    if(iron && !material) return {DomainStatus::Unavailable,{}};
    if(summon.corpse && !ports_.monsters.usableCorpse(corpse,actor.area,actor.tick)) return {DomainStatus::Unavailable,{}};
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
    for(const auto position:positions) {auto candidate=ports_.monsters.prepareNecro(actor,rule,position,ready->second.summon,childRandom(random),actors.size());if(!candidate) return {candidate.status,{}};
        if(iron) {auto &body=candidate.value->begin()->second;body.necroMaterial=material;body.petWeapon=ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt;}
        actors.merge(*candidate.value);}
    const EntityId id=actors.empty()?EntityId{}:actors.begin()->first;
    const uint64_t expires=skill.bone && skill.bone->barrier?actor.tick+uint64_t(skill.bone->barrierFrames):UINT64_MAX;
    auto next=state_;auto growth=boneGrowth_;
    for(const auto &[key,body]:actors) {(void)body;Companion pet{key,actor.player,iron?Kind::IronGolem:Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=expires;pet.nextDecision=actor.tick+1;pet.random=random;pet.ownerTeleport=owner->teleportRevision;pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;next.companions.emplace(key,std::move(pet));}
    if(skill.bone && skill.bone->barrier && skill.bone->sideCount>1) {
        const Vec direction=missileWallDirection(owner->position,positions.front());
        for(int side:{-1,1}) growth.push_back({actor,summon,rule,id,positions.front(),direction*(float(side)*skill.missileVelocity),skill.bone->sideCount,actor.tick+1,actor.tick+uint64_t(std::lround(skill.missileLifetime*25.f)),expires,random,uint16_t(skill.sourceId),skill.rank});
    }
    const auto sameType=[&](const Companion &existing) {if(existing.sourceSkill==uint16_t(skill.sourceId)) return true;if(!summon.golem || !existing.sourceSkill) return false;const auto found=owner->rules.skills->definitions.find(*existing.sourceSkill);return found!=owner->rules.skills->definitions.end() && found->second.spec.summon && found->second.spec.summon->golem;};
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && sameType(existing) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) {
        if(count<=size_t(summon.limit)) break;
        if(existing.owner!=actor.player || !sameType(existing) || existing.removeAt) continue;
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);--count;
    }
    DomainResult<> debit;
    if(iron && !restoring) {auto cost=ports_.inventory.ironGolemCost(actor,skill,*material);if(!cost) return {cost.status,{}};debit=ports_.transactions.commit(std::move(*cost.value));}
    else {transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};edit.player.mana-=skill.manaCost;edit.charge=skill.charge;if(summon.golem && !iron && owner->persistent.ironGolem) edit.ironGolem=std::optional<ItemInstance>{};auto cost=ports_.transactions.prepare(std::move(edit));if(!cost) return {cost.status,{}};debit=ports_.transactions.commit(std::move(*cost.value));}
    if(!debit) return debit;
    ports_.monsters.commitAmazon(std::move(actors),0);state_.companions.swap(next.companions);boneGrowth_.swap(growth);
    if(summon.corpse) ports_.monsters.consumeCorpse(corpse);
    cancel(actor.actor);for(auto key:retire) ports_.monsters.retire(key,actor.tick);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::necroStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->implementation==MonsterKind::BoneWall || tick.tick<body->busyUntil || body->frozenUntil>tick.tick || body->stunnedUntil>tick.tick) return StepStatus::Complete;
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
    EntityId target;int nearest=36;
    for(const auto &[id,candidate]:ports_.monsters.read().actors) if(candidate.enemyTarget() && candidate.life>0 && candidate.area==body->area) {
        const int distance=missileDistance(body->position,candidate.position);
        if(distance<nearest && area.definition.collision.segment(body->position,candidate.position)) {nearest=distance;target=id;}
    }
    if(target) {
        if(body->revived()) return StepStatus::Complete; // Its prepared original family AI owns combat decisions.
        const auto *enemy=ports_.monsters.find(target);
        const bool mage=body->necroPet && body->necroPet->kind==NecroSummonKind::Mage;
        if((mage && missileDistance(body->position,enemy->position)<=25) || meleeDistance(body->position,body->rule.size,enemy->position,enemy->rule.size)<=body->rule.meleeRange) {
            ports_.monsters.stop(pet.actor);
            const uint16_t skill=body->necroPet && body->necroPet->kind==NecroSummonKind::Mage && !body->rule.skillActions.empty()?body->rule.skillActions.begin()->first:0;
            const auto result=ports_.skills.requestCast({pet.actor,skill,UnitTarget{target,0,1},tick.tick});
            if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
        } else ports_.monsters.requestMove({pet.actor,{body->area,area.generation,enemy->position},target,body->rule.meleeRange,75,false});
    } else if(missileDistance(body->position,owner->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,owner->position},owner->actor,4,75,false});
    pet.nextDecision=tick.tick+uint64_t(std::max(1,body->rule.decisionTicks));return StepStatus::Complete;
}
StepStatus System::advanceBoneGrowth(TickContext tick) {
    bool blocked=false;
    for(auto it=boneGrowth_.begin();it!=boneGrowth_.end();) {
        auto &growth=*it;const auto *root=ports_.monsters.find(growth.root);const auto *owner=ports_.players.find(growth.actor.player);
        if(!root || root->life<=0 || !owner || !owner->entered || owner->persistent.player.hp<=0 || tick.tick>=growth.until || !growth.remaining) {it=boneGrowth_.erase(it);continue;}
        if(tick.tick<growth.next) {++it;continue;}
        const Vec next=growth.position+growth.velocity*TickContext::seconds;
        const auto &area=ports_.areas.at(root->area);
        if(!area.definition.collision.missileSegment(growth.position,next,{5,1})) {it=boneGrowth_.erase(it);continue;}
        if(missileChangedCell(growth.position,next)) {
            const Vec at{std::floor(next.x)+.5f,std::floor(next.y)+.5f};bool occupied=false;
            for(const auto &[id,m]:ports_.monsters.read().actors) {(void)id;if(m.life>0 && m.area==root->area && (m.position-at).length()<float((m.rule.size+growth.rule.size)/2)) {occupied=true;break;}}
            for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.persistent.player.hp>0 && p.area==root->area && (p.position-at).length()<float((2+growth.rule.size)/2)) {occupied=true;break;}}
            if(!occupied && area.definition.collision.walkable(at,growth.rule.spawnCollision)) {
                auto actor=growth.actor;actor.tick=tick.tick;
                auto candidate=ports_.monsters.prepareNecro(actor,growth.rule,at,growth.summon,growth.random);
                if(candidate.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
                if(candidate) {
                    const auto id=candidate.value->begin()->first;
                    std::map<EntityId,Companion> added;Companion pet{id,actor.player,Kind::Summon,growth.skill};pet.rank=growth.rank;pet.expires=growth.expires;added.emplace(id,std::move(pet));
                    ports_.monsters.commitAmazon(std::move(*candidate.value),0);state_.companions.merge(added);--growth.remaining;
                }
            }
        }
        growth.position=next;growth.next=tick.tick+1;++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
