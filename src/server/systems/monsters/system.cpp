#include "system.hpp"
#include "server/systems/attributes/pd2/map_values.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/monsters/pd2/boss_program.hpp"
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
void System::refreshCollision(const Actor &unit) noexcept {
    NativeFootprint body,reservation;
    if(unit.hatching || (!unit.alive() && !unit.rule.deadCollision))
        body=ports_.areas.rectangleFootprint(unit.area,unit.position,3,3,0x8000);
    else if(!unit.burrowed && !unit.airborne && !unit.submerged)
        body=ports_.areas.patternFootprint(unit.area,unit.position,unit.collisionPattern.value_or(unit.rule.collisionPattern),0x100);
    if(unit.pd2NoPathDisabled && unit.alive()) {
        const auto pattern=unit.collisionPattern.value_or(unit.rule.collisionPattern);
        body=pattern==NativeCollisionPattern::BigUnit?
            ports_.areas.rectangleFootprint(unit.area,unit.position,3,3,0x100):
            ports_.areas.patternFootprint(unit.area,unit.position,NativeCollisionPattern::SmallNoPresence,0x100);
    }
    if(unit.pd2DashAbsent) body={};
    if(unit.spawnReservation) reservation=ports_.areas.patternFootprint(unit.area,*unit.spawnReservation,NativeCollisionPattern::SmallUnit,0x100);
    ports_.areas.setUnitCollision(unit.id,body,reservation,unit.alive());
}
DomainResult<std::vector<MonsterRule::BirthEquipment::Choice>> selectBirthEquipment(const MonsterRule &rule,uint64_t &random) {
    size_t first=0;
    while(first<rule.birthEquipment.size() && rule.birthEquipment[first].level>rule.level) ++first;
    std::vector<MonsterRule::BirthEquipment::Choice> result;
    for(size_t index=first;index<rule.birthEquipment.size();++index) {
        const auto &choices=rule.birthEquipment[index].choices;
        if(choices.empty()) return {DomainStatus::InvalidRequest,{}};
        result.push_back(choices.at(limitedRandom(random,uint32_t(choices.size()))));
    }
    return {DomainStatus::Applied,std::move(result)};
}

