#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::companions {
DomainResult<EntityId> System::summon(const Summon &) {return {};}
DomainResult<> System::ownerDied(const ActorContext &actor) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area ||
        player->persistent.player.hp > 0 || !area || area->generation != actor.areaGeneration)
        return {DomainStatus::InvalidActor, {}};
    if (player->persistent.player.hireling.sourceRow >= 0 && player->persistent.player.hireling.hp > 0) {
        auto record = player->persistent.player;
        record.hireling.hp = 0;
        auto plan = ports_.transactions.prepare(transactions::CharacterEdit{
            actor, player->inventoryRevision, player->characterRevision, std::move(record)});
        if (!plan) return {plan.status, {}};
        const auto committed = ports_.transactions.commit(std::move(*plan.value));
        if (!committed) return committed;
    }
    for (auto &[id, pet] : state_.companions) {
        if (pet.owner != actor.player) continue;
        const auto *body = ports_.monsters.find(id);
        if (!body) continue;
        pet.release = 0;
        if (body->life > 0) ports_.monsters.retire(id, actor.tick);
        if (pet.kind != Kind::Hireling && !pet.removeAt)
            pet.removeAt = actor.tick + uint64_t(body->rule.deathTicks);
    }
    return {DomainStatus::Applied, std::monostate{}};
}
bool System::canDismiss(const ActorContext &actor,EntityId id) const {
    const auto *p=ports_.players.find(actor.player);const auto pet=state_.companions.find(id);const auto *body=ports_.monsters.find(id);
    return p && p->entered && p->actor==actor.actor && p->area==actor.area && p->persistent.player.hp>0 &&
        p->rules.skills && ports_.areas.at(actor.area).generation==actor.areaGeneration && body && body->life>0 && body->area==actor.area &&
        body->owner==actor.player && pet!=state_.companions.end() && pet->second.owner==actor.player && !pet->second.removeAt &&
        pet->second.sourceSkill && p->rules.skills->dismissibleSummons.contains(*pet->second.sourceSkill);
}
DomainResult<> System::dismiss(const ActorContext &actor,EntityId id) {
    if(!canDismiss(actor,id)) return {DomainStatus::InvalidRequest,{}};
    auto &pet=state_.companions.at(id);const auto *body=ports_.monsters.find(id);
    // Ownership retirement never enters enemy experience or loot accounting.
    pet.removeAt=actor.tick+uint64_t(body->rule.deathTicks);pet.release=0;
    ports_.monsters.retire(id,actor.tick);return {DomainStatus::Applied,std::monostate{}};
}

