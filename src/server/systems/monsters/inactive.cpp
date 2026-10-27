#include "system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/area_store.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "server/systems/attributes/pd2/map_values.hpp"
#include "gameplay/maps/pd2/event_program.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
namespace d2x::server::monsters {
DomainResult<> System::compressRoom(RegionId region,size_t index,uint64_t tick) {
    if(modifierPending_ || restoring_) return {DomainStatus::Conflict,{}};
    const auto &area=ports_.areas.at(region).definition;
    const auto &bounds=area.activation.rooms().at(index);
    const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
    const auto order=ports_.areas.roomUnitOrder(room);
    for(const auto id:order) {
        const auto *unit=find(id);if(!unit) continue;
        if(unit->owner || !unit->admissionRule || unit->busyUntil>tick || unit->skillMotion || unit->poison || unit->wound ||
            (!unit->alive() && !unit->rewardComplete)) return {DomainStatus::Unavailable,{}};
        if(unit->revision>UINT64_MAX-4) return {DomainStatus::Capacity,{}};
    }
    auto loan=ports_.areas.roomRandom(room);if(!loan) return {loan.status,{}};
    std::vector<std::pair<EntityId,bool>> decisions;
    effects::AuraUnitPublication removal;
    for(const auto id:order) {
        const auto found=state_.actors.find(id);if(found==state_.actors.end()) continue;
        const auto &unit=found->second;
        const bool dead=!unit.alive();
        bool compress=area.saveMonsters || unit.restoredFromInactive;
        if(dead && tick>=unit.busyUntil && limitedRandom(loan.value->next,3)!=0) compress=false;
        if(dead) {
            if(unit.alignment()==UnitAlignment::Good || unit.identity.rank==MonsterRank::Unique || unit.identity.rank==MonsterRank::Champion ||
                unit.identity.rank==MonsterRank::SuperUnique || unit.identity.rank==MonsterRank::Minion) compress=false;
            if(unit.rule.states) for(const auto state:ports_.effects.unitStates(id,tick))
                if(unit.rule.states->at(state).corpseUnselectable) compress=false;
        }
        if(unit.rule.restore==0) compress=false;
        else if(unit.rule.restore==2) compress=true;
        if(unit.nativeRevive) compress=false;
        decisions.emplace_back(id,compress);
        if(compress) {
            const auto prepared=ports_.effects.prepareUnitCurseRemoval(removal,id,tick);
            if(!prepared) return prepared;
        }
    }
    decltype(state_.inactive) staged;
    auto &saved=staged[{region,index}];
    if(const auto previous=state_.inactive.find({region,index});previous!=state_.inactive.end()) saved=previous->second;
    for(const auto &[id,compress]:decisions) {
        const auto found=state_.actors.find(id);
        const auto &unit=found->second;
        const bool dead=!unit.alive();
        if(compress) {
            InactiveMonster node;
            node.id=id;node.identity=unit.identity;node.implementation=unit.implementation;
            node.area=region;node.position=unit.position;node.home=unit.home;node.questNpc=unit.questNpc;node.admissionRule=unit.admissionRule;
            if(node.admissionRule->enchantment) {
                const auto base=area.nativeMonsterClasses.find(node.admissionRule->nativeClass);
                if(base==area.nativeMonsterClasses.end() || base->second.rule.enchantment)
                    return {DomainStatus::NotImplemented,{}};
                node.admissionRule=std::make_shared<const MonsterRule>(base->second.rule);
            }
            node.frame=tick;node.revision=unit.revision;node.life=unit.life;node.maximumLife=unit.maximumLife;
            node.corpse=unit.corpse;node.selfResurrections=unit.selfResurrections;
            // Compression samples the prepared RemoveState result, not the still-live cursed actor.
            auto snapshot=unit;
            const auto changed=removal.life.find(id);
            if(changed!=removal.life.end()) {
                const auto &life=changed->second;
                if(life.changed) {snapshot.nativeStats=life.intrinsic;snapshot.life=life.life.raw();snapshot.maximumLife=life.maximum.raw();++snapshot.revision;}
                if(life.specialTargetNode) snapshot.specialTargetOrder=*life.specialTargetNode?1:0;
                if(life.detachedMinion) snapshot.detachedMinion=*life.detachedMinion;
                if(life.nativeMinions) snapshot.nativeMinions=*life.nativeMinions;
            }
            node.level=snapshot.rule.level;
            node.revision=snapshot.revision;node.life=snapshot.life;node.maximumLife=snapshot.maximumLife;
            if(snapshot.nativeStats && snapshot.rule.stats) {
                const auto experience=snapshot.rule.stats->names.find("experience");
                if(experience!=snapshot.rule.stats->names.end()) {
                    const auto effects=removal.units.find(id);
                    const auto sources=effects==removal.units.end()?ports_.effects.unitStatSources(id,tick):
                        combatEffectStatSources(effects->second.states.entries(),tick,true);
                    attributes::MonsterResourceTrace resources(snapshot,tick);
                    const auto values=resources.values(sources);
                    node.experience=values.get({experience->second,0});
                    if(const auto level=snapshot.rule.stats->names.find("level");level!=snapshot.rule.stats->names.end())
                        node.level=values.get({level->second,0});
                }
            }
            node.alignment=int(snapshot.alignment());node.hostile=unit.hostile;node.dead=dead;
            node.noTreasure=unit.rule.noTreasure;node.noExperience=unit.rule.noExperience;
            node.uncounted=unit.uncounted;
            node.ownsMinions=unit.ownsNativeMinions;node.transferMinions=unit.transferMinionsOnDeath;
            node.minionOwner=snapshot.detachedMinion?std::optional<EntityId>{EntityId{}}:snapshot.nativeMinionOwner;node.minions=snapshot.nativeMinions;
            node.wander=unit.wanderTargetOrder!=0;node.special=snapshot.specialTargetOrder!=0;
            node.modifiers=unit.rule.nativeModifiers;node.nameSeed=unit.rule.enchantment?unit.rule.enchantment->nameSeed:0;
            node.skillPositions=unit.skillPositions;node.componentParameter=unit.components[10];node.nestSpawned=unit.nestSpawned;
            node.baalThroneWave=unit.rule.ai.kind==MonsterAiKind::BaalThrone?unit.baalThroneWave:0;
            if((unit.identity.monster=="diablo" || townNpcAi(unit.rule.ai.kind)) && unit.aiHomeCommand &&
                (unit.aiHomeCommand->x!=0 || unit.aiHomeCommand->y!=0)) node.aiHomeCommand=unit.aiHomeCommand;
            node.impMountedAi=unit.impMountedAi;
            node.pd2EventState=ports_.effects.unitStates(id,tick).contains(213);
            node.whippedAi=unit.whippedAi;
            saved.insert(saved.begin(),std::move(node));
        }
    }
    if(!ports_.effects.validateAuraUnits(removal) || !ports_.areas.validateRoomRandom(*loan.value)) return {DomainStatus::Stale,{}};
    if(!removal.facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(removal.facts)});
        if(!sent) return {sent.status,{}};
    }
    ports_.effects.publishAuraUnits(removal);
    ports_.areas.publishRoomRandom(*loan.value);
    if(const auto previous=state_.inactive.find({region,index});previous!=state_.inactive.end()) previous->second.swap(saved);
    else state_.inactive.insert(staged.extract(staged.begin()));
    for(const auto &[id,compress]:decisions) {
        (void)compress;
        ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
        ports_.areas.removeRoomUnit(id);state_.actors.erase(id);
    }
    if(state_.inactive.at({region,index}).empty()) state_.inactive.erase({region,index});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::finishRestoration(const InactiveMonster &source,uint64_t tick) {
    const auto *unit=find(source.id);if(!unit) return {DomainStatus::Stale,{}};
    if(unit->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto candidate=*unit;
    attributes::MonsterResourceTrace resources(candidate,tick);
    const auto sources=ports_.effects.unitStatSources(source.id,tick);
    resources.sampleSources(sources);
    resources.writeBaseStat("maxhp",nativeNarrow(source.maximumLife),sources);
    resources.writeBaseStat("alignment",source.alignment,sources);
    resources.writeBaseStat("experience",source.experience,sources);
    resources.writeBaseStat("level",source.level,sources);
    resources.seedLife(Fixed8::fromRaw(nativeNarrow(source.dead?0:uint32_t(tick-source.frame)<7500?source.life:source.maximumLife)));
    auto life=resources.take();candidate.nativeStats=std::move(life.intrinsic);
    candidate.life=life.life.raw();candidate.maximumLife=life.maximum.raw();candidate.modeAlive=!source.dead && candidate.life==0;
    candidate.nativeMinionOwner=source.minionOwner;candidate.nativeMinions=source.minions;
    candidate.ownsNativeMinions=source.ownsMinions;candidate.transferMinionsOnDeath=source.transferMinions;
    candidate.rule.noTreasure=source.noTreasure;candidate.rule.noExperience=source.noExperience;
    if(source.pd2EventState) {
        const auto &map=ports_.areas.at(candidate.area).definition.pd2Map;
        if(map && (map->controller==14 || map->controller==33)) {
            if(const auto *effects=ports_.effects.unitEffects(source.id)) candidate.initialEffects=std::make_shared<CombatEffectSet>(*effects);
            const auto bosses=pd2::wandererPool(true);
            const int velocity=map->controller==33?5:std::find(bosses.begin(),bosses.end(),candidate.rule.nativeClass)!=bosses.end()?50:100;
            attributes::preparePd2EventMonster(candidate,velocity,tick);
        }
    }
    candidate.restoredFromInactive=true;candidate.restorationPending=false;
    const auto translation=ports_.areas.at(source.area).definition.origin-ports_.areas.at(candidate.area).definition.origin;
    candidate.home=source.home+translation;candidate.questNpc=source.questNpc;
    candidate.aiHomeCommand=source.aiHomeCommand;
    candidate.skillPositions=source.skillPositions;
    for(auto &point:candidate.skillPositions) point=point+translation;
    if(candidate.questNpc) candidate.movementGoal=candidate.position;
    candidate.corpse=source.corpse;candidate.selfResurrections=source.selfResurrections;
    // The compressed native record retains special AI state16, but does not
    // serialize UNITS_StoreOwner or its attached/sourceunit stat lists.
    // Restore runs state16's initializer once, then its first think sees the
    // missing owner and returns to Fn122 with NU1.
    if(!source.dead && source.impMountedAi) initializeImpMount(candidate);
    candidate.whippedAi=!source.dead && source.whippedAi;
    candidate.uncounted=source.uncounted;
    candidate.identity=source.identity;
    if(source.dead) {candidate.deathTick=tick;candidate.rewardComplete=true;}
    if(source.admissionRule->sharedRandomComponents) {
        candidate.components[10]=uint8_t(source.componentParameter);
        if(source.admissionRule->sharedRandomComponents&(1u<<11)) candidate.components[11]=uint8_t(source.componentParameter);
    }
    if(candidate.rule.ai.kind==MonsterAiKind::SandMaggot) candidate.nestSpawned=source.nestSpawned;
    if(candidate.rule.ai.kind==MonsterAiKind::BaalThrone) candidate.baalThroneWave=source.baalThroneWave;
    if(source.wander) {
        if(state_.nextWanderTargetOrder==UINT64_MAX) return {DomainStatus::Capacity,{}};
        candidate.wanderTargetOrder=state_.nextWanderTargetOrder;
    }
    if(source.special) {
        if(state_.nextSpecialTargetOrder==UINT64_MAX) return {DomainStatus::Capacity,{}};
        candidate.specialTargetOrder=state_.nextSpecialTargetOrder;
    }
    ++candidate.revision;
    auto publication=prepareActorPublication({{source.id,std::move(candidate)}},0);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishActors(publication);
    if(source.wander) ++state_.nextWanderTargetOrder;
    if(source.special) ++state_.nextSpecialTargetOrder;
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::advanceRestoration(uint64_t tick) {
    if(!restoring_) return StepStatus::Complete;
    if(modifierPending_) return StepStatus::Blocked;
    const auto found=state_.inactive.find({restoring_->area,restoring_->room});
    if(found==state_.inactive.end() || found->second.empty() || found->second.front().id!=restoring_->actor)
        throw std::logic_error("Inactive restoration lost its source node");
    const auto &source=found->second.front();
    const auto *unit=find(source.id);
    if(!unit || !unit->restorationPending) throw std::logic_error("Inactive restoration lost its pending actor");
    const bool modifiers=source.identity.rank==MonsterRank::Unique || source.identity.rank==MonsterRank::Champion ||
        source.identity.rank==MonsterRank::SuperUnique || source.identity.rank==MonsterRank::Minion;
    if(modifiers && !unit->rule.enchantment) {
        const auto prepared=prepareBirthModifiers(source.id,source.identity.rank,source.modifiers,{},tick,{},false,source.nameSeed);
        return prepared || isDeferred(prepared.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    }
    const auto finished=finishRestoration(source,tick);
    if(!finished) return isDeferred(finished.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    found->second.erase(found->second.begin());
    if(found->second.empty()) state_.inactive.erase(found);
    restoring_.reset();
    return StepStatus::Complete;
}
StepStatus System::restoreRoom(RegionId region,size_t index,uint64_t tick,bool sanctumCleared) {
    for(;;) {
        if(restoring_) {
            const auto status=advanceRestoration(tick);
            if(status!=StepStatus::Complete) return status;
        }
        const auto found=state_.inactive.find({region,index});if(found==state_.inactive.end()) return StepStatus::Complete;
        auto &saved=found->second;
        const auto &area=ports_.areas.at(region).definition;const auto &bounds=area.activation.rooms().at(index);
        const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
        // SUNITINACTIVE_RestoreInactiveUnits finishes each node before the next.
        // A published restoration changes occupancy: resample here, not after an
        // artificial scheduler yield. Real modifier/content/output waits still block.
        const auto collision=ports_.spatial.nativeCollisionSample();
        while(!saved.empty()) {
            auto &source=saved.front();
            if(int(region)==108 && sanctumCleared && !source.dead && source.identity.monster!="diablo") {
                saved.erase(saved.begin());continue;
            }
            if(find(source.id)) throw std::logic_error("Inactive monster is already active outside restoration");
            if(modifierPending_) return StepStatus::Blocked;
            if(state_.actors.size()>=65536 || source.revision>=UINT64_MAX-1) return StepStatus::Blocked;
            auto identity=source.identity;identity.rank=source.admissionRule->boss?MonsterRank::Boss:MonsterRank::Normal;
            Admission request{identity,source.implementation,region,source.position,source.hostile,*source.admissionRule,source.skillPositions,tick,source.dead};
            request.ignoreSpawnCollision=source.admissionRule->flying;
            request.uncounted=source.uncounted;request.resurrected=true;
            auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
            std::optional<RoomRandomLoan> sourceLoan;
            std::optional<Vec> point;
            for(const int attempts:{-1,source.identity.rank==MonsterRank::Minion?4:5}) {
                const auto searched=searchRoomSpawn(request,*loan.value,attempts,collision);
                if(!searched) return isDeferred(searched.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                if(*searched.value) {point=*searched.value;break;}
            }
            if(!point && bounds.width>1 && bounds.height>1) {
                const int left=room.x*5+1,top=room.y*5+1;
                for(int attempt=0;attempt<20 && !point;++attempt) {
                    request.position=Vec{float(left+int(limitedRandom(loan.value->next,uint32_t(bounds.width-1)))),
                        float(top+int(limitedRandom(loan.value->next,uint32_t(bounds.height-1))))}-area.origin;
                    auto candidate=request;candidate.ignoreSpawnCollision=false;
                    const auto probe=searchRoomSpawn(candidate,*loan.value,-1,collision);
                    if(!probe) return isDeferred(probe.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                    if(!*probe.value) continue;
                    const auto birth=searchRoomSpawn(request,*loan.value,-1,collision);
                    if(!birth) return isDeferred(birth.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                    point=*birth.value;
                    break;
                }
            }
            if(!point && source.identity.rank!=MonsterRank::Minion) {
                const auto global=request.position+area.origin;
                const auto free=ports_.areas.nativeRestoreCoordinates(room,int(global.x),int(global.y),1,0x3c01,collision);
                if(free) {
                    if(free->room!=loan.value->room) {
                        auto destination=ports_.areas.roomRandom(free->room);
                        if(!destination) return StepStatus::NotImplemented;
                        sourceLoan=*loan.value;
                        *loan.value=*destination.value;
                    }
                    request.area=free->room.area;
                    request.position=free->position;
                    const auto searched=searchRoomSpawn(request,*loan.value,-1,collision);
                    if(!searched) return isDeferred(searched.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                    point=*searched.value;
                }
            }
            const auto publishSearch=[&] {
                if(sourceLoan) ports_.areas.publishRoomRandom(*sourceLoan);
                ports_.areas.publishRoomRandom(*loan.value);
            };
            if(!point) {publishSearch();saved.erase(saved.begin());continue;}
            request.position=*point;
            auto random=ports_.random;ComponentPaletteState palettes;
            auto prepared=prepareAdmissionActor(request,source.id,random,palettes,ports_.ids.cursor());
            if(!prepared) return isDeferred(prepared.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            auto actor=std::move(*prepared.value);
            const auto allocated=actor.birthEquipmentItems;
            actor.restorationPending=true;actor.questNpc=source.questNpc;
            actor.revision=source.revision+1;
            actor.admissionRevision=actor.revision;
            const std::array restored{source.id};
            auto publication=prepareActorPublication({{source.id,std::move(actor)}},allocated,restored);
            if(!validateActors(publication)) return StepStatus::Blocked;
            if(!ports_.areas.validateRoomRandom(*loan.value) || (sourceLoan && !ports_.areas.validateRoomRandom(*sourceLoan)))
                return StepStatus::Blocked;
            publishSearch();
            publishComponents(palettes);publishActors(publication);ports_.random=random;
            restoring_=Restoration{region,index,source.id};
            break;
        }
        if(!restoring_) {state_.inactive.erase(found);return StepStatus::Complete;}
    }
}
}