void System::withdrawQuestGroup(RegionId region,std::string_view name) {
    const auto key="quest."+std::string(name);
    for(auto it=state_.actors.begin();it!=state_.actors.end();) {
        const auto &actor=it->second;
        if(actor.area!=region || actor.owner || actor.identity.origin!=SpawnOrigin::Preset ||
           (actor.identity.spawnKey!=key && actor.identity.ownerSpawnKey!=key)) {++it;continue;}
        stopScriptedMotion(it->first);
        ports_.effects.forgetUnit(it->first);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,it->first});ports_.areas.removeRoomUnit(it->first);
        it=state_.actors.erase(it);
    }
}
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area = ports_.areas.find(request.area);
    if (!area || area->definition.town || !request.hostile || !request.rule || (!request.implementation && request.rule->trap==MonsterRule::Trap::None) || request.identity.spawnKey.empty())
        return {DomainStatus::InvalidRequest, {}};
    const auto &rule = *request.rule;
    if (rule.nativeClass < 0 || rule.nativeClass > UINT16_MAX || rule.minimumLife < 0 || rule.maximumLife < 0 ||
        rule.nativeVelocity < 0 || rule.nativeVelocity > 255 || rule.difficulty < 0 || rule.difficulty > 2 || rule.alignment < 0 || rule.alignment > 2 ||
        rule.level <= 0 || (rule.attacks.empty() && rule.skillActions.empty() &&
        rule.ai.kind!=MonsterAiKind::SuicideMinion && rule.ai.kind!=MonsterAiKind::EvilHole && rule.ai.kind!=MonsterAiKind::Idle)) return {DomainStatus::InvalidRequest, {}};
    const auto finish=[&](EntityId id)->DomainResult<EntityId> {
        const auto *unit=find(id);
        if(unit && unit->rule.enchantment && unit->rule.enchantment->aura && !unit->rightSkill) {
            const auto &aura=*unit->rule.enchantment->aura;
            const auto assigned=ports_.effects.assignUnitAura(id,aura,request.tick,aura.rank);
            if(!assigned) return {assigned.status,{}};
        }
        return {DomainStatus::Applied,id};
    };
    for (const auto &[id, actor] : state_.actors)
        if (actor.area == request.area && actor.identity.spawnKey == request.identity.spawnKey) return finish(id);
    if(!area->definition.collision.walkable(request.position,rule.spawnCollision)) return {DomainStatus::Unavailable,{}};
    if (state_.actors.size() >= 65536 || ports_.ids.cursor() >= UINT32_MAX) return {DomainStatus::Capacity, {}};
    auto random=ports_.random;ComponentPaletteState palettes;
    auto prepared=prepareAdmissionActor(request,EntityId{ports_.ids.cursor()},random,palettes);
    if(!prepared) return {prepared.status,{}};
    auto actor=std::move(*prepared.value);
    const auto id=actor.id;
    const auto allocated=1+actor.birthEquipmentItems;
    std::map<EntityId,Actor> candidate;candidate.emplace(id,std::move(actor));
    auto publication=prepareActorPublication(std::move(candidate),allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return finish(id);
}
DomainResult<Actor> System::prepareAdmissionActor(const Admission &request,EntityId id,uint64_t &random,ComponentPaletteState &palettes,uint64_t itemCursor,const std::set<size_t> *stagedUniques,std::shared_ptr<RoomUnitRegistration> room) {
    if(!request.rule || !request.rule->stats || !request.rule->formulas || request.rule->minimumLife<0 ||
        request.rule->maximumLife<0) return {DomainStatus::InvalidRequest,{}};
    if(const auto *area=ports_.areas.find(request.area);area && area->definition.pd2Map &&
       area->definition.pd2Map->controller>=41 && area->definition.pd2Map->controller<=45 &&
       pd2::scenePauseClass(request.identity.nativeClass) &&
       (area->provisionalPd2Map || ports_.travel.read().pd2SceneOpening))
        return {DomainStatus::PendingContent,{}};
    const auto &rule=*request.rule;
    Actor actor;
    actor.admissionTick=request.tick;
    actor.hostile=request.hostile;
    actor.restoredFromInactive=request.resurrected;
    actor.uncounted=request.uncounted || rule.neverCount;
    actor.questNpc=!request.hostile && rule.ai.kind!=MonsterAiKind::GoodNpcRanged;actor.rewardComplete=!request.hostile;
    const auto entered=unsigned(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[](const auto &v){return v.second.entered;}));
    actor.admittedPlayerCount=rule.alignment?1u:std::clamp(std::max(entered,ports_.settings.virtualPlayers),1u,8u);
    actor.id = id; actor.identity = request.identity; actor.implementation = request.implementation;
    actor.area = request.area; actor.position = actor.home = request.position; actor.skillPositions=request.skillPositions; actor.revision = 1; actor.rule = rule;
    actor.admissionRule=std::make_shared<const MonsterRule>(rule);
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.combatRandom=request.unitRandom?*request.unitRandom:childRandom(random);
    const bool activeAi=actor.nativeAi();
    if(activeAi && !request.deadMode) {
        uint64_t delay=1;
        if(request.birthTicks<=0) {
            const bool clients=std::any_of(ports_.players.all().begin(),ports_.players.all().end(),[&](const auto &entry) {
                const auto &player=entry.second;
                return player.entered && ports_.areas.nearby(player.area,player.position+Vec{.5f,.5f},actor.area,actor.position);
            });
            delay=clients?2:uint64_t(rule.neutralDelayTicks>0?rule.neutralDelayTicks:15);
        }
        actor.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,id},FrameEventType::AiThink,request.tick+delay}));
    }
    if(activeAi) actor.initialModeTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
        {{},{FrameEventUnit::Monster,id},FrameEventType::ModeChange,request.tick+1,true}));
    prepareInitialTimers(actor,request.tick);
    palettes=prepareComponents(actor,&palettes);
    const auto rolledLife=rollMonsterLife(rule.minimumLife,rule.maximumLife,actor.combatRandom);
    // D2MOO MONSTER_GetHpBonus/GetExperienceBonus: +50% per additional
    // entered/virtual player, captured at admission. Non-evil alignment uses one.
    const auto lifeBonus=int32_t(50*(actor.admittedPlayerCount-1));
    const auto admittedLife=std::min(nativeAdd(rolledLife,nativePercentage(rolledLife,lifeBonus,100)),(1<<23)-1);
    actor.life=actor.maximumLife=nativeShiftLeft(admittedLife,8);actor.modeAlive=admittedLife==0;
    if(request.deadMode) {actor.life=0;actor.modeAlive=false;actor.deathTick=request.tick;actor.rewardComplete=true;}
    else if(request.birthTicks>0) {
        actor.riseMode=8;actor.riseUntil=actor.busyUntil=request.tick+uint64_t(request.birthTicks);
        actor.neutralAt=actor.riseUntil;
    }
    playerCountBonus(actor.rule,actor.admittedPlayerCount);
    actor.nativeStats=attributes::prepareMonsterStatSources(actor,ports_.areas.statTime(actor.area));
    const auto equipped=prepareAdmissionEquipment(actor,random,itemCursor?itemCursor:uint64_t(id.value)+1,stagedUniques);
    if(!equipped) return {equipped.status,{}};
    if(rule.enchantment) {
        attributes::MonsterResourceTrace resources(actor,request.tick);
        auto maximum=resources.read().maximum.raw();
        maximum=nativeAdd(maximum,nativePercentage(maximum,rule.enchantment->lifePercent,100));
        resources.writeBaseStat("maxhp",maximum,{});
        if(!request.deadMode) resources.seedLife(Fixed8::fromRaw(maximum));
        if(rule.enchantment->stopRegeneration) resources.writeBaseStat("hpregen",0,{});
        if(rule.enchantment->lifeScalePercent!=100) {
            maximum=resources.read().maximum.raw();
            maximum=nativeAdd(maximum,nativePercentage(maximum,nativeSubtract(rule.enchantment->lifeScalePercent,100),100));
            resources.writeBaseStat("maxhp",maximum,{});
            if(!request.deadMode) resources.seedLife(Fixed8::fromRaw(maximum));
        }
        auto change=resources.take();actor.nativeStats=std::move(change.intrinsic);
        actor.life=change.life.raw();actor.maximumLife=change.maximum.raw();
    }
    if(const auto *area=ports_.areas.find(actor.area);area && area->definition.pd2Map)
        attributes::preparePd2MapMonster(actor,*area->definition.pd2Map,request.tick,request.deadMode,ports_.travel.read().pd2SceneParticipants,area->definition.pd2RuntimeGlobal);
    if(request.pd2SceneLifePercent) attributes::preparePd2SceneMonster(actor,request.pd2SceneLifePercent,request.tick);
    // MONSTER_Reinitialize rebuilds the existing unit in its existing room.
    // Its caller supplies that registration before preparation: creating a
    // second one here throws before the caller can restore the original.
    if(room && !ports_.areas.validateRoomUnit(*room,id)) return {DomainStatus::Stale,{}};
    actor.roomAdmission=room?std::move(room):ports_.areas.prepareRoomUnit(id,1,actor.area,actor.position);
    return {DomainStatus::Applied,std::move(actor)};
}
std::optional<EquipmentPreparation> System::pendingEquipment() {
    std::erase_if(birthEquipment_,[&](const auto &entry) {
        const auto &request=entry.second.first;const auto *area=ports_.areas.find(request.area);
        return !area || area->generation!=request.generation || state_.actors.contains(request.actor) ||
            request.tick<ports_.frameEvents.currentFrame();
    });
    for(const auto &[token,entry]:birthEquipment_) if(!entry.second) return entry.first;
    return {};
}
DomainResult<> System::prepareEquipment(PreparedEquipment prepared) {
    const auto found=birthEquipment_.find(prepared.token);
    if(found==birthEquipment_.end()) return {DomainStatus::Stale,{}};
    if(found->second.second) return {DomainStatus::Conflict,{}};
    const auto &request=found->second.first;
    if(prepared.deferred.empty()) {
        if(!prepared.equipment || !prepared.stats || request.itemCursor>UINT32_MAX || prepared.allocatedItems>UINT32_MAX-request.itemCursor)
            return {DomainStatus::InvalidRequest,{}};
        auto unitRandom=request.unitRandom;
        const auto selection=selectBirthEquipment(request.rule,unitRandom);
        if(!selection || unitRandom!=prepared.unitRandom || prepared.allocatedItems>selection.value->size())
            return {DomainStatus::InvalidRequest,{}};
        const auto &selected=*selection.value;
        auto random=request.gameRandom;
        for(size_t index=0;index<prepared.allocatedItems;++index) {rollNativeItemSeeds(random);}
        if(random!=prepared.gameRandom || prepared.equipment->inventory.items.size()>prepared.allocatedItems)
            return {DomainStatus::InvalidRequest,{}};
        std::set<int> slots;size_t selectedIndex=0;
        for(const auto &[id,item]:prepared.equipment->inventory.items) {
            const auto *location=std::get_if<ContainerLocation>(&item.location);
            if(id!=item.id || id.value<request.itemCursor || id.value-request.itemCursor>=prepared.allocatedItems ||
                !item.socketedItems.empty() || !(item.nativeFlags&0x1000) || !location || location->container!=request.actor ||
                location->cell.x<0 || location->cell.x>=10 || location->cell.y || !slots.insert(location->cell.x).second ||
                item.level!=unsigned(request.rule.level))
                return {DomainStatus::InvalidRequest,{}};
            while(selectedIndex<selected.size() && (selected[selectedIndex].body!=location->cell.x+1 ||
                selected[selectedIndex].code!=item.definition)) ++selectedIndex;
            if(selectedIndex==selected.size()) return {DomainStatus::InvalidRequest,{}};
            ++selectedIndex;
        }
        if(prepared.sources.size()!=prepared.equipment->inventory.items.size() ||
            (prepared.weapon && !prepared.equipment->inventory.items.contains(prepared.weapon->item))) return {DomainStatus::InvalidRequest,{}};
        for(const auto &source:prepared.sources)
            if(!source || source->ownerKind!=StatOwnerKind::Item || !source->unitRoot ||
                !prepared.equipment->inventory.items.contains(source->source.owner)) return {DomainStatus::InvalidRequest,{}};
        for(const auto row:prepared.limitedUniques) {
            if(request.uniques.contains(row) || std::none_of(prepared.equipment->inventory.items.begin(),prepared.equipment->inventory.items.end(),
                [&](const auto &entry){return entry.second.quality==ItemQuality::Unique && entry.second.specialRow>=0 && size_t(entry.second.specialRow)==row;}))
                return {DomainStatus::InvalidRequest,{}};
        }
    }
    found->second.second=std::move(prepared);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::prepareAdmissionEquipment(Actor &actor,uint64_t &random,uint64_t itemCursor,const std::set<size_t> *stagedUniques) {
    if(actor.rule.birthEquipment.empty()) return {DomainStatus::Applied,std::monostate{}};
    const auto &area=ports_.areas.at(actor.area);
    auto uniques=ports_.loot.read().uniques;
    if(stagedUniques) uniques.insert(stagedUniques->begin(),stagedUniques->end());
    std::erase_if(birthEquipment_,[&](const auto &entry) {
        return entry.second.first.actor==actor.id && (entry.second.first.uniques!=uniques ||
            entry.second.first.uniquesExpected!=ports_.loot.read().uniques);
    });
    auto found=std::find_if(birthEquipment_.begin(),birthEquipment_.end(),[&](const auto &entry) {
        const auto &request=entry.second.first;
        return request.actor==actor.id && request.area==actor.area && request.generation==area.generation &&
            request.tick==actor.admissionTick && request.itemCursor==itemCursor && request.rule.nativeClass==actor.rule.nativeClass &&
            request.rule.level==actor.rule.level && request.unitRandom==actor.combatRandom && request.gameRandom==random;
    });
    if(found==birthEquipment_.end()) {
        if(nextEquipmentToken_==UINT64_MAX || birthEquipment_.size()>=65536 || itemCursor>UINT32_MAX) return {DomainStatus::Capacity,{}};
        std::erase_if(birthEquipment_,[&](const auto &entry) {return entry.second.first.actor==actor.id;});
        const auto stat=[&](std::string_view name){return nativeStatValue(actor.nativeStats->values,*actor.rule.stats,name);};
        EquipmentPreparation request;request.token=nextEquipmentToken_++;request.tick=actor.admissionTick;request.generation=area.generation;
        request.actor=actor.id;request.itemCursor=itemCursor;request.area=actor.area;request.rule=actor.rule;
        request.unitRandom=actor.combatRandom;request.gameRandom=random;
        request.uniques=std::move(uniques);request.uniquesExpected=ports_.loot.read().uniques;
        request.strength=stat("strength");request.dexterity=stat("dexterity");request.defense=stat("armorclass");request.rating=stat("tohit");
        const auto token=request.token;
        birthEquipment_.emplace(token,std::pair{std::move(request),std::optional<PreparedEquipment>{}});
        return {DomainStatus::PendingContent,{}};
    }
    if(!found->second.second) return {DomainStatus::PendingContent,{}};
    const auto &prepared=*found->second.second;
    if(!prepared.deferred.empty()) return {DomainStatus::NotImplemented,{}};
    actor.equipment=std::make_shared<PersistentCharacter>(*prepared.equipment);actor.equipmentStats=prepared.stats;
    actor.petWeapon=prepared.weapon;actor.birthEquipmentItems=prepared.allocatedItems;
    actor.birthUniquesExpected=found->second.first.uniquesExpected;actor.birthLimitedUniques=prepared.limitedUniques;
    actor.nativeCharges.clear();actor.nativeChargesTracked=true;
    attributes::MonsterResourceTrace resources(actor,actor.admissionTick);
    resources.observeSourceWrites([&](StatValue write,EntityId source,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
        attributes::dispatchMonsterItemAura(actor,resources,write,source,stats,std::move(root),{});
    });
    for(const auto &source:prepared.sources) resources.postIntrinsicSource(source,{});
    if(!actor.preparedItemAuras.empty()) {
        found->second.second->deferred="Native MonEquip item aura publication is not implemented";
        return {DomainStatus::NotImplemented,{}};
    }
    auto change=resources.take();actor.nativeStats=std::move(change.intrinsic);actor.life=change.life.raw();actor.maximumLife=change.maximum.raw();
    actor.combatRandom=prepared.unitRandom;random=prepared.gameRandom;
    return {DomainStatus::Applied,std::monostate{}};
}
ComponentPaletteState System::prepareComponents(Actor &actor,const ComponentPaletteState *staged) const {
    // MONSTERREGION::sub_6FC67FA0 applies to NPCs as well as hostile units.
    // Lazy classes use the actual area's palette and thirteen-class bound.
    const auto &rule=actor.rule;
    ComponentPaletteState candidate=staged?*staged:ComponentPaletteState{};
    if(rule.skipRegionComponents) {
        actor.components=chooseMonsterComponents(rule.componentCounts,{},actor.combatRandom);
        actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
        return candidate;
    }
    const auto current=state_.componentPalettes.find(actor.area);
    auto &palettes=candidate.try_emplace(actor.area,current==state_.componentPalettes.end()?
        ports_.areas.at(actor.area).definition.componentPalettes:current->second).first->second;
    actor.components=chooseRegionMonsterComponents(rule.componentCounts,rule.nativeClass,rule.totalPieces,palettes,actor.combatRandom);
    if(rule.sharedRandomComponents) {
        const auto value=uint8_t(rollRandom(actor.combatRandom)&3);
        for(size_t slot=0;slot<actor.components.size();++slot)
            if(rule.sharedRandomComponents&(1u<<slot)) actor.components[slot]=value;
    }
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    return candidate;
}
void System::publishComponents(ComponentPaletteState &candidate) noexcept {
    for(auto &[area,palettes]:candidate)
        if(const auto current=state_.componentPalettes.find(area);current!=state_.componentPalettes.end()) current->second.swap(palettes);
    state_.componentPalettes.merge(candidate);
}
DomainResult<> System::admitNpc(EntityId id,RegionId region,Vec position,const NpcRule &npc,bool resurrected) {
    if(!id || state_.actors.contains(id) || !npc.combat || !ports_.areas.find(region)) return {DomainStatus::InvalidRequest,{}};
    if(state_.actors.size()>=65536) return {DomainStatus::Capacity,{}};
    const bool allocate=id.value==ports_.ids.cursor();
    if(allocate) {
        const auto &definitions=ports_.areas.at(region).definition.questNpcs;
        const auto source=definitions.find(npc.code);
        if(source==definitions.end() || &source->second!=&npc) return {DomainStatus::InvalidRequest,{}};
    }
    Admission request;request.identity.monster=npc.code;request.identity.origin=SpawnOrigin::Preset;
    request.identity.spawnKey="npc."+std::to_string(id.value);request.area=region;request.position=position;
    request.rule=*npc.combat;request.tick=ports_.frameEvents.currentFrame();request.resurrected=resurrected;
    auto random=ports_.random;ComponentPaletteState palettes;
    auto prepared=prepareAdmissionActor(request,id,random,palettes,ports_.ids.cursor()+size_t(allocate));
    if(!prepared) return {prepared.status,{}};
    auto actor=std::move(*prepared.value);actor.questNpc=true;actor.movementGoal=position;
    const auto allocated=actor.birthEquipmentItems+size_t(allocate);
    const std::array presets{id};
    auto publication=prepareActorPublication({{id,std::move(actor)}},allocated,{},nullptr,{},
        allocate?std::span<const EntityId>{}:std::span<const EntityId>{presets});
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::moveNpc(EntityId id,RegionId region,Vec position,Vec destination,bool escaped) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.questNpc || found->second.area!=region || !found->second.alive()) return {DomainStatus::Stale,{}};
    auto &actor=found->second;
    if(actor.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(escaped) {
        ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
        ports_.areas.removeRoomUnit(id);state_.actors.erase(found);
    } else {
        actor.position=position;actor.movementGoal=destination;actor.moving=(destination-position).length()>.001f;
        actor.pathType=1;actor.pathSteps=1;++actor.revision;ports_.areas.moveRoomUnit(id,region,position);refreshCollision(actor);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
EntityId System::minionOwner(EntityId id) const {
    const auto *unit=find(id);if(!unit) return {};
    if(unit->detachedMinion) return {};
    if(unit->nativeMinionOwner) return *unit->nativeMinionOwner;
    if(unit->owner && !unit->conversion) {
        const auto *owner=ports_.players.find(*unit->owner);return owner?owner->actor:EntityId{};
    }
    if(unit->identity.ownerSpawnKey.empty()) return {};
    for(const auto &[key,owner]:state_.actors)
        if(owner.area==unit->area && owner.identity.spawnKey==unit->identity.ownerSpawnKey) return key;
    return {};
}
std::vector<EntityId> System::specialTargets() const {
    std::vector<EntityId> result;
    for(const auto &[id,unit]:state_.actors) if(unit.specialTargetOrder) result.push_back(id);
    std::sort(result.begin(),result.end(),[&](EntityId left,EntityId right) {return state_.actors.at(left).specialTargetOrder>state_.actors.at(right).specialTargetOrder;});
    return result;
}
DomainResult<> System::registerWanderTarget(EntityId id) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(unit.rule.ai.kind!=MonsterAiKind::NpcBarb) return {DomainStatus::InvalidRequest,{}};
    // Fn127 only registers node11. Existing group9/player nodes stay there.
    if(unit.wanderTargetOrder || unit.specialTargetOrder || unit.owner) return {DomainStatus::Applied,std::monostate{}};
    if(state_.nextWanderTargetOrder==UINT64_MAX || unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    unit.wanderTargetOrder=state_.nextWanderTargetOrder++;++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<Vec> System::ensureAiHomeCommand(EntityId id) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(!unit.aiHomeCommand) {
        if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        const auto global=unit.position+ports_.areas.at(unit.area).definition.origin;
        unit.aiHomeCommand=Vec{std::floor(global.x),std::floor(global.y)};++unit.revision;
    }
    return {DomainStatus::Applied,*unit.aiHomeCommand};
}
void System::resetAiFunctionMemory(EntityId id) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return;
    auto &unit=found->second;
    if(!unit.aiHomeCommand && !unit.baalThroneWave) return;
    if(unit.revision==UINT64_MAX) throw std::overflow_error("Monster AI memory revision exhausted");
    unit.aiHomeCommand.reset();unit.baalThroneWave=0;++unit.revision;
}
void System::registerPet(Actor &unit) noexcept {
    publishInitialAi(unit);
    if(!unit.owner || unit.conversion || unit.rule.petType<=0 || unit.petRegistrationOrder) return;
    if(state_.nextPetRegistrationOrder==UINT64_MAX) std::terminate();
    unit.petRegistrationOrder=state_.nextPetRegistrationOrder++;
    unit.inactivePet=false;
}
std::vector<EntityId> System::pets(PlayerId owner) const {
    std::vector<EntityId> result;
    for(const auto &[id,unit]:state_.actors)
        if(unit.owner==owner && unit.petRegistrationOrder && !unit.inactivePet && !unit.conversion) result.push_back(id);
    std::sort(result.begin(),result.end(),[&](EntityId left,EntityId right) {
        const auto &a=state_.actors.at(left),&b=state_.actors.at(right);
        return a.rule.petType!=b.rule.petType?a.rule.petType<b.rule.petType:a.petRegistrationOrder<b.petRegistrationOrder;
    });
    return result;
}
} // namespace d2x::server::monsters