DomainResult<> System::hydra(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    const auto *p=ports_.players.find(actor.player);if(!p || !p->rules.skills || !p->rules.skills->hydra || skill.hydraLimit<=0 || skill.hydraFrames<=0) return {DomainStatus::Unavailable,{}};
    auto actors=ports_.monsters.prepareHydra(actor,*p->rules.skills->hydra,target);if(!actors) return {actors.status,{}};
    auto next=state_;auto random=ports_.random;
    for(const auto &[id,monster]:*actors.value) {
        const auto &head=*std::find_if(p->rules.skills->hydra->heads.begin(),p->rules.skills->hydra->heads.end(),[&](const auto &head) {return head.nativeClass==monster.rule.nativeClass;});
        Companion entry{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};
        entry.rank=skill.rank;entry.attackSkill=head.attackSkill;entry.expires=actor.tick+uint64_t(skill.hydraFrames);
        entry.nextDecision=monster.riseUntil;entry.random=childRandom(random);next.companions.emplace(id,std::move(entry));
    }
    size_t count=0;for(const auto &[id,pet]:next.companions) {(void)id;if(pet.owner==actor.player && pet.sourceSkill==uint16_t(skill.sourceId) && !pet.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[id,pet]:next.companions) {
        if(count<=size_t(skill.hydraLimit)) break;
        if(pet.owner!=actor.player || pet.sourceSkill!=uint16_t(skill.sourceId) || pet.removeAt) continue;
        const auto *monster=ports_.monsters.find(id);if(!monster) continue;
        pet.removeAt=actor.tick+uint64_t(monster->rule.deathTicks);pet.release=0;retire.push_back(id);--count;
    }
    const auto debit=ports_.transactions.release(actor,p->characterRevision,skill.manaCost,{},skill.charge);if(!debit) return debit;
    ports_.monsters.commitHydra(std::move(*actors.value));state_.companions.swap(next.companions);ports_.random=random;
    for(const auto id:retire) ports_.monsters.retire(id,actor.tick);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    bool blocked=synchronizeHirelings(tick)==StepStatus::Blocked;
    std::vector<EntityId> stale;
    for(const auto &[id,source]:pending_) {
        const auto *p=ports_.players.find(source.actor.player);
        if(!p || !p->entered || p->persistent.player.hp<=0 || p->area!=source.actor.area || p->actor!=source.actor.actor || p->persistent.player.level!=source.ownerLevel || p->persistent.player.skillRanks!=source.hardRanks || p->inventoryRevision!=source.inventoryRevision) stale.push_back(id);
    }
    for(auto id:stale) cancel(id);
    for(auto it=state_.companions.begin();it!=state_.companions.end();) {
        auto &pet=it->second;const auto *p=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
        if(!body) {it=state_.companions.erase(it);continue;}
        if(pet.kind==Kind::Hireling) {
            if(!p || !p->entered) {ports_.monsters.remove(pet.actor);it=state_.companions.erase(it);continue;}
            if(hirelingStep(pet,tick)==StepStatus::Blocked) blocked=true;
            ++it;continue;
        }
        if(!pet.removeAt && (body->life<=0 || (body->amazonPet && body->amazonPet->decoy && p && p->area!=body->area) || !p || !p->entered || tick.tick>pet.expires)) {
            pet.removeAt=tick.tick+uint64_t(body->rule.deathTicks);pet.release=0;ports_.monsters.retire(pet.actor,tick.tick);
        }
        if(pet.removeAt) {
            if(tick.tick>=pet.removeAt) {ports_.monsters.remove(pet.actor);it=state_.companions.erase(it);} else ++it;continue;
        }
        if(body->amazonPet && p && p->persistent.player.hp>0) {if(amazonStep(pet,tick)==StepStatus::Blocked) blocked=true;++it;continue;}
        if(!p || p->area!=body->area || p->persistent.player.hp<=0) {++it;continue;}
        if(body->amazonPet) {++it;continue;}
        const auto &area=ports_.areas.at(body->area);
        ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick.tick};
        if(pet.release && tick.tick>=pet.release) {
            const auto *target=ports_.monsters.find(pet.target);
            if(target && target->life>0 && !target->owner && target->area==body->area) {
                auto skill=skills::evaluate(*p,*pet.sourceSkill,pet.rank);
                const auto result=ports_.missiles.spawn({actor,skill,{},target->position,true,pet.actor,1,body->position});
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            }
            pet.release=0;
        }
        if(tick.tick<pet.nextDecision || tick.tick<body->busyUntil) {++it;continue;}
        EntityId target;int nearest=25;
        for(const auto &[id,t]:ports_.monsters.read().actors) {
            if(t.owner || t.life<=0 || t.area!=body->area || !area.definition.activation.nearby(p->position,t.position)) continue;
            const int distance=missileDistance(body->position,t.position);
            if(distance<nearest && area.definition.collision.missileSegment(body->position,t.position,{4,1})) {nearest=distance;target=id;}
        }
        auto random=pet.random;
        if(target && limitedRandom(random,100)<60) {
            if(!ports_.events.hasCapacity(1)) {blocked=true;++it;continue;}
            const auto *enemy=ports_.monsters.find(target);
            const auto published=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},body->area},
                {AttackFact{pet.actor,target,1,1,body->area,body->position,enemy->position,tick.tick+1,uint16_t(pet.attackSkill),uint8_t(pet.rank)}}});
            if(!published) {blocked=true;++it;continue;}
            pet.target=target;pet.release=tick.tick+uint64_t(body->rule.impactTick);
            pet.nextDecision=tick.tick+uint64_t(body->rule.attackTicks);ports_.monsters.beginAttack(pet.actor,pet.nextDecision);
        } else pet.nextDecision=tick.tick+10;
        pet.random=random;++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
