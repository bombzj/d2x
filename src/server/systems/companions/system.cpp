#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace d2x::server::companions {
DomainResult<> System::requestAction(Companion &pet,const actions::MonsterCastRequest &request) {
    const auto result=ports_.skills.requestCast(request);
    if(!isDeferred(result.status)) return result;
    pet.pendingAction=request;
    // Retain the decision and let its owner finish recording the selected
    // parameters. The event itself remains blocked until this start accepts.
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::think(EntityId id,TickContext tick) {
    auto found=state_.companions.find(id);
    if(found==state_.companions.end()) return {DomainStatus::InvalidActor,{}};
    auto &pet=found->second;
    if(const auto *body=ports_.monsters.find(id);body && pet.observedAiThinkRevision!=body->aiThinkRevision) {
        pet.observedAiThinkRevision=body->aiThinkRevision;pet.nextDecision=body->aiThinkAt;
    }
    if(pet.pendingAura) {
        const auto accepted=ports_.effects.hirelingAura(id,*pet.pendingAura,tick.tick);
        if(isDeferred(accepted.status)) return accepted;
        pet.pendingAura.reset();
    } else if(pet.pendingAction) {
        const auto accepted=ports_.skills.requestCast(*pet.pendingAction);
        if(isDeferred(accepted.status)) return accepted;
        pet.pendingAction.reset();
    } else if(const auto *body=ports_.monsters.find(id);body && body->alive() && tick.tick<pet.nextDecision && tick.tick<=pet.expires && !pet.removeAt) {
        ports_.monsters.scheduleAi(id,pet.nextDecision);return {DomainStatus::Applied,std::monostate{}};
    } else if(advancePet(id,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    found=state_.companions.find(id);
    if(found==state_.companions.end()) return {DomainStatus::Applied,std::monostate{}};
    if(found->second.pendingAction || found->second.pendingAura) return {DomainStatus::Capacity,{}};
    const auto *body=ports_.monsters.find(id);
    if(body) {
        auto due=std::max(tick.tick+1,found->second.nextDecision);
        if(body->revived() && body->alive()) {
            // A newly accepted owner-follow path belongs to companions until
            // it ends; invoking the family here would overwrite it immediately.
            if(!found->second.nativePetMoving || body->route.empty()) {
                if(ports_.ai.think(id,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
                if(const auto controller=ports_.ai.read().controllers.find(id);controller!=ports_.ai.read().controllers.end())
                    due=std::min(due,std::max(tick.tick+1,controller->second.nextDecision));
            }
        }
        if(body->alive()) due=std::max(due,std::max({body->busyUntil,body->stunnedUntil,body->knockedUntil}));
        if(found->second.removeAt) due=std::min(due,std::max(tick.tick+1,found->second.removeAt));
        else if(found->second.expires!=UINT64_MAX) due=std::min(due,std::max(tick.tick+1,found->second.expires+1));
        ports_.monsters.scheduleAi(id,due);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::hydra(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    const auto *p=ports_.players.find(actor.player);if(!p || !p->rules.skills || !p->rules.skills->hydra || !skill.summon || !skill.summon->native || skill.hydraLimit<=0 || skill.hydraFrames<=0) return {DomainStatus::Unavailable,{}};
    const auto *area=ports_.areas.find(actor.area);
    if(!p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area ||
        area->generation!=actor.areaGeneration || area->definition.town) return {DomainStatus::InvalidActor,{}};
    const auto expectedRandom=ports_.random;
    auto random=expectedRandom;
    const auto &rules=*p->rules.skills->hydra;
    constexpr std::array<Vec,3> offsets{{{-1,-1},{0,0},{1,-1}}};
    std::array<std::optional<Vec>,3> positions;
    std::map<RoomView,RoomRandomLoan> loans;
    const auto collision=ports_.spatial.nativeCollisionSample();
    const auto sample=[&](RoomView room,int32_t column,int32_t row) {
        auto mask=collision(room,column,row);
        for(size_t index=0;index<positions.size();++index) if(positions[index]) {
            const auto point=*positions[index]+area->definition.origin;
            mask|=nativeCollisionFootprint(rules.heads[index].rules.at(size_t(p->persistent.difficulty)).collisionPattern,
                column-int(std::floor(point.x)),row-int(std::floor(point.y)),0x100);
        }
        return mask;
    };
    for(size_t index=0;index<positions.size();++index) {
        const auto center=target+offsets[index],global=center+area->definition.origin;
        const auto room=ports_.areas.roomAtPosition(actor.area,p->position,int(std::floor(global.x)),int(std::floor(global.y)));
        if(!room) continue;
        if(room->area!=actor.area) return {DomainStatus::NotImplemented,{}};
        auto loan=loans.find(*room);
        if(loan==loans.end()) {
            const auto borrowed=ports_.areas.roomRandom(*room);if(!borrowed) return {borrowed.status,{}};
            loan=loans.emplace(*room,*borrowed.value).first;
        }
        monsters::Admission request;request.identity.monster=rules.heads[index].definition.code;request.identity.spawnKey="hydra.search";
        request.area=actor.area;request.position=center;request.rule=rules.heads[index].rules.at(size_t(p->persistent.difficulty));request.tick=actor.tick;
        auto point=ports_.monsters.searchRoomSpawn(request,loan->second,-1,sample);if(!point) return {point.status,{}};
        if(!*point.value) point=ports_.monsters.searchRoomSpawn(request,loan->second,4,sample);
        if(!point) return {point.status,{}};
        positions[index]=*point.value;
    }
    if(std::none_of(positions.begin(),positions.end(),[](const auto &position){return position.has_value();})) {
        for(const auto &[room,loan]:loans) {(void)room;const auto consumed=ports_.transactions.consumeRoomRandom(loan);if(!consumed) return consumed;}
        return {DomainStatus::Unavailable,{}};
    }
    auto actors=ports_.monsters.prepareHydra(actor,rules,*skill.summon,positions,random);if(!actors) return {actors.status,{}};
    auto next=state_;
    for(const auto &[id,monster]:*actors.value) {
        const auto &head=*std::find_if(p->rules.skills->hydra->heads.begin(),p->rules.skills->hydra->heads.end(),[&](const auto &head) {return head.definition.nativeClass==monster.rule.nativeClass;});
        Companion entry{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};
        entry.rank=skill.rank;entry.attackSkill=head.definition.attackSkill;entry.expires=actor.tick+uint64_t(skill.hydraFrames);
        entry.nextDecision=actor.tick+25;next.companions.emplace(id,std::move(entry));
    }
    size_t count=0;for(const auto &[id,pet]:next.companions) {(void)id;if(pet.owner==actor.player && pet.sourceSkill==uint16_t(skill.sourceId) && !pet.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[id,pet]:next.companions) {
        if(count<=size_t(skill.hydraLimit)) break;
        if(pet.owner!=actor.player || pet.sourceSkill!=uint16_t(skill.sourceId) || pet.removeAt) continue;
        const auto *monster=ports_.monsters.find(id);if(!monster) continue;
        pet.removeAt=actor.tick+uint64_t(monster->rule.deathTicks);pet.release=0;retire.push_back(id);--count;
    }
    auto units=std::make_shared<effects::AuraUnitPublication>();const auto allocated=actors.value->size();
    ports_.effects.stageAuraActors(*units,std::move(*actors.value),allocated);
    const auto retired=ports_.monsters.stageRetirements(*units,retire,actor.tick);if(!retired) return retired;
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);if(!plan) return {plan.status,{}};
    plan.value->gameRandom=transactions::UnitRandomEdit{expectedRandom,random};
    for(const auto &[room,loan]:loans) {(void)room;plan.value->roomRandomLoans.push_back(std::make_shared<const RoomRandomLoan>(loan));}
    const auto debit=ports_.transactions.commit(std::move(*plan.value));if(!debit) return debit;
    state_.companions.swap(next.companions);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    const auto hirelings=synchronizeHirelings(tick);
    if(hirelings!=StepStatus::Complete && hirelings!=StepStatus::Blocked) return hirelings;
    bool blocked=hirelings==StepStatus::Blocked;
    blocked|=synchronizeIronGolems(tick)==StepStatus::Blocked;
    std::vector<EntityId> stale;
    for(const auto &[id,source]:pending_) {
        const auto *p=ports_.players.find(source.actor.player);
        const auto *caster=source.summon.assassin?ports_.monsters.find(source.actor.actor):nullptr;
        const bool controlled=caster && caster->assassinPet && caster->owner==source.actor.player && caster->alive() && caster->area==source.actor.area;
        if(!p || !p->entered || p->persistent.player.hp<=0 || p->area!=source.actor.area || (p->actor!=source.actor.actor && !controlled) || p->persistent.player.level!=source.ownerLevel || p->persistent.player.skillRanks!=source.hardRanks || p->inventoryRevision!=source.inventoryRevision) stale.push_back(id);
    }
    for(auto id:stale) cancel(id);
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::advancePet(EntityId only,TickContext tick) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    bool blocked=false;
    for(auto it=state_.companions.find(only);it!=state_.companions.end() && it->first==only;) {
        auto &pet=it->second;const auto *p=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
        if(!body) {it=state_.companions.erase(it);continue;}
        // Revived monsters retain their family AI clock in ai::System.
        pet.nextDecision=ports_.monsters.resumeAi(pet.actor,tick.tick,pet.nextDecision);
        if(pet.kind==Kind::Hireling) {
            if(!p || !p->entered) {ports_.monsters.remove(pet.actor);it=state_.companions.erase(it);continue;}
            if(hirelingStep(pet,tick)==StepStatus::Blocked) blocked=true;
            ++it;continue;
        }
        const bool wall=body->implementation==MonsterKind::BoneWall;
        const bool aiDue=tick.tick>=pet.nextDecision && tick.tick>=body->busyUntil;
        const bool expired=tick.tick>pet.expires && ((!body->hydra() && !wall) || aiDue);
        if(wall && aiDue && !expired) pet.nextDecision=tick.tick+15;
        if(!pet.removeAt && (!body->alive() || (body->amazonPet && body->amazonPet->decoy && p && p->area!=body->area) || !p || !p->entered || expired)) {
            if(body->alive()) {
                const auto retired=ports_.monsters.retire(pet.actor,tick.tick);
                if(!retired) {if(isDeferred(retired.status)) return StepStatus::Blocked;++it;continue;}
            }
            pet.removeAt=tick.tick+uint64_t(body->rule.deathTicks);pet.release=0;
        }
        if(pet.removeAt) {
            if(tick.tick>=pet.removeAt) {
                // BoneWall's native death-end enters DD; it does not remove
                // the unit. Do13 may still resolve this GUID and add a segment.
                if(body->implementation!=MonsterKind::BoneWall) {
                    const auto removed=ports_.monsters.remove(pet.actor);
                    if(!removed) {blocked|=isDeferred(removed.status);pet.nextDecision=tick.tick+1;++it;continue;}
                }
                it=state_.companions.erase(it);
            } else ++it;continue;
        }
        if(body->amazonPet && p && p->persistent.player.hp>0) {if(amazonStep(pet,tick)==StepStatus::Blocked) blocked=true;++it;continue;}
        if(body->assassinPet) {if(assassinStep(pet,tick)==StepStatus::Blocked) blocked=true;++it;continue;}
        if(body->druidPet) {if(refreshDruid(pet,tick)==StepStatus::Blocked || (!pet.removeAt && druidStep(pet,tick)==StepStatus::Blocked)) blocked=true;++it;continue;}
        if(body->necroSummon && p && p->persistent.player.hp>0) {if(refreshNecro(pet,tick)==StepStatus::Blocked) blocked=true;if(necroStep(pet,tick)==StepStatus::Blocked) blocked=true;++it;continue;}
        if(!p || p->area!=body->area || p->persistent.player.hp<=0) {++it;continue;}
        if(body->amazonPet) {++it;continue;}
        const auto &area=ports_.areas.at(body->area);
        if(tick.tick<pet.nextDecision || tick.tick<body->busyUntil) {++it;continue;}
        EntityId target;int nearest=25;
        for(const auto id:participants.enemies(body->id,body->area)) {
            const auto position=participants.find(id).position();
            if(!area.definition.activation.nearby(p->position,position)) continue;
            const int distance=missileDistance(body->position,position);
            if(distance<nearest && area.definition.collision.missileSegment(body->position,position,{4,1})) {nearest=distance;target=id;}
        }
        auto random=body->combatRandom;
        if(target && limitedRandom(random,100)<60) {
            const auto enemy=participants.find(target);
            // Commit the AI roll only once; action release is owned by skills,
            // and its damage samples this head's native source context.
            ports_.monsters.commitCombatRandom(body->id,random);
            const auto result=requestAction(pet,{pet.actor,uint16_t(pet.attackSkill),UnitTarget{target,0,uint8_t(enemy.player?0:1)},tick.tick});
            if(isDeferred(result.status)) {blocked=true;++it;continue;}
            pet.nextDecision=result?std::max(body->busyUntil,tick.tick+1):tick.tick+10;
        } else {ports_.monsters.commitCombatRandom(body->id,random);pet.nextDecision=tick.tick+10;}
        ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
