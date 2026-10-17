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

namespace d2x::server::monsters {
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

int movementPercent(const Actor &actor,uint64_t tick) {
    const int64_t value=int64_t(actor.velocityPercent)+actor.effectVelocity+actor.hirelingMovementPercent+
        (actor.necroPet?actor.necroPet->velocityPercent:0)+(actor.rule.enchantment?actor.rule.enchantment->velocityPercent:0);
    return int(std::clamp<int64_t>(value,25,INT16_MAX));
}
namespace {
std::optional<std::pair<Vec,int>> nativePathTarget(const PlayerStore &players,const System &monsters,EntityId id,RegionId area) {
    for(const auto &[key,player]:players.all()) {
        (void)key;
        if(player.actor==id && player.entered && player.area==area) return std::pair{player.position,2};
    }
    const auto *unit=monsters.find(id);
    if(unit && unit->area==area) return std::pair{unit->position,unit->rule.size};
    return {};
}
int recoveryTicks(const Actor &actor,const effects::System &effects,uint64_t tick,bool block) {
    const auto &clock=block?actor.rule.blockAnimation:actor.rule.hitRecoveryAnimation;
    if(!clock) return block?actor.rule.blockTicks:actor.rule.hitRecoveryTicks;
    auto modifiers=effects.unitModifiers(actor.id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    const int speed=effectiveRecoverySpeed(clock->speed,block?modifiers.fasterBlock:modifiers.fasterHitRecovery);
    return monsterAnimationTicks({clock->frames,speed});
}
void playerCountBonus(MonsterRule &rule,unsigned count) {
    const unsigned scale=100+50*(count-1);
    rule.experienceBase=rule.experienceBase*scale/100;
    for(size_t level=1;level<rule.experience.size() && level<rule.experienceRatios.size();++level)
        rule.experience[level]=monsterExperienceGain(rule.experienceBase,1,rule.experienceLevel,int(level),rule.experienceRatios[level],rule.experienceShift);
    if(rule.difficulty==0 || count==1) return;
    // D2MOO MonsterMode::sub_6FC627B0 uses 8*(count-1)/128 for
    // expansion Nightmare/Hell attack damage, to-hit and native element duration.
    const auto scaled=[&](int value){return int(std::min(int64_t(INT32_MAX),int64_t(value)+int64_t(value)*8*(count-1)/128));};
    rule.minimumDamage=scaled(rule.minimumDamage);rule.maximumDamage=scaled(rule.maximumDamage);rule.attackRating=scaled(rule.attackRating);
    const auto attackBonus=[&](MonsterAttackRule &attack) {
        attack.minimum=scaled(attack.minimum);attack.maximum=scaled(attack.maximum);attack.rating=scaled(attack.rating);
        for(auto &element:attack.elements) {
            element.minimum=scaled(element.minimum);element.maximum=scaled(element.maximum);element.durationFrames=scaled(element.durationFrames);
        }
    };
    for(auto &[mode,attack]:rule.attacks) {(void)mode;attackBonus(attack);}
    for(auto &[skill,attack]:rule.skillActions) {(void)skill;attackBonus(attack);}
}
}
void System::withdrawQuestGroup(RegionId region,std::string_view name) {
    const auto key="quest."+std::string(name);
    std::erase_if(state_.actors,[&](const auto &entry){const auto &m=entry.second;const bool removed=m.area==region && !m.owner && m.identity.origin==SpawnOrigin::Preset && (m.identity.spawnKey==key || m.identity.ownerSpawnKey==key);if(removed) {ports_.effects.forgetUnit(entry.first);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,entry.first});ports_.areas.removeRoomUnit(entry.first);}return removed;});
}
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area = ports_.areas.find(request.area);
    if (!area || area->definition.town || !request.hostile || !request.rule || (!request.implementation && request.rule->trap==MonsterRule::Trap::None) || request.identity.spawnKey.empty())
        return {DomainStatus::InvalidRequest, {}};
    const auto &rule = *request.rule;
    if (rule.nativeClass < 0 || rule.nativeClass > UINT16_MAX || rule.minimumLife <= 0 || rule.maximumLife < rule.minimumLife ||
        rule.nativeVelocity < 0 || rule.nativeVelocity > 255 || rule.difficulty < 0 || rule.difficulty > 2 || rule.alignment < 0 || rule.alignment > 2 ||
        rule.level <= 0 || (rule.attacks.empty() && rule.skillActions.empty())) return {DomainStatus::InvalidRequest, {}};
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
DomainResult<Actor> System::prepareAdmissionActor(const Admission &request,EntityId id,uint64_t &random,ComponentPaletteState &palettes,uint64_t itemCursor,const std::set<size_t> *stagedUniques) {
    if(!request.rule || !request.rule->stats || !request.rule->formulas || request.rule->minimumLife<0 ||
        request.rule->maximumLife<request.rule->minimumLife) return {DomainStatus::InvalidRequest,{}};
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
    const bool activeAi=request.hostile || rule.ai.kind==MonsterAiKind::GoodNpcRanged;
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
    actor.roomAdmission=ports_.areas.prepareRoomUnit(id,1,actor.area,actor.position);
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
        actor.pathType=1;actor.pathSteps=1;++actor.revision;ports_.areas.moveRoomUnit(id,region,position);
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
void System::maximumLife(EntityId id,int64_t value) {
    if(value<=0 || value>INT32_MAX) throw std::runtime_error("Monster maximum life outside native signed range");
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || found->second.maximumLife==value) return;
    auto &unit=found->second;
    const auto before=Fixed8::fromRaw(nativeChecked(unit.maximumLife)),after=Fixed8::fromRaw(nativeChecked(value));
    const bool callback=unit.life>0 && before>0;
    unit.life=resourceOnMaximumChange(Fixed8::fromRaw(nativeChecked(unit.life)),before,after).raw();
    unit.maximumLife=value;
    if(callback && unit.nativeStats && unit.rule.stats && unit.rule.damageRegen) {
        auto root=std::make_shared<StatList>(*unit.nativeStats->sources);
        setNativeStat(*root,*unit.rule.stats,"hpregen",monsterRegenerationOnMaximumChange(after,unit.rule.damageRegen));
        unit.nativeStats->values=evaluateStatList(*unit.rule.stats,*root,{root.get(),{}, {}});
        unit.nativeStats->sources=std::move(root);
    }
    ++unit.revision;
}
bool System::canCommitLife(const LifeChange &change) const {
    const auto *unit=find(change.actor);
    if(!unit) return false;
    if(change.nativeMinions && !change.changed) return false;
    if(change.random && (unit->combatRandom!=change.random->first || !change.changed)) return false;
    if(change.itemAuraTimers && !change.periodicEvents && (!change.itemAuraTimers->empty() ||
        ports_.frameEvents.hasEvent({FrameEventUnit::Monster,change.actor},FrameEventType::PeriodicStats))) return false;
    if(change.periodicEvents && (change.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Monster,change.actor} ||
        !ports_.frameEvents.validate(change.periodicEvents->edit))) return false;
    if(change.specialTargetNode.value_or(false) && !unit->specialTargetOrder && state_.nextSpecialTargetOrder==UINT64_MAX) return false;
    if((change.curseControl || change.reselectAi) && unit->aiFunctionRevision==UINT64_MAX) return false;
    if((change.tauntPostActions || change.restoreNeutral || change.aiThinkAt) && unit->aiThinkRevision==UINT64_MAX) return false;
    const bool alignmentChanged=change.intrinsic && unit->rule.stats &&
        change.intrinsic->values.get({unit->rule.stats->names.at("alignment"),0})!=int(unit->alignment());
    if((alignmentChanged || change.curseControl || change.attraction || change.detachedMinion) && unit->allegianceRevision==UINT64_MAX) return false;
    return unit && unit->revision==change.expectedRevision && (!(change.cureColdPoison || change.shatter || change.poison || change.itemAuraTimers || change.curseControl || change.stopMovement || change.hitDelay || change.attraction || change.aiResetTimers || change.specialTargetNode || change.detachedMinion || change.tauntPostActions || change.reselectAi || change.chilledUntil || change.frozenUntil || change.stunnedUntil || change.restoreNeutral || change.wound || change.aiThinkAt) || change.changed) && (!change.changed || unit->revision<UINT64_MAX) &&
        change.maximum>=0 && change.life>=0 && change.life<=change.maximum;
}
DomainResult<> System::consumeRandom(EntityId id,uint64_t expected,uint64_t next) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.combatRandom!=expected) return {DomainStatus::Stale,{}};
    if(expected!=next) {
        if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        unit.combatRandom=next;++unit.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::commitLife(LifeChange &&change) noexcept {
    if(!canCommitLife(change)) return false;
    auto &unit=state_.actors.at(change.actor);
    if(change.changed) {
        const auto alignment=unit.alignment();
        unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();unit.nativeStats=std::move(change.intrinsic);++unit.revision;
        if(change.random) unit.combatRandom=change.random->second;
        if(unit.petRegistrationOrder && !unit.life && !unit.modeAlive) unit.inactivePet=true;
        if(change.cureColdPoison) {unit.poison.reset();unit.chilledUntil=unit.frozenUntil=0;unit.shatterActive=false;}
        if(change.shatter) unit.shatterActive=*change.shatter;
        if(change.poison) unit.poison=*change.poison;
        if(change.wound) unit.wound=*change.wound;
        if(change.chilledUntil) unit.chilledUntil=*change.chilledUntil;
        if(change.frozenUntil) unit.frozenUntil=*change.frozenUntil;
        if(change.stunnedUntil) unit.stunnedUntil=*change.stunnedUntil;
        if(change.restoreNeutral) {
            unit.route.clear();unit.moving=unit.running=false;unit.busyUntil=unit.reactionUntil=0;unit.neutralAt.reset();
        }
        if(change.itemAuraTimers) unit.itemAuraTimers=std::move(*change.itemAuraTimers);
        if(change.periodicEvents) ports_.frameEvents.publish(std::move(change.periodicEvents->edit));
        if(change.curseControl) {
            const auto before=unit.curseControl.ai,after=change.curseControl->ai;
            if(change.reselectAi || (before!=after && before!=CurseAi::Confuse && after!=CurseAi::Confuse && before!=CurseAi::Attract && after!=CurseAi::Attract)) {
                ++unit.aiFunctionRevision;unit.aiThinkAt=0;
            }
            unit.curseControl=*change.curseControl;
        }
        if(change.tauntPostActions) unit.mainOwnerCleared=true;
        if(change.restoreNeutral || change.tauntPostActions || change.aiThinkAt) {
            ++unit.aiThinkRevision;unit.aiThinkAt=change.aiThinkAt.value_or(0);
            scheduleAi(unit.id,unit.aiThinkAt);
        }
        if(change.tauntTarget) unit.movementTarget=unit.tauntTarget=*change.tauntTarget;
        if(change.detachedMinion) {
            unit.detachedMinion=*change.detachedMinion;
            if(*change.detachedMinion) unit.nativeMinionOwner=EntityId{};
        }
        if(change.nativeMinions) unit.nativeMinions=std::move(*change.nativeMinions);
        if(change.specialTargetNode) {
            if(!*change.specialTargetNode) unit.specialTargetOrder=0;
            else if(!unit.specialTargetOrder && !unit.owner) unit.specialTargetOrder=state_.nextSpecialTargetOrder++;
        }
        if(change.stopMovement) {unit.route.clear();unit.moving=false;unit.running=false;}
        if(change.hitDelay) unit.nextHitTick=std::max(unit.nextHitTick,*change.hitDelay);
        if(change.attraction) {unit.attractionTarget=change.attraction->target;unit.attractionUntil=change.attraction->until;}
        if(change.aiResetTimers) {unit.aiResetTimers=std::move(*change.aiResetTimers);unit.nativeAiResetTracked=true;}
        if(alignment!=unit.alignment() || change.curseControl || change.attraction || change.detachedMinion) ++unit.allegianceRevision;
    }
    return true;
}
std::vector<FrameEventQueue::PreparedInsertion> System::prepareDeathSweep(Actor &actor,uint64_t tick) const {
    std::vector<FrameEventQueue::PreparedInsertion> events;
    if(!actor.rule.deathSweep) return events;
    const auto &sweep=*actor.rule.deathSweep;auto random=actor.combatRandom;
    const auto point=actor.position+ports_.areas.at(actor.area).definition.origin;
    const auto targets=ports_.effects.nativeAuraCandidateRecipients({actor.id,actor.area,sweep.radius,0x583,false,
        point,effects::NativeAuraTargetMode::TargetRange,tick,point});
    for(const auto key:targets) if(const auto *other=find(key);key!=actor.id && other && other->alive() &&
        other->alignment()==UnitAlignment::Evil && (!sweep.undeadOnly || other->rule.undead)) {
        const auto delay=nativeAdd(sweep.minimumDelay,int32_t(limitedRandom(random,uint32_t(nativeSubtract(sweep.maximumDelay,sweep.minimumDelay)))));
        events.push_back(ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,key},FrameEventType::MonsterModifier,
            tick+uint64_t(uint32_t(delay)),false,0,21}));
    }
    actor.combatRandom=random;return events;
}
DomainResult<Actor> System::prepareDeathActor(EntityId id,uint64_t tick) const {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    return prepareDeathActor(found->second,tick);
}
DomainResult<Actor> System::prepareDeathActor(const Actor &current,uint64_t tick) const {
    const auto id=current.id;if(!current.alive()) return {DomainStatus::Applied,current};
    if(current.revision>UINT64_MAX-4 || current.interruption==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto unit=current;unit.life=0;unit.modeAlive=false;unit.deathTick=tick;unit.deathOccurrence=0;
    if(unit.petRegistrationOrder) unit.inactivePet=true;
    if(unit.questNpc || unit.rule.environmentController) unit.rewardComplete=true;
    unit.itemAuraTimers.clear();unit.poison.reset();unit.wound.reset();
    unit.busyUntil=tick+uint64_t(unit.rule.deathTicks);unit.route.clear();unit.moving=unit.running=false;
    unit.movementTarget={};unit.aiPose=0;unit.holdPose=false;unit.poseUntil=unit.riseUntil=unit.reactionUntil=0;
    unit.chilledUntil=unit.frozenUntil=unit.stunnedUntil=0;unit.shatterActive=false;unit.delayedDeathSource.reset();
    const auto kind=unit.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
    unit.corpse.states.clear();
    if(unit.rule.states) for(const auto state:ports_.effects.unitStates(id,tick)) {
        const auto definition=unit.rule.states->find(state);
        if(definition!=unit.rule.states->end() && definition->second.stayOnDeath[size_t(kind)]) {
            unit.corpse.states.insert(state);
            unit.corpse.unavailable|=definition->second.hideOnDeath || definition->second.shatterOnDeath || definition->second.corpseUnselectable;
        }
    }
    unit.aiStates.clear();unit.spawnReservation.reset();++unit.interruption;++unit.revision;
    return {DomainStatus::Applied,std::move(unit)};
}
std::vector<Actor> System::prepareMinionDeaths(Actor candidate,uint64_t tick,const std::map<EntityId,Actor> *staged) const {
    std::map<EntityId,Actor> changed;std::vector<EntityId> order;
    const auto first=candidate.id;changed.emplace(first,std::move(candidate));order.push_back(first);
    const auto edit=[&](EntityId id)->Actor * {
        if(const auto found=changed.find(id);found!=changed.end()) return &found->second;
        const auto *source=find(id);
        if(staged) if(const auto found=staged->find(id);found!=staged->end()) source=&found->second;
        if(!source) return nullptr;
        if(source->revision==UINT64_MAX) throw std::overflow_error("Native minion owner revision exhausted");
        auto copy=*source;++copy.revision;order.push_back(id);return &changed.emplace(id,std::move(copy)).first->second;
    };
    std::vector<EntityId> pending{first};
    for(size_t cursor=0;cursor<pending.size();++cursor) {
        auto &unit=changed.at(pending[cursor]);
        if(unit.alive()) continue;
        if(unit.ownsNativeMinions && !unit.ownershipDeathHandled) {
            unit.ownershipDeathHandled=true;
            if(!unit.transferMinionsOnDeath) {
                for(const auto id:unit.nativeMinions)
                    if(auto *child=edit(id)) child->nativeMinionOwner=EntityId{};
            } else {
                for(const auto id:unit.nativeMinions) if(id!=unit.id) if(auto *successor=edit(id)) {
                    successor->nativeMinionOwner=id;successor->ownsNativeMinions=true;
                    successor->transferMinionsOnDeath=true;unit.nativeMinionOwner=id;
                    successor->nativeMinions.insert(successor->nativeMinions.begin(),unit.id);
                    for(const auto other:unit.nativeMinions) if(other!=id) if(auto *child=edit(other)) {
                        child->nativeMinionOwner=id;
                        successor->nativeMinions.insert(successor->nativeMinions.begin(),other);
                    }
                    break;
                }
            }
        }
        if(!unit.killMinionsOnDeath || unit.minionDeathHandled) continue;
        unit.minionDeathHandled=true;
        const auto owner=unit.nativeMinionOwner.value_or(minionOwner(unit.id));
        if(!owner || (!find(owner) && !ports_.players.findActor(owner))) continue;
        const auto minions=unit.nativeMinions;unit.nativeMinions.clear();
        for(const auto id:minions) if(auto *child=edit(id)) {
            // FreeMinionList + clear owner precede ModeDeath. Its UMod15
            // callback consequently cannot recurse through the old parent.
            child->nativeMinionOwner=EntityId{};
            if(child->alive()) {
                auto dead=prepareDeathActor(*child,tick);
                if(!dead) throw std::overflow_error("Native minion death revision exhausted");
                *child=std::move(*dead.value);pending.push_back(id);
            }
        }
        if(auto *parent=edit(owner)) {
            parent->nativeMinionOwner=EntityId{};
            if(owner!=unit.id && parent->alive()) {
                auto dead=prepareDeathActor(*parent,tick);
                if(!dead) throw std::overflow_error("Native minion parent death revision exhausted");
                *parent=std::move(*dead.value);pending.push_back(owner);
            }
        }
    }
    std::vector<Actor> result;result.reserve(order.size());
    for(const auto id:order) result.push_back(std::move(changed.at(id)));
    return result;
}
DomainResult<> System::scriptDeath(EntityId id,uint64_t tick) {
    const auto *live=find(id);if(!live) return {DomainStatus::InvalidActor,{}};
    if(!live->alive()) return ports_.effects.monsterDeathEffect(id,tick);
    const auto &current=*live;
    auto prepared=prepareDeathActor(id,tick);if(!prepared) return {prepared.status,{}};
    auto unit=std::move(*prepared.value);
    auto cascades=prepareDeathSweep(unit,tick);
    auto publication=ports_.effects.prepareDamageStates(std::move(unit),{},tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    publication.facts.emplace_back(HitFact{id,1,current.area,0,true,current.position,0,0});
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},current.area},std::move(publication.facts)});
    if(!sent) return {sent.status,{}};
    publication.actors->actors.at(id).deathOccurrence=*sent.value;
    ports_.effects.publishAuraUnits(publication);
    ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills);
    ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicStats);
    for(auto &cascade:cascades) ports_.frameEvents.publish(std::move(cascade));
    return ports_.effects.monsterDeathEffect(id,tick);
}
DomainResult<> System::finishTrap(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.rule.environmentController) return {DomainStatus::InvalidActor,{}};
    return scriptDeath(id,tick);
}
DomainResult<> System::reduceShrineLife(EntityId id,int64_t amount) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.damageable() || found->second.life<=0 || amount<0 || amount>=found->second.life) return {DomainStatus::InvalidActor,{}};
    found->second.life-=amount;++found->second.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::upgradeShrine(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::Stale,{}};
    auto &actor=found->second;
    if(modifierPending_) return {DomainStatus::Conflict,{}};
    if(!actor.enemyTarget() || actor.life<=0 || actor.identity.rank!=MonsterRank::Normal || !actor.rule.shrineEligible || !actor.rule.modifierChoices ||
        (actor.moving && actor.running) || actor.busyUntil>tick || actor.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    if(nextModifierToken_==UINT64_MAX) return {DomainStatus::Capacity,{}};
    ModifierPreparation request;request.token=nextModifierToken_;request.tick=tick;request.area=actor.area;
    request.generation=ports_.areas.at(actor.area).generation;request.expectedRandom=request.nextRandom=actor.combatRandom;
    request.leader={id,actor.revision,actor.identity,actor.rule.level,actor.rule.nativeClass};
    const auto &choices=*actor.rule.modifierChoices;
    const bool champion=limitedRandom(request.nextRandom,100)<uint32_t(choices.championChance);
    request.leader.identity.rank=champion?MonsterRank::Champion:MonsterRank::Unique;
    const int count=champion?1:actor.rule.difficulty+int(limitedRandom(request.nextRandom,1))+1;
    request.selection.ids=chooseMonsterModifiers(choices.candidates[champion?1:0],actor.rule.nativeModifiers,count,request.nextRandom);
    std::vector<EntityId> minions;
    for(const auto otherId:actor.nativeMinions) {
        const auto *minion=find(otherId);
        if(!minion || otherId==id || minion->area!=actor.area || minion->life<=0 || !minion->enemyTarget() || minionOwner(otherId)!=id) continue;
        minions.push_back(otherId);
    }
    return prepareBirthModifiers(id,request.leader.identity.rank,std::move(request.selection.ids),minions,tick,
        std::pair{request.expectedRandom,request.nextRandom});
}
DomainResult<> System::prepareBirthModifiers(EntityId id,MonsterRank rank,std::vector<int> modifiers,
    std::span<const EntityId> minions,uint64_t tick,std::optional<std::pair<uint64_t,uint64_t>> random,bool promoteMinions,std::optional<uint16_t> nameSeed) {
    const auto *actor=find(id);
    if(!actor || (!actor->alive() && !actor->restorationPending) || modifiers.size()>9 ||
        (rank!=MonsterRank::Unique && rank!=MonsterRank::Champion && rank!=MonsterRank::SuperUnique && !(nameSeed && rank==MonsterRank::Minion)))
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
    const auto rolledName=rank==MonsterRank::Minion?uint16_t{}:uint16_t(rollRandom(request.nextRandom));
    request.selection.nameSeed=nameSeed.value_or(rolledName);
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
        resources.writeBaseStat("maxhp",maximum,sources);resources.seedLife(Fixed8::fromRaw(current.alive()?maximum:0));
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
    auto leader=upgrade(actor,std::move(rule),request.leader.identity.rank,request.leader.identity.rank!=MonsterRank::Minion);
    if(request.leader.identity.rank!=MonsterRank::Minion) leader.identity.ownerSpawnKey.clear();
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
    if(actor.alive() && actor.rule.enchantment->aura) {
        const auto &aura=*actor.rule.enchantment->aura;
        const auto assigned=ports_.effects.assignUnitAura(id,aura,tick,aura.rank);
        if(!assigned) return isDeferred(assigned.status) || assigned.status==DomainStatus::Conflict
            ?StepStatus::Blocked:StepStatus::NotImplemented;
    }
    modifierPending_.reset();modifierPrepared_.reset();modifierCommitted_=false;
    return StepStatus::Complete;
}
std::optional<std::pair<Vec,int>> System::targetPosition(EntityId id,RegionId area) const {
    for(const auto &[key,p]:ports_.players.all()) {
        (void)key;if(p.actor==id && p.entered && p.area==area && p.persistent.player.hp>0) return std::pair{p.position,2};
    }
    const auto *m=find(id);
    if(m && m->area==area && m->life>0 && m->damageable()) return std::pair{m->position,m->rule.size};
    return {};
}
bool System::recacheHirelingRoom(Actor &actor) {
    if(!actor.hireling) return true;
    constexpr Vec center{.5f,.5f};
    const auto &source=ports_.areas.at(actor.area);
    if(source.definition.activation.room(actor.position+center)) return true;
    Vec navigationOrigin;
    const auto navigation=source.definition.collision.linkedNavigation(navigationOrigin);
    const AreaState *destination=nullptr;Vec offset;
    for(const auto &[region,candidate]:ports_.areas.all()) {
        (void)region;
        if(candidate.definition.act!=source.definition.act ||
            std::none_of(navigation.navigationSources.begin(),navigation.navigationSources.end(),
                [&](const auto &entry){return entry.grid==&candidate.definition.collision;})) continue;
        const Vec translation=source.definition.origin-candidate.definition.origin;
        if(!candidate.definition.activation.room(actor.position+translation+center)) continue;
        if(destination) return false;
        destination=&candidate;offset=translation;
    }
    if(!destination) return false;
    // PATH_RecacheRoomAtCoordIfNeeded moves room ownership without restarting
    // the path or treating a continuous level boundary as a pet teleport.
    actor.area=destination->definition.id;
    actor.position=actor.pathStep.translate(actor.position+center,offset)-center;
    actor.movementGoal=actor.movementGoal+offset;
    for(auto &point:actor.route) point=point+offset;
    return true;
}
DomainResult<> System::requestMove(const MoveRequest &request) {
    const auto it = state_.actors.find(request.actor);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto &area = ports_.areas.at(actor.area);
    if (request.destination.area != actor.area || request.destination.generation != area.generation)
        return {DomainStatus::Stale, {}};
    // Native paths retain a unit target independently of combat eligibility:
    // Fenris and resource vines must be able to approach a real corpse.
    if (actor.rule.nativeVelocity <= 0 || (request.target && !nativePathTarget(ports_.players,*this,request.target,actor.area)) || request.stopDistance<0 || request.stopDistance>255 ||
        request.velocityPercent<25 || request.velocityPercent>INT16_MAX) return {DomainStatus::InvalidRequest,{}};
    const auto collision=(actor.airborne || actor.submerged)?MovementCollisionRule{0,1}:actor.rule.collision;
    uint8_t pathType=request.circleTurn?1:request.pathType;
    // MonsterMode's default obstacle-search distance is 5; pet tactics supply
    // their own 40/100 bounds. Circling is
    // published as AStar to its computed last point by SCmd, not as pursuit.
    const uint8_t pathDistance=request.pathDistance;
    if((pathType!=1 && pathType!=2 && pathType!=7 && pathType!=13 && pathType!=15) || !pathDistance)
        return {DomainStatus::InvalidRequest,{}};
    const Vec center{.5f,.5f};
    auto route = request.circleTurn?area.definition.collision.nativeMonsterCirclePath(actor.position+center,request.destination.position+center,request.circleDistance,request.circleTurn,collision):
        area.definition.collision.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision);
    if(route.empty() && !request.circleTurn && (pathType==2 || pathType==7 || pathType==13)) {
        // MonsterMode retries failed Toward paths with WallFollow. The
        // solver raises its search bound to 40 for a live unit target.
        pathType=15;
        route=area.definition.collision.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision,bool(request.target));
    }
    for(auto &point:route) point=point-center;
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    if(request.pathSteps<0 || request.pathSteps>255) return {DomainStatus::InvalidRequest,{}};
    // PATH_SetStepNum stores steps - 1 as a unit-distance arrival bound.
    // It never limits the number of route points (D2Common PathMisc).
    const auto goal=request.circleTurn?route.back():request.destination.position;
    actor.route = std::move(route); actor.movementTarget = request.circleTurn?EntityId{}:request.target;
    actor.pathStep.reset();
    actor.pathSteps=uint8_t(request.pathSteps);
    actor.pathType=pathType;actor.pathDistance=pathDistance;
    actor.movementGoal = goal;
    actor.stopDistance = request.pathSteps>0?request.pathSteps-1:request.stopDistance;
    actor.velocityPercent = request.velocityPercent; actor.running = request.running;
    if(!actor.airborne) {actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;}
    ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        // Step::PATH_ResetMovement clears points and velocity, not pTargetUnit.
        if (!actor.route.empty() || actor.moving || actor.running) { actor.route.clear(); actor.moving = false; actor.running = false; ++actor.revision; }
    }
}
DomainResult<> System::beginAttack(EntityId id, uint64_t until, bool returnToNeutral, EntityId target,std::optional<NativeUnitStats> modeStats) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || !it->second.alive()) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    if(actor.nativeActionRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    ++actor.nativeActionRevision;actor.nativeActionInterruption=actor.interruption;actor.nativeActionUntil=until;
    if(modeStats) actor.nativeStats=std::move(modeStats);
    actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;
    actor.neutralAt=returnToNeutral?std::optional{until}:std::nullopt;
    // MonsterMode's mode change installs the action target on the same path.
    // BL must retain that identity after an attacking actor stops walking.
    actor.busyUntil = until; actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = target; ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::extendAttack(EntityId id,uint64_t until) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    actor.nativeActionUntil=std::max(actor.nativeActionUntil,until);
    actor.busyUntil=std::max(actor.busyUntil,until);
    if(actor.neutralAt) actor.neutralAt=std::max(*actor.neutralAt,until);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
uint64_t System::resumeAi(EntityId id,uint64_t tick,uint64_t scheduled) {
    auto found=state_.actors.find(id);
    if(found==state_.actors.end()) return scheduled;
    auto &actor=found->second;
    if(!actor.alive()) {actor.neutralAt.reset();return scheduled;}
    if(!actor.neutralAt || tick<*actor.neutralAt || frozen(id,tick)) return scheduled;
    const auto ended=std::max(*actor.neutralAt,actor.frozenUntil);
    actor.neutralAt.reset();
    // MonsterMode::sub_6FC64310 preserves an already scheduled future AI
    // event. Default aidel starts at ENDANIM, not at attack start, and is
    // independent of busyUntil so the neutral pause cannot extend animation.
    if(scheduled<=ended) {
        const uint64_t delay=actor.stunnedUntil>ended?45:uint64_t(actor.rule.neutralDelayTicks>0?actor.rule.neutralDelayTicks:15);
        scheduled=ended+std::min(delay,UINT64_MAX-ended);
    }
    return scheduled;
}
void System::finishAction(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return;
    auto &actor=found->second;actor.nativeActionUntil=actor.busyUntil=tick;
    if(actor.skillMotion && actor.skillMotion->leaperRecovery) {actor.skillMotion.reset();actor.moving=false;}
    if(actor.neutralAt) actor.neutralAt=tick;
    ++actor.revision;
    const auto next=ports_.frameEvents.nextDue({FrameEventUnit::Monster,id},FrameEventType::AiThink).value_or(tick);
    scheduleAi(id,resumeAi(id,tick,next));
}
DomainResult<> System::damage(EntityId id, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames, bool freeze, uint8_t hitClass,std::optional<PoisonApplication> poison,bool poisonOnly,uint64_t stunFrames,std::optional<OpenWoundsApplication> wound,int64_t absorbed,bool clearStun,int64_t crushing,int coldPierce,bool softHit,std::optional<uint64_t> freezeLength,std::optional<bool> getHit,DamageStatus status) {
    auto it = state_.actors.find(id);
    if(it!=state_.actors.end() && !it->second.alive() && (it->second.rule.deathBurst || it->second.rule.boneDeathExplosion)) return ports_.effects.monsterDeathEffect(id,tick);
    if (it == state_.actors.end() || (it->second.life<=0 && !it->second.delayedDeathSource && !(status.actionOnly && it->second.modeAlive)) || !it->second.damageable() || crushing < 0) return {DomainStatus::InvalidActor, {}};
    if(it->second.revision>UINT64_MAX-4) return {DomainStatus::Capacity,{}};
    auto actor = it->second;
    if(wound && !actor.rule.stateTarget.acceptsCurseList()) wound.reset();
    // Crushing Blow changes current life before the ordinary damage channel;
    // it must not inflate the hit-recovery damage threshold.
    // The poison rate contributes once to the contact damage as well as to
    // HPREGEN (SUnitDmg total-damage channel). Duration immunity does not erase it.
    if(poison) amount=nativeAdd(nativeChecked(amount),nativeChecked(std::max<int64_t>(0,poison->rate)));
    const auto restoredLife=std::min(actor.maximumLife,actor.life+std::max<int64_t>(0,absorbed));
    const auto afterCrushing=std::max<int64_t>(0,restoredLife-crushing);
    const auto remainingLife=status.actionOnly?actor.life:poisonOnly && !poison?std::max<int64_t>(0,afterCrushing-amount):subtractContactResource(afterCrushing,amount);
    const auto life=std::max(actor.hireling && poisonOnly && !poison && ports_.areas.at(actor.area).definition.town?int64_t(256):int64_t(0),remainingLife);
    const uint8_t percent = monsterLifeRatio(life,actor.maximumLife);
    auto random=actor.combatRandom;
    auto modifiers=ports_.effects.unitModifiers(id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    auto chilled=actor.chilledUntil, frozen=actor.frozenUntil;
    auto shatterActive=actor.shatterActive;
    const auto states=ports_.effects.unitStates(id,tick);
    const bool uninterruptable=states.contains(actor.rule.uninterruptableState) || actor.aiStates.contains(actor.rule.uninterruptableState);
    if(actor.delayedDeathSource && (amount || uninterruptable)) return {DomainStatus::Unavailable,{}};
    const bool delayDeath=!life && uninterruptable;
    const bool dead=!life && !delayDeath && !status.deferAction;
    const bool freezable=!actor.hireling && !actor.rule.boss && !nativeUniqueMonster(actor.identity.rank);
    coldFrames=coldHitDuration(coldFrames,ports_.effects.unitResistance(id,DamageType::Cold,tick,coldPierce),0,
        modifiers.cannotBeFrozen,modifiers.halfFreezeDuration);
    const auto rawFreeze=freezeLength?coldHitDuration(*freezeLength,ports_.effects.unitResistance(id,DamageType::Cold,tick,coldPierce),0,
        modifiers.cannotBeFrozen,modifiers.halfFreezeDuration):freeze?coldFrames:0;
    bool appliedFreeze=false,coldApplied=false;std::optional<uint64_t> freezeThinkAt;
    const auto applyCold=[&](uint64_t frames) {
        if(!frames || !actor.rule.coldEffect) return;
        coldApplied=true;
        const auto duration=std::max<uint64_t>(1,actor.rule.coldEffect<0?frames/unsigned(actor.rule.coldDivisor):frames);
        chilled=std::max(chilled,tick+std::min(duration,UINT64_MAX-tick));
        // Retail 1.13c RVA DC8F8, unlike the reconstructed reversed condition.
        shatterActive=limitedRandom(random,100)<20 && !actor.rule.deadCollision;
    };
    if(freezeLength || !freeze) applyCold(coldFrames);
    if(rawFreeze && !uninterruptable) {
        if(freezable && actor.rule.coldEffect<0) {
            appliedFreeze=true;
            const auto duration=rawFreeze/unsigned(actor.rule.freezeDivisor);
            frozen=std::max(frozen,tick+std::min(duration,UINT64_MAX-tick));
            freezeThinkAt=tick+std::min(duration+1,UINT64_MAX-tick);
        } else if(!freezable) applyCold(rawFreeze);
    }
    const auto receivedCold=coldApplied && chilled>actor.chilledUntil?chilled:0;
    const auto receivedFreeze=appliedFreeze?frozen:0;
    auto deathStates=actor.corpse.states;
    bool corpseUnavailable=actor.corpse.unavailable;
    if(dead) {
        const auto retain=[&](const CombatStateDefinition &state,bool enabled) {
            const auto kind=actor.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
            if(enabled && state.id>=0 && state.stayOnDeath[size_t(kind)]) {
                deathStates.insert(state.id);
                corpseUnavailable|=state.hideOnDeath || state.shatterOnDeath || state.corpseUnselectable;
            }
        };
        retain(actor.rule.frozenState,appliedFreeze || frozen>tick);
        retain(actor.rule.shatterState,shatterActive ||
            (actor.rule.enchantment && actor.rule.enchantment->has(35)));
        chilled=frozen=0;shatterActive=false;
    }
    auto poisoned=actor.poison;bool appliedPoison=false;
    if(poison) {
        const int lengthResistance=actor.hireling?
            std::clamp(modifiers.poisonLengthResist-poison->lengthPierce+actor.rule.difficultyResistancePenalty,-100,75):
            piercedResistance(modifiers.poisonLengthResist,poison->lengthPierce);
        poison->frames=modifiers.preventPoison?0:resistedDuration(poison->frames,lengthResistance);
        if(life && poison->frames && poison->state>=0 && replacesPoison(poisoned?poisoned->damage.rate:0,poison->rate)) {
            appliedPoison=true;
            const auto until=tick+std::min(poison->frames,UINT64_MAX-tick);
            if(poisoned) {
                // Native refresh changes rate/deadline, retaining the stat-list owner.
                poisoned->damage.rate=poison->rate;poisoned->damage.frames=poison->frames;poisoned->until=until;
                poisoned->nativeExpireFrame.reset();poisoned->removalDue.reset();
            } else poisoned=PoisonStatus{*poison,source,until,tick+1};
        }
    }
    if(dead) poisoned.reset();
    auto stunned=actor.stunnedUntil;bool appliedStun=false;
    if(clearStun) stunned=0;
    const bool unique=nativeUniqueMonster(actor.identity.rank);
    if(stunFrames && unique && !status.stunApplied) throw std::logic_error("Unique stun requires its actual attacker seed decision");
    if(life && stunFrames && actor.rule.stunState>=0 && status.stunApplied.value_or(!unique && !actor.rule.boss && actor.rule.nativeVelocity!=0)) {appliedStun=true;stunned=tick+std::min<uint64_t>(actor.hireling?13:250,stunFrames);}
    if(dead) stunned=0;
    const bool recovery=!status.deferAction && life && !poisonOnly && getHit.value_or(!softHit) && monsterHitRecovery(amount,actor.maximumLife,hitClass,appliedFreeze || states.contains(actor.rule.frozenState.id),poisonOnly,actor.rule.hitRecoveryTicks>0,random,stunned>tick);
    const int reactionTicks=recovery?recoveryTicks(actor,ports_.effects,tick,false):0;
    std::vector<DomainFact> facts;
    const bool wasShatter=actor.shatterActive;
    const bool shatter=shatterActive || deathStates.contains(actor.rule.shatterState.id);
    if((shatter!=wasShatter || (!life && shatter)) && actor.rule.shatterState.id>=0) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,shatter});
    // Publish original states before the death action, including a lethal first freeze.
    facts.emplace_back(HitFact{id,1,actor.area,percent,dead,actor.position,hitClass,uint8_t(recovery?3:0),{},actor.lightningReady});
    actor.life = life;
    if(status.deferAction && !life) actor.modeAlive=true;
    if(delayDeath) {actor.modeAlive=true;actor.delayedDeathSource=status.source?status.source:source;actor.killer=source;}
    else if(dead) {actor.modeAlive=false;actor.delayedDeathSource.reset();}
    if(dead && actor.petRegistrationOrder) actor.inactivePet=true;
    ++actor.revision; if(amount>0 && !poisonOnly && !status.actionOnly) ++actor.damageOccurrence;
    actor.combatRandom=random;
    if (recovery) ++actor.hitOccurrence;
    actor.chilledUntil = chilled; actor.frozenUntil = frozen;
    actor.shatterActive=shatterActive;actor.corpse.states=std::move(deathStates);
    actor.stunnedUntil=stunned;
    actor.poison=poisoned;
    const bool appliedWound=life && bool(wound);
    if(dead) actor.wound.reset();
    else if(wound) {
        // Native same-state/same-skill refresh extends the existing stat list;
        // its original rate and source survive instead of stacking wounds.
        if(actor.wound) actor.wound->until=wound->until;
        else actor.wound=std::move(wound);
    }
    if(recovery || frozen>tick || stunned>tick || dead) {
        ++actor.interruption;actor.route.clear();actor.moving=actor.running=false;
        if(recovery) {actor.reactionMode=3;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;}
    }
    std::vector<FrameEventQueue::PreparedInsertion> cascades;
    if (dead) {
        actor.itemAuraTimers.clear();
        // Fn043 marks an exhausted nest NOTC before its own death action.
        if(source==id && (actor.rule.ai.kind==MonsterAiKind::FoulCrowNest || actor.rule.ai.kind==MonsterAiKind::Sarcophagus)) actor.rule.noTreasure=true;
        actor.corpse.unavailable=corpseUnavailable;
        actor.deathTick = tick; actor.deathOccurrence = 0; actor.killer = source;
        actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; actor.busyUntil = tick + uint64_t(actor.rule.deathTicks);
        cascades=prepareDeathSweep(actor,tick);
    }
    effects::UnitDamageStates updates{status.source?status.source:source,receivedCold,receivedFreeze,appliedStun?stunned:0,status.burn,status.burnFrames};
    updates.clearStun=clearStun;updates.poisonApplied=appliedPoison;updates.woundApplied=appliedWound;
    updates.freezeThinkAt=freezeThinkAt;updates.deathDelay=delayDeath;
    updates.resetRegeneration=life && poison && poison->frames && poison->state>=0;updates.contact=amount>0 && !status.actionOnly;
    auto publication=ports_.effects.prepareDamageStates(std::move(actor),std::move(updates),tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Conflict,{}};
    facts.insert(facts.begin(),std::make_move_iterator(publication.facts.begin()),std::make_move_iterator(publication.facts.end()));
    auto event=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},it->second.area},std::move(facts)});
    if(!event) return {event.status,{}};
    if(dead) publication.actors->actors.at(id).deathOccurrence=*event.value;
    ports_.effects.publishAuraUnits(publication);
    if(dead) {
        ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills);
        ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicStats);
    }
    for(auto &cascade:cascades) ports_.frameEvents.publish(std::move(cascade));
    return dead?ports_.effects.monsterDeathEffect(id,tick):DomainResult<>{DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::finishDamageAction(EntityId id,EntityId source,int64_t amount,uint64_t tick,uint8_t hitClass,bool softHit,std::optional<bool> getHit) {
    const auto *unit=find(id);if(!unit || !unit->alive() || unit->delayedDeathSource) return {DomainStatus::Applied,std::monostate{}};
    DamageStatus status;status.source=source;status.actionOnly=true;
    return damage(id,source,amount,tick,0,false,hitClass,{},false,0,{},0,false,0,0,softHit,{},getHit,status);
}
DomainResult<> System::block(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(actor.rule.blockTicks>0) {
        const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
        const int reactionTicks=recoveryTicks(actor,ports_.effects,tick,true);
        const uint8_t percent=monsterLifeRatio(actor.life,actor.maximumLife);
        HitFact hit{id,1,actor.area,percent,false,actor.position,0,6};
        if(actor.movementTarget && nativePathTarget(ports_.players,*this,actor.movementTarget,actor.area))
            hit.actionTarget=MotionTarget{actor.movementTarget,uint8_t(ports_.players.findActor(actor.movementTarget)?0:1)};
        auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{hit}});
        if(!result) return {result.status,{}};
        stop(id);++actor.interruption;actor.reactionMode=6;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;++actor.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::holyFreeze(EntityId id,uint64_t until,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;if(actor.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto random=actor.combatRandom;const bool active=rollRandom(random)%100<20;
    std::vector<DomainFact> facts;
    if(active!=actor.shatterActive && !actor.rule.shatterState.noSend)
        facts.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,active});
    auto deathStates=actor.corpse.states;
    if(!actor.alive()) {
        if(active) deathStates.insert(actor.rule.shatterState.id);
        else deathStates.erase(actor.rule.shatterState.id);
    }
    if(!facts.empty() && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    actor.holyFreezeUntil=until;actor.shatterActive=active;actor.combatRandom=random;actor.corpse.states.swap(deathStates);
    if(!actor.alive() && active) actor.corpse.unavailable|=actor.rule.shatterState.shatterOnDeath || actor.rule.shatterState.corpseUnselectable || actor.rule.shatterState.hideOnDeath;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::hitDelay(EntityId id, uint64_t until) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.nextHitTick = std::max(it->second.nextHitTick, until); }
void System::knockback(EntityId id, Vec source, uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || it->second.owner || it->second.rule.knockbackTicks<=0) return;
    auto &m=it->second;stop(id);m.knockbackSource=source;m.knockbackGoal=knockbackDestination(m.position,source,3);
    m.knockedUntil=tick+uint64_t(m.rule.knockbackTicks);m.busyUntil=m.knockedUntil;m.neutralAt.reset();++m.hitOccurrence;++m.interruption;++m.revision;
}
DomainResult<> System::missileReaction(EntityId id,EntityId source,bool getHit,bool knockback,uint8_t hitClass,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::Unavailable,{}};
    const auto &current=found->second;const auto states=ports_.effects.unitStates(id,tick);
    if(!current.alive() || states.contains(current.rule.uninterruptableState)) return {DomainStatus::Applied,std::monostate{}};
    auto actor=current;
    if(knockback && actor.rule.knockbackTicks<=0) {knockback=false;getHit=true;}
    const bool stunned=states.contains(actor.rule.stunState);
    if(!knockback && (!getHit || !stunned || actor.rule.hitRecoveryTicks<=0)) return {DomainStatus::Applied,std::monostate{}};
    if(actor.revision==UINT64_MAX || actor.interruption==UINT64_MAX || actor.hitOccurrence==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto duration=knockback?actor.rule.knockbackTicks:recoveryTicks(actor,ports_.effects,tick,false);
    actor.route.clear();actor.moving=false;actor.movementTarget={};
    actor.reactionMode=knockback?8:3;actor.reactionUntil=tick+std::min<uint64_t>(uint64_t(duration),UINT64_MAX-tick);
    actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;
    ++actor.interruption;++actor.revision;++actor.hitOccurrence;
    HitFact fact{id,1,actor.area,monsterLifeRatio(actor.life,actor.maximumLife),false,actor.position,hitClass,actor.reactionMode};
    if(knockback) {
        const auto attacker=combat::Participants{ports_.players,*this,ports_.social}.find(source);
        if(!attacker.id()) return {DomainStatus::Unavailable,{}};
        const auto region=attacker.player?attacker.player->area:attacker.monster->area;
        const auto point=attacker.position()+ports_.areas.at(region).definition.origin-ports_.areas.at(actor.area).definition.origin;
        actor.knockbackSource=point;actor.knockbackGoal=knockbackDestination(actor.position,point,3);
        actor.knockedUntil=actor.reactionUntil;fact.knockback=actor.knockbackGoal;
    }
    auto publication=prepareActorPublication({{id,std::move(actor)}},0);
    if(!validateActors(publication)) return {DomainStatus::Conflict,{}};
    const auto output=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},current.area},{fact}});
    if(!output) return {output.status,{}};
    publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
}
void System::rewardComplete(EntityId id) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.rewardComplete = true; }
bool System::canRemove(EntityId id) const {
    const auto *unit=find(id);if(!unit || !unit->owner) return false;
    return !ports_.frameEvents.hasHandler({FrameEventUnit::Monster,id},effects::monsterModifierTimerHandler);
}
DomainResult<> System::remove(EntityId id) {
    const auto *unit=find(id);if(!unit || !unit->owner) return {DomainStatus::InvalidActor,{}};
    if(!canRemove(id)) return {DomainStatus::Unavailable,{}};
    const auto it=state_.actors.find(id);
    ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});ports_.areas.removeRoomUnit(id);state_.actors.erase(it);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::advanceMode(EntityId only,TickContext tick) {
    bool blocked = false;
    for (auto entry=state_.actors.find(only);entry!=state_.actors.end() && entry->first==only;++entry) {
        auto &[id,actor]=*entry;
        if(actor.modeFrame==tick.tick) continue;
        if(actor.delayedDeathSource && !ports_.effects.unitStates(id,tick.tick).contains(actor.rule.uninterruptableState)) {
            const auto result=damage(id,actor.killer,0,tick.tick,0,false,0,{},true,0,{},0,false,0,0,true,{},false,{*actor.delayedDeathSource});
            if(!result) {blocked=true;continue;}
        }
        if(actor.alive() && !actor.questNpc && !actor.skillMotion && actor.knockedUntil<=tick.tick && actor.stunnedUntil<=tick.tick &&
           tick.tick>=actor.busyUntil && !frozen(id,tick.tick) && !actor.route.empty() && actor.pathStep.changedCell() &&
           ports_.areas.loaded(actor.area,actor.position+Vec{.5f,.5f})) {
            const auto result=ports_.effects.movementSkills(id,tick.tick,actor.pathStep.direction(),actor.pathStep.nativePosition());
            if(!result) {blocked=true;continue;}
        }
        actor.modeFrame=tick.tick;
        if (!ports_.areas.loaded(actor.area, actor.position+Vec{.5f,.5f})) {
            actor.moving = false; actor.route.clear(); actor.movementTarget = {};
            continue;
        }
        if(!actor.nativeAiResetTracked) {
            if(actor.curseControl.ai!=CurseAi::None && actor.curseControl.until<=tick.tick) actor.curseControl={};
            if(actor.attractionUntil<=tick.tick) actor.attractionTarget={};
        }
        (void)id; if(!actor.questNpc) actor.moving = false;
        if(actor.conversion && (actor.life<=0 || !actor.owner || !ports_.players.find(*actor.owner) || !ports_.players.find(*actor.owner)->entered)) {
            const auto result=ports_.effects.endConversion(id,tick.tick);
            if(!result) {blocked=true;continue;}
        }
        if(actor.knockbackGoal && actor.life>0) {
            const auto &grid=ports_.areas.at(actor.area).definition.collision;
            const Vec delta=*actor.knockbackGoal-actor.position;
            const Vec next=actor.position+delta.unit()*std::min(delta.length(),25.f*TickContext::seconds);
            if(grid.segment(actor.position,next,{},actor.rule.collision)) {actor.position=next;ports_.areas.moveRoomUnit(id,actor.area,actor.position);}
            else actor.knockbackGoal=actor.position;
            ++actor.revision;
            if(tick.tick<actor.knockedUntil) continue;
            actor.knockbackGoal.reset();actor.knockedUntil=0;
        }
        if(actor.holyFreezeUntil && actor.holyFreezeUntil<=tick.tick) actor.holyFreezeUntil=0;
        if(actor.questNpc) continue; // Quest movement consumes this same actor in npc::System.
        if(actor.skillMotion && advanceSkillMotion(actor,tick.tick)) continue;
        if (frozen(id,tick.tick) || actor.stunnedUntil>tick.tick) continue;
        if (actor.life <= 0 || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const auto target=actor.movementTarget ? nativePathTarget(ports_.players,*this,actor.movementTarget,actor.area) : std::optional(std::pair{actor.movementGoal, 0});
        if(!target) {stop(id);continue;}
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        auto arrived=[&](Vec position) {
            // Coordinate paths finish by consuming their last precise point,
            // not by an extra floating stopping radius.
            if (!actor.movementTarget) return false;
            return meleeDistance(position,actor.rule.size,target->first,target->second)<=actor.stopDistance &&
                (actor.pathSteps || grid.segment(position,target->first));
        };
        if (arrived(actor.position)) { stop(id); continue; }
        const int speed = movementPercent(actor,tick.tick);
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, speed) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance == 0) { actor.route.pop_front(); continue; }
            constexpr Vec center{.5f,.5f};
            const auto previousStep=actor.pathStep;
            const auto step=actor.pathStep.advance(grid,actor.position+center,actor.route.front()+center,
                remaining,(actor.airborne || actor.submerged)?MovementCollisionRule{0,1}:actor.rule.collision);
            const Vec next=step.position-center;
            const bool atGoal=arrived(next);
            bool barrier=false;
            if(!actor.airborne && !actor.submerged) for(const auto &[other,body]:state_.actors) if(other!=id && body.life>0 && body.area==actor.area && body.implementation==MonsterKind::BoneWall && missileUnitIntersection(actor.position,next,actor.rule.size,body.position,body.rule.size)) {barrier=true;break;}
            if(barrier) {actor.route.clear();actor.moving=false;break;}
            const auto previous=actor.position;actor.position=next;
            if(!recacheHirelingRoom(actor)) {actor.position=previous;actor.pathStep=previousStep;break;}
            ports_.areas.moveRoomUnit(id,actor.area,actor.position);
            actor.moving=actor.moving || step.travelled>0;++actor.revision;
            if(step.blocked) {actor.route.clear();break;}
            if (atGoal) { actor.route.clear(); break; }
            if(!step.reached) break;
            remaining-=step.travelled;actor.route.pop_front();
            if(step.travelled>0) break; // D2Common_10226: one nonzero path point per frame.
        }
        // Path exhaustion/collision returns FALSE from D2Common_10226 and
        // leaves WALK/RUN in this frame; do not publish a stale chase target.
        if(actor.route.empty()) {actor.moving=false;actor.running=false;}
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
