#include "system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/npc/system.hpp"
#include "hireling_equipment.hpp"
#include "server/systems/inventory/planning.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::companions {
namespace {
bool sameHireling(const HirelingRecord &current, const HirelingRecord &prepared) {
    return current.sourceRow == prepared.sourceRow && current.level == prepared.level &&
        current.classId == prepared.classId && current.seed == prepared.seed && current.nameKey == prepared.nameKey;
}
Vec nativePosition(Vec point) {return {std::floor(point.x+.5f),std::floor(point.y+.5f)};}
int coordinateDistance(Vec first,Vec second) {
    const int x=std::abs(int(first.x)-int(second.x)),y=std::abs(int(first.y)-int(second.y));
    return std::max(x,y)+std::min(x,y)/2;
}
int ownerDistance(Vec body,Vec owner,int size) {
    // AiUtil::AIUTIL_GetDistanceToCoordinates_FullUnitSize differs from 10399:
    // subtract the hireling's full SizeX on each axis, then take absolute values.
    const int x=std::abs(std::abs(int(body.x)-int(owner.x))-size);
    const int y=std::abs(std::abs(int(body.y)-int(owner.y))-size);
    return std::max(x,y)+std::min(x,y)/2;
}
bool walkingConnection(const AreaStore &areas,RegionId from,RegionId to) {
    if(from==to) return true;
    const auto &source=areas.at(from),&destination=areas.at(to);
    if(source.definition.act!=destination.definition.act) return false;
    Vec origin;const auto view=source.definition.collision.linkedNavigation(origin);
    return std::any_of(view.navigationSources.begin(),view.navigationSources.end(),
        [&](const auto &entry){return entry.grid==&destination.definition.collision;});
}
void followOwnerTrail(const Ports &ports,Companion &pet,const PlayerState &owner,
                      const monsters::Actor &body,int distance) {
    const auto &area=ports.areas.at(body.area);
    const auto bodyWorld=nativePosition(body.position)+area.definition.origin;
    const auto finalWorld=nativePosition(owner.routeDestination)+ports.areas.at(owner.area).definition.origin;
    const auto move=[&](Vec world,uint8_t type,uint8_t bound,int velocity,bool run) {
        monsters::MoveRequest request{pet.actor,{body.area,area.generation,world-area.definition.origin},{},0,velocity,run};
        request.pathSteps=1;request.pathType=type;request.pathDistance=bound;
        return bool(ports.monsters.requestMove(request));
    };
    // PETAI_PetMove motion 1 follows the owner's twenty sampled positions,
    // newest first. Walking outside town uses the final target instead.
    if(owner.moving && !owner.runningNow && !ports.areas.at(owner.area).definition.town) {
        move(finalWorld,13,100,75,false);return;
    }
    for(auto it=owner.movementHistory.rbegin();it!=owner.movementHistory.rend();++it) {
        if(coordinateDistance(bodyWorld,*it)<=5) continue;
        if(move(*it,13,100,135,false) || move(*it,15,100,135,true) || move(*it,7,100,135,true)) return;
    }
    // This is the final native PETAI motion-1 WanderToTarget branch, not an
    // extra search/teleport policy invented for failed bridge paths.
    const auto ownerWorld=nativePosition(owner.position)+ports.areas.at(owner.area).definition.origin;
    const unsigned radius=unsigned(std::max(distance/4,4));
    int x=0,y=0;
    if(rollRandom(pet.random)&1) {x=int(radius);y=int(limitedRandom(pet.random,radius));}
    else {x=int(limitedRandom(pet.random,radius));y=int(radius);}
    if(rollRandom(pet.random)&1) x=-x;
    if(rollRandom(pet.random)&1) y=-y;
    move(ownerWorld+Vec{float(x),float(y)},13,100,90,false);
}
}
std::optional<HirelingExperienceAward> System::experience(PlayerId owner,const monsters::Actor &victim) const {
    const auto *player=ports_.players.find(owner);const auto prepared=hirelingRules_.find(owner);
    if(!player || prepared==hirelingRules_.end() || !prepared->second.deferred.empty()) return {};
    const auto &record=player->persistent.player.hireling;
    if(!sameHireling(record,prepared->second.source.record) || record.level<1 || size_t(record.level)>=victim.rule.experience.size() ||
        !hirelingCanGainExperience(record.hp>0,record.level,player->persistent.player.level)) return {};
    const monsters::Actor *body=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==owner && pet.kind==Kind::Hireling) {body=ports_.monsters.find(id);break;}
    if(!body || body->life<=0 || body->area!=victim.area) return {};
    const auto plan=planHirelingExperience(victim.rule.experience[size_t(record.level)],
        {record.experience,prepared->second.baseExperience,prepared->second.nextExperience,record.level,player->persistent.player.level,
            body->petStats.attributes.combat.experiencePercent,true,victim.hirelingKiller==body->id});
    if(!plan.amount || plan.experience>UINT32_MAX) return {};
    HirelingExperienceAward award{record,record,body->id};award.after.level=plan.level;award.after.experience=plan.experience;return award;
}
DomainResult<> System::awardExperience(const ActorContext &actor,const HirelingExperienceAward &award) {
    const auto *player=ports_.players.find(actor.player);const auto *body=ports_.monsters.find(award.actor);
    if(!player || !body || body->life<=0 || body->owner!=actor.player || !sameHireling(player->persistent.player.hireling,award.before) ||
        player->persistent.player.hireling.experience!=award.before.experience) return {DomainStatus::Stale,{}};
    auto record=player->persistent.player;record.hireling.level=award.after.level;record.hireling.experience=award.after.experience;
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
    if(award.after.level>award.before.level) edit.publicFacts.emplace_back(SoundFact{award.actor,1,body->area,0x5B});
    auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
}
std::vector<HirelingPreparation> System::pendingHirelings(uint64_t tick) const {
    std::vector<HirelingPreparation> result;
    for(const auto &[id,p]:ports_.players.all()) {
        const auto &record=p.persistent.player.hireling;
        if(!p.entered || record.sourceRow<0) continue;
        int skillBonus=0;
        for(const auto &[key,pet]:state_.companions) if(pet.owner==id && pet.kind==Kind::Hireling) {
            skillBonus=ports_.effects.unitModifiers(key,tick).combat.allSkills;break;
        }
        const auto prior=hirelingRules_.find(id);
        if(prior!=hirelingRules_.end() && sameHireling(record,prior->second.source.record) && prior->second.source.actor.actor==p.actor && prior->second.source.difficulty==p.persistent.difficulty && prior->second.source.inventoryRevision==p.inventoryRevision && prior->second.source.temporarySkillBonus==skillBonus) continue;
        result.push_back({{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,0},record,p.persistent.difficulty,p.inventoryRevision,p.persistent,p.rules.items,p.rules.equipment,skillBonus});
    }
    return result;
}
DomainResult<> System::install(PreparedHireling prepared) {
    const auto *p=ports_.players.find(prepared.source.actor.player);
    if(!p || !p->entered || p->actor!=prepared.source.actor.actor || !sameHireling(p->persistent.player.hireling,prepared.source.record) || p->persistent.difficulty!=prepared.source.difficulty || p->inventoryRevision!=prepared.source.inventoryRevision) return {DomainStatus::Stale,{}};
    hirelingRules_.insert_or_assign(p->player,std::move(prepared));return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::synchronizeHirelings(TickContext tick) {
    const auto expiredList=[&](const HirelingListPreparation &source) {
        const auto access=ports_.npc.service(source.actor,source.npc);
        return !access || access->conversation->revision!=source.conversation;
    };
    std::erase_if(pendingLists_,[&](const auto &entry){return expiredList(entry.second);});
    std::erase_if(hirelingLists_,[&](const auto &entry){return expiredList(entry.second.source);});
    std::erase_if(hirelingRules_,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    bool blocked=false;
    for(const auto &[id,prepared]:hirelingRules_) {
        const auto *p=ports_.players.find(id);if(!p || !p->entered || p->persistent.player.hp<=0 || p->persistent.player.hireling.hp<=0 || !prepared.deferred.empty()) continue;
        bool present=false;for(const auto &[key,pet]:state_.companions) {(void)key;if(pet.owner==id && pet.kind==Kind::Hireling) {present=true;break;}}
        if(present) continue;
        const auto &area=ports_.areas.at(p->area);ActorContext actor{id,p->actor,p->area,area.generation,0,tick.tick};
        for(int radius=1;radius<=4 && !present;++radius) for(int x=-radius;x<=radius && !present;++x) for(int y=-radius;y<=radius && !present;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(!area.definition.collision.walkable(at,prepared.rule.collision)) continue;
            bool occupied=false;
            for(const auto &[key,m]:ports_.monsters.read().actors) {(void)key;if(m.life>0 && m.area==p->area && (m.position-at).length()<float((m.rule.size+prepared.rule.size)/2)) {occupied=true;break;}}
            if(occupied || (p->position-at).length()<float((2+prepared.rule.size)/2)) continue;
            if(!p->rules.items || !p->rules.equipment) continue;
            auto spawnRule=prepared.rule;
            spawnRule.minimumLife=spawnRule.maximumLife=calculateHirelingEquipment(p->persistent,*p->rules.items,*p->rules.equipment,prepared).life;
            const auto maximum=int64_t(spawnRule.minimumLife)*256;
            // Native save records alive/dead, not the old room's current life.
            auto bodies=ports_.monsters.prepareHireling(actor,spawnRule,prepared.code,at,maximum,initialRandom(p->persistent.player.hireling.seed));
            if(!bodies) {blocked|=bodies.status==DomainStatus::Capacity;continue;}
            auto next=state_.companions;const auto key=bodies.value->begin()->first;
            Companion pet{key,id,Kind::Hireling,{}};pet.expires=UINT64_MAX;pet.nextDecision=tick.tick+uint64_t(prepared.think);pet.random=initialRandom(p->persistent.player.hireling.seed);
            pet.ownerTeleport=p->teleportRevision;
            next.emplace(key,std::move(pet));ports_.monsters.commitHydra(std::move(*bodies.value));state_.companions.swap(next);present=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::hirelingStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *p=ports_.players.find(pet.owner);
    if(!body || !p) return StepStatus::Complete;
    const auto rule=hirelingRules_.find(pet.owner);if(rule==hirelingRules_.end() || !rule->second.deferred.empty()) return StepStatus::Complete;
    const auto &prepared=rule->second;const auto &area=ports_.areas.at(p->area);
    ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick.tick};
    if(!sameHireling(p->persistent.player.hireling,prepared.source.record)) return StepStatus::Complete;
    const auto activeStates=ports_.effects.unitStates(body->id,tick.tick);unsigned passiveMask=0;
    for(size_t i=0;i<prepared.passives.size();++i) if(activeStates.contains(prepared.passives[i].first.suppressedByState)) passiveMask|=1u<<i;
    if(body->hirelingInventoryRevision!=p->inventoryRevision || body->rule.level!=p->persistent.player.hireling.level || !body->equipment || body->hirelingPassiveMask!=passiveMask || pet.projectedSkillBonus!=prepared.source.temporarySkillBonus) {
        if(!p->rules.items || !p->rules.equipment) return StepStatus::Blocked;
        const auto stats=calculateHirelingEquipment(p->persistent,*p->rules.items,*p->rules.equipment,prepared,{},activeStates);
        auto updated=prepared.rule;
        updated.minimumLife=updated.maximumLife=stats.life;updated.defense=stats.equipment.defense;
        updated.attackRating=stats.equipment.weapons[0].attackRating;
        updated.minimumDamage=stats.equipment.weapons[0].minimum/256;updated.maximumDamage=stats.equipment.weapons[0].maximum/256;
        updated.blockChance=stats.equipment.blockChance;updated.blockWithoutShield=bool(stats.equipment.shield);
        const auto &mods=stats.modifiers;const auto &combat=mods.combat;
        updated.resistances={combat.physicalResist,combat.magicResist,
            std::max(-100,prepared.rule.resistances[2]+mods.fireResist),
            std::max(-100,prepared.rule.resistances[3]+mods.lightningResist),
            std::max(-100,prepared.rule.resistances[4]+mods.coldResist),
            std::max(-100,prepared.rule.resistances[5]+mods.poisonResist)};
        auto equipment=std::make_shared<PersistentCharacter>(p->persistent);
        std::erase_if(equipment->inventory.items,[&](const auto &entry){const auto *at=std::get_if<ContainerLocation>(&entry.second.location);return !at || at->container!=p->persistent.containers.hirelingEquipment;});
        ports_.monsters.updateHireling(pet.actor,std::move(updated),stats.equipment.weapons[0],combat,stats.actor.strength,stats.actor.dexterity,
            std::max(0,mods.vitality),std::move(equipment),p->inventoryRevision,p->characterRevision);
        pet.projectedSkillBonus=prepared.source.temporarySkillBonus;
        const int frw=std::max(0,stats.modifiers.fasterMoveVelocity);
        // MONSTERAI_SendMercStats sends base stats; owner item/state packets
        // provide equipment contributions separately, as on the original server.
        ports_.monsters.hirelingProjection(pet.actor,prepared.nextExperience,passiveMask,stats.modifiers.velocityPercent+150*frw/(150+frw),
            {{"maxhp",int64_t(prepared.rule.minimumLife)*256},{"strength",prepared.strength},{"dexterity",prepared.dexterity},
             {"armorclass",prepared.rule.defense},{"mindamage",prepared.rule.minimumDamage},{"maxdamage",prepared.rule.maximumDamage},
             {"fireresist",prepared.rule.resistances[2]},{"lightresist",prepared.rule.resistances[3]},
             {"coldresist",prepared.rule.resistances[4]},{"poisonresist",prepared.rule.resistances[5]}});
    }
    const float life=float(body->life)/256.f;
    if(p->persistent.player.hireling.hp!=life) {
        transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};edit.player.hireling.hp=life;
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan || !ports_.transactions.commit(std::move(*plan.value))) return StepStatus::Blocked;
    }
    if(body->life<=0 || p->persistent.player.hp<=0) {ports_.monsters.stop(pet.actor);return StepStatus::Complete;}
    const auto &bodyArea=ports_.areas.at(body->area);
    const int distanceToOwner=ownerDistance(nativePosition(body->position)+bodyArea.definition.origin,
        nativePosition(p->position)+area.definition.origin,body->rule.size);
    const bool walking=!body->route.empty() || body->moving;
    if(!walkingConnection(ports_.areas,body->area,p->area) || (!walking && distanceToOwner>prepared.warp) || pet.ownerTeleport!=p->teleportRevision) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(ports_.monsters.warpPet(pet.actor,actor,at)) {pet.ownerTeleport=p->teleportRevision;pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(tick.tick<pet.nextDecision || tick.tick<body->busyUntil || body->frozenUntil>tick.tick) return StepStatus::Complete;
    // Fn061 returns while WALK/RUN is active instead of replacing a partially
    // executed detour every AI interval.
    if(walking) return StepStatus::Complete;
    if(body->area!=p->area || distanceToOwner>prepared.follow) {
        followOwnerTrail(ports_,pet,*p,*body,distanceToOwner);
        pet.nextDecision=tick.tick+uint64_t(prepared.think);return StepStatus::Complete;
    }
    if(distanceToOwner>16 && p->moving) {
        // Near-owner motion remains a unit approach; publish its true native
        // arrival bound as PATH_SetStepNum instead of sending zero to clients.
        monsters::MoveRequest request{pet.actor,{body->area,area.generation,p->position},p->actor,16,p->runningNow?135:75,false};
        request.pathSteps=17;request.pathDistance=40;
        ports_.monsters.requestMove(request);pet.nextDecision=tick.tick+uint64_t(prepared.think);return StepStatus::Complete;
    }
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId target;int nearest=prepared.vision;
    if(!area.definition.town) for(const auto id:participants.enemies(body->id,body->area)) {
        const auto enemy=participants.find(id);
        const int distance=missileDistance(body->position,enemy.position());
        if(distance<nearest && area.definition.collision.missileSegment(body->position,enemy.position(),{4,1})) {nearest=distance;target=id;}
    }
    auto random=pet.random;
    if(target) {
        const bool melee=prepared.act==2 || prepared.act==5;
        const int chance=melee?98:std::min(pet.attackBias+40+2*body->rule.level,95);
        const bool attackNow=limitedRandom(random,100)<unsigned(chance);
        if(!melee && nearest<4 && limitedRandom(random,100)<50) {
            const auto retreat=[&](Vec center)->bool {
                constexpr Vec offsets[]{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
                std::optional<Vec> best;float distance=-1;
                const auto enemy=participants.find(target).position();
                for(const auto offset:offsets) {
                    const Vec at{std::floor(center.x+offset.x*4)+.5f,std::floor(center.y+offset.y*4)+.5f};
                    if(!area.definition.collision.walkable(at,body->rule.collision)) continue;
                    const auto d=(at-enemy).length();if(d>distance) {distance=d;best=at;}
                }
                return best && bool(ports_.monsters.requestMove({pet.actor,{body->area,area.generation,*best},{},0,75,false}));
            };
            if((distanceToOwner>4 && retreat(p->position)) || retreat(body->position)) {pet.random=random;pet.nextDecision=tick.tick+uint64_t(prepared.think);return StepStatus::Complete;}
        }
        if(!attackNow) {pet.attackBias+=10;pet.random=random;pet.nextDecision=tick.tick+10;return StepStatus::Complete;}
        std::vector<const HirelingAction *> eligible;
        const auto states=ports_.effects.unitStates(pet.actor,tick.tick);
        for(const auto &action:prepared.actions) {
            if(action.aiType==1 && states.contains(action.state)) continue;
            if(action.maximumRange && nearest>action.maximumRange) continue;
            if(action.aura && ports_.effects.hirelingAuraActive(pet.actor,action.skill,action.rank)) continue;
            eligible.push_back(&action);
        }
        unsigned total=unsigned(prepared.defaultChance);for(const auto *action:eligible) total+=unsigned(action->chance);
        const unsigned roll=limitedRandom(random,total+1);unsigned end=unsigned(prepared.defaultChance);
        const HirelingAction *chosen=nullptr;
        if(roll>=end) for(const auto *action:eligible) {end+=unsigned(action->chance);if(roll<=end) {chosen=action;break;}}
        DomainResult<> result;
        if(chosen && chosen->aura) result=ports_.effects.hirelingAura(pet.actor,*chosen->aura,tick.tick);
        else result=ports_.skills.requestCast({pet.actor,uint16_t(chosen?chosen->skill:prepared.rule.skillIds[0]),UnitTarget{target,0,uint8_t(participants.find(target).player?0:1)},tick.tick,uint8_t(chosen?chosen->mode:4)});
        if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
        pet.attackBias=0;
        if(result.status==DomainStatus::Unavailable && (!chosen || !chosen->aura)) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,participants.find(target).position()},target,body->rule.meleeRange,75,false});
        pet.nextDecision=tick.tick+uint64_t(chosen && chosen->aura?10:prepared.think);
    } else {
        if(missileDistance(body->position,p->position)>4) {
            monsters::MoveRequest request{pet.actor,{body->area,area.generation,p->position},p->actor,4,75,false};
            request.pathSteps=5;request.pathDistance=40;ports_.monsters.requestMove(request);
        }
        pet.nextDecision=tick.tick+uint64_t(prepared.think);
    }
    pet.random=random;return StepStatus::Complete;
}
}
