#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include "core/random.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/life_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/attributes/pd2/map_values.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <map>
#include <vector>
#include <span>
namespace d2x::server::monsters {
DomainResult<> System::attackProfile(EntityId id,std::shared_ptr<const StatList> mode,uint64_t expected,uint64_t next) {
    auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.nativeStats) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.combatRandom!=expected) return {DomainStatus::Stale,{}};
    if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto stats=attributes::withUnitModeStats(*unit.nativeStats,*unit.rule.stats,std::move(mode));
    unit.nativeStats=std::move(stats);unit.combatRandom=next;++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::toggleAiState(EntityId id,int state,bool active,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || state<0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;if(actor.aiStates.contains(state)==active) return {DomainStatus::Applied,std::monostate{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,state,active}}});
    if(!sent) return {sent.status,{}};
    if(active) actor.aiStates.insert(state);else actor.aiStates.erase(state);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::pd2PhasePresence(EntityId id,bool active) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    // Current 3A4C60/80 bind retail Game 7BE10/89730: clear/restore the
    // three target flags and NO_PATH, retaining the MONSTER footprint.
    unit.targetFlagsDisabled=unit.pd2NoPathDisabled=!active;
    refreshCollision(unit);++unit.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::curseControl(EntityId id,Actor::CurseControl control) {
    if(auto found=state_.actors.find(id);found!=state_.actors.end()) {
        auto &actor=found->second;
        const auto function=[](CurseAi ai) {return ai==CurseAi::DimVision || ai==CurseAi::Terror || ai==CurseAi::Taunt?ai:CurseAi::None;};
        const bool changed=function(actor.curseControl.ai)!=function(control.ai);
        if(changed) {++actor.aiFunctionRevision;actor.aiThinkAt=0;actor.whippedAi=false;}
        actor.curseControl=std::move(control);++actor.revision;
        if(changed) scheduleAi(id,0);
    }
}
void System::refreshNecro(EntityId id,MonsterRule rule,UnitCombatStats stats,std::shared_ptr<const NecroPetSpec> pet,std::optional<WeaponDamage> weapon,
    std::optional<NativeUnitStats> intrinsic,uint64_t tick) {
    if(auto it=state_.actors.find(id);it!=state_.actors.end() && it->second.necroSummon && it->second.alive() && it->second.life>0) {
        auto &body=it->second;
        std::optional<LifeChange> life;
        if(intrinsic) {
            if(!rule.stats || !intrinsic->sources) throw std::runtime_error("Missing native summon refresh sources");
            auto root=std::make_shared<StatList>(*statListWithOwner(*intrinsic->sources,id));
            root->fullValues.reset();
            if(body.nativeStats && body.nativeStats->sources) for(const auto &source:body.nativeStats->sources->contributions)
                if(source && source->source.kind==StatSourceKind::Mode) root->contributions.push_back(source);
            intrinsic->sources=std::move(root);intrinsic->values=evaluateStatList(*rule.stats,*intrinsic->sources,{intrinsic->sources.get(),{}, {}});
            auto candidate=body;candidate.rule=rule;candidate.nativeStats=std::move(intrinsic);
            attributes::MonsterResourceTrace resources(candidate,tick);resources.sampleSources(ports_.effects.unitStatSources(id,tick));life=resources.take();
            life->changed=true;
            if(!canCommitLife(*life)) throw std::logic_error("Native summon refresh lost its prepared revision");
        }
        body.rule=std::move(rule);body.rule.statGameType=ports_.settings.ladderMonsterStats();body.petStats=std::move(stats);body.necroPet=std::move(pet);body.petWeapon=std::move(weapon);
        if(life) {
            if(!commitLife(std::move(*life))) throw std::logic_error("Native summon refresh resource commit lost its prepared revision");
            body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
            body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
            body.petStats.attributes.combat=nativeCombatModifiers(body.nativeStats->values,*body.rule.stats,body.petStats.attributes.combat);
        } else {
            body.maximumLife=int64_t(std::max(1,body.petStats.attributes.maxLife))*256;body.life=std::min(body.life,body.maximumLife);++body.revision;
        }
    }
}
DomainResult<std::map<EntityId,Actor>> System::prepareNecro(const ActorContext &owner,const MonsterRule &rule,Vec position,const SummonCastSpec &summon,uint64_t random,size_t offset,std::span<const std::shared_ptr<const StatList>> equipmentSources,std::optional<MonsterComponents> components,std::optional<bool> shield) const {
    if(offset>=65536-state_.actors.size() || offset>=UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    const auto *p=ports_.players.find(owner.player);if(!p) return {DomainStatus::InvalidActor,{}};
    Actor actor;actor.id=EntityId{ports_.ids.cursor()+offset};actor.area=owner.area;actor.position=actor.home=position;actor.revision=1;actor.owner=owner.player;
    actor.admissionTick=owner.tick;actor.nativeMinionOwner=owner.actor;
    actor.identity.monster=summon.monster;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
    actor.implementation=summon.kind;actor.rule=rule;actor.rule.petType=summon.petType;actor.necroSummon=true;actor.necroPet=summon.necro;actor.petStats=summon.stats;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.rule.level=summon.stats.level;actor.rule.difficulty=p->persistent.difficulty;
    actor.rule.coldDivisor=p->rules.skills->coldDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.freezeDivisor=p->rules.skills->freezeDivisor.at(size_t(actor.rule.difficulty));
    const auto &a=summon.stats.attributes;
    actor.rule.defense=a.defense;actor.rule.attackRating=a.attackRating;
    actor.rule.minimumDamage=int(summon.stats.minimumDamage);actor.rule.maximumDamage=int(summon.stats.maximumDamage);
    actor.rule.criticalChance=summon.stats.critical;actor.rule.blockChance=summon.stats.block;actor.rule.damageRegen=summon.stats.damageRegen;
    actor.rule.resistances={a.combat.physicalResist,a.combat.magicResist,a.fireResist,a.lightningResist,a.coldResist,a.poisonResist};
    actor.life=actor.maximumLife=int64_t(std::max(1,a.maxLife))*256;actor.rewardComplete=true;actor.combatRandom=random;
    actor.components=components?*components:chooseMonsterComponents(rule.componentCounts,{},random);
    actor.shield=shield?*shield:summon.shieldChance>0 && int(limitedRandom(random,100))<summon.shieldChance;
    if(summon.kind==MonsterKind::NecroSkeleton || (summon.necro && summon.necro->kind==NecroSummonKind::Mage)) {
        int appearanceLevel=1;
        const auto states=ports_.effects.unitStateStats(owner.actor,owner.tick);
        if(const auto state=states.find(p->rules.skills->itemFullSet.id);state!=states.end()) {
            appearanceLevel=0;
            const StatKey levelStat{p->rules.skills->stats->names.at("modifierlist_level"),0};
            for(const auto &stat:state->second) if(stat.key==levelStat) {appearanceLevel=stat.value;break;}
        } else if(p->totals.statSources) {
            const StatKey levelStat{p->rules.skills->stats->names.at("modifierlist_level"),0};
            for(auto source=p->totals.statSources->contributions.rbegin();source!=p->totals.statSources->contributions.rend();++source) {
                if(!*source || (*source)->source.kind!=StatSourceKind::Passive) continue;
                const auto skill=p->rules.skills->formulas->skills.find((*source)->source.definition);
                if(skill==p->rules.skills->formulas->skills.end() || skill->second.passiveState!=p->rules.skills->itemFullSet.id) continue;
                const auto rank=p->totals.skillRanks.find(skill->first);
                if(rank==p->totals.skillRanks.end() || rank->second<=0 ||
                    (skill->second.suppressionState>0 && states.contains(skill->second.suppressionState))) continue;
                appearanceLevel=baseStatValue(*p->rules.skills->stats,**source,levelStat);break;
            }
        }
        if(appearanceLevel<0) return {DomainStatus::InvalidRequest,{}};
        applyNecroSkeletonComponents(actor.components,appearanceLevel,actor.shield,
            rule.petVariant>=0?std::optional{uint8_t(rule.petVariant)}:std::nullopt);
    }
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    actor.combatRandom=random;
    for(auto &[mode,attack]:actor.rule.attacks) { (void)mode;attack.minimum=actor.rule.minimumDamage;attack.maximum=actor.rule.maximumDamage;attack.rating=actor.rule.attackRating; }
    if(summon.native) {
        StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};
        binding.contributions.assign(equipmentSources.begin(),equipmentSources.end());
        const auto bound=statListWithOwner(binding,actor.id);
        actor.rule.alignment=2;actor.nativeStats=attributes::prepareSummonStatSources(actor,summon,bound->contributions,ports_.areas.statTime(actor.area));
        actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
        actor.petStats.attributes.maxLife=std::max(1,actor.petStats.attributes.maxLifeFixed.raw()>>8);
        actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,actor.petStats.attributes.combat);
        actor.rule.resistances={summon.native->base.attributes.combat.physicalResist,summon.native->base.attributes.combat.magicResist,
            summon.native->base.attributes.fireResist,summon.native->base.attributes.lightningResist,summon.native->base.attributes.coldResist,summon.native->base.attributes.poisonResist};
    }
    if(summon.kind==MonsterKind::BoneWall) {
        actor.nativeMinionOwner=owner.actor;actor.killMinionsOnDeath=true;
        actor.riseMode=rule.spawnMode;
        actor.aiPose=rule.spawnMode;actor.busyUntil=actor.riseUntil=actor.poseUntil=owner.tick+uint64_t(rule.summonBirthTicks);
    }
    if(components) actor.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
        {{},{FrameEventUnit::Monster,actor.id},FrameEventType::AiThink,owner.tick+25}));
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);prepareInitialTimers(actor,owner.tick);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
DomainResult<std::map<EntityId,Actor>> System::prepareHireling(const ActorContext &owner,const MonsterRule &rule,std::string_view code,Vec position,int64_t life,uint64_t random,NativeUnitStats intrinsic,int birthLevel,NativeBirthBatch &birth,std::span<const EntityId> retired) const {
    const auto *p=ports_.players.find(owner.player);const auto *area=ports_.areas.find(owner.area);
    if(!p || !p->entered || p->actor!=owner.actor || p->area!=owner.area || !area || area->generation!=owner.areaGeneration || life<=0 || rule.minimumLife<=0 || !area->definition.collision.walkable(position,rule.collision)) return {DomainStatus::InvalidActor,{}};
    std::set<EntityId> removals;
    for(const auto id:retired) if(!removals.insert(id).second || !canRemove(id)) return {DomainStatus::InvalidRequest,{}};
    if(state_.actors.size()-removals.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    Actor actor;actor.id=EntityId{ports_.ids.cursor()};actor.area=owner.area;actor.position=actor.home=position;actor.revision=1;actor.owner=owner.player;
    actor.identity.monster=std::string(code);actor.identity.spawnKey="hireling."+std::to_string(owner.player.value);actor.identity.origin=SpawnOrigin::Summoned;
    if(!rule.stats || !intrinsic.sources || !rule.lifeTables[0] || birthLevel<0) return {DomainStatus::InvalidRequest,{}};
    const auto maximum=nativeStatValue(intrinsic.values,*rule.stats,"maxhp");
    if(maximum<=0) return {DomainStatus::InvalidRequest,{}};
    intrinsic.sources=statListWithOwner(*intrinsic.sources,actor.id);actor.nativeStats=std::move(intrinsic);
    actor.rule=rule;actor.hireling=true;actor.life=std::min(life,int64_t(maximum));actor.maximumLife=maximum;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.petStats.damageRegen=rule.damageRegen;
    actor.admissionTick=owner.tick;actor.nativeMinionOwner=owner.actor;
    actor.rewardComplete=true;actor.combatRandom=random;
    birth.palettes=prepareComponents(actor);
    const auto bounds=monsterLifeBounds(*rule.lifeTables[0],birthLevel,0,actor.rule.statGameType);
    limitedRandom(actor.combatRandom,uint32_t(nativeAdd(nativeSubtract(bounds.maximum,bounds.minimum),1)));
    for(const size_t component:{size_t{0},size_t{1},size_t{5},size_t{6},size_t{7}}) actor.components[component]=0;
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    const bool clients=std::any_of(ports_.players.all().begin(),ports_.players.all().end(),[&](const auto &entry) {
        const auto &player=entry.second;
        return player.entered && ports_.areas.nearby(player.area,player.position+Vec{.5f,.5f},actor.area,actor.position);
    });
    actor.aiThinkAt=owner.tick+(clients?2:uint64_t(rule.neutralDelayTicks>0?rule.neutralDelayTicks:15));
    actor.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
        {{},{FrameEventUnit::Monster,actor.id},FrameEventType::AiThink,actor.aiThinkAt}));
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);prepareInitialTimers(actor,owner.tick);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
DomainResult<std::map<EntityId,Actor>> System::prepareHydra(const ActorContext &owner,const HydraRules &spec,const SummonCastSpec &summon,const std::array<std::optional<Vec>,3> &positions,uint64_t &random,std::optional<std::array<RegionId,3>> destinationAreas) const {
    const auto *p=ports_.players.findActor(owner.actor);const auto *caster=find(owner.actor);const auto *area=ports_.areas.find(owner.area);
    const bool valid=p?p->player==owner.player && p->entered && p->area==owner.area && p->persistent.player.hp>0:
        caster && caster->alive() && caster->area==owner.area;
    if(!valid || !area ||
        area->generation!=owner.areaGeneration || area->definition.town) return {DomainStatus::InvalidActor,{}};
    const int difficulty=p?int(p->persistent.difficulty):caster->rule.difficulty;
    if(state_.actors.size()>65536-3 || ports_.ids.cursor()>UINT32_MAX-3) return {DomainStatus::Capacity,{}};
    if(!summon.native || spec.defenseRatios.empty()) return {DomainStatus::Unavailable,{}};
    std::map<EntityId,Actor> prepared; auto cursor=ports_.ids.cursor();
    constexpr MonsterKind kinds[]{MonsterKind::Hydra1,MonsterKind::Hydra2,MonsterKind::Hydra3};
    for(size_t i=0;i<spec.heads.size();++i) {
        const auto &head=spec.heads[i];const auto &definition=head.definition;
        if(!positions[i]) continue;
        const Vec position=*positions[i];
        Actor actor;actor.id=EntityId{cursor++};actor.area=destinationAreas?destinationAreas->at(i):owner.area;actor.position=position;actor.revision=1;
        if(p) actor.owner=p->player;
        actor.admissionTick=owner.tick;actor.nativeMinionOwner=owner.actor;
        actor.identity.monster=definition.code;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
        actor.implementation=kinds[i];actor.rule=head.rules.at(size_t(difficulty));
        actor.rule.ai.kind=MonsterAiKind::Hydra;
        actor.rule.alignment=p?int(UnitAlignment::Good):int(caster->alignment());
        actor.rule.statGameType=ports_.settings.ladderMonsterStats();actor.combatRandom=childRandom(random);
        actor.components=chooseMonsterComponents(actor.rule.componentCounts,{},actor.combatRandom);
        auto preparedSummon=summon;auto &program=*preparedSummon.native;program.base=head.base.at(size_t(actor.rule.difficulty));
        // Current content is the LoD profile. MONSTERS_Initialize replaces
        // non-boss ratio-unit levels with the expansion area level in N/H.
        if(actor.rule.difficulty && !head.noRatio && !head.boss)
            program.base.level=spec.areaLevels.at(int(actor.area)).at(size_t(actor.rule.difficulty));
        const auto level=std::clamp(program.base.level,0,int(spec.defenseRatios.size()-1));
        if(!head.noRatio) program.base.attributes.defense=nativePercentage(
            spec.defenseRatios.at(size_t(level)).at(size_t(actor.rule.difficulty+(actor.rule.statGameType?3:0))),program.base.attributes.defense,100);
        const auto &life=*actor.rule.lifeTables.at(size_t(actor.rule.difficulty));
        const auto bounds=monsterLifeBounds(life,program.base.level,actor.rule.difficulty,actor.rule.statGameType);
        const auto whole=nativeAdd(bounds.minimum,int32_t(limitedRandom(actor.combatRandom,uint32_t(nativeAdd(nativeSubtract(bounds.maximum,bounds.minimum),1)))));
        program.initialLife=Fixed8::fromRaw(nativeShiftLeft(std::min(whole,(1<<23)-1),8));
        preparedSummon.stats=program.base;actor.petStats=program.base;actor.rule.level=program.base.level;
        actor.nativeStats=attributes::prepareSummonStatSources(actor,preparedSummon,{},ports_.areas.statTime(actor.area));
        if(!p) {
            attributes::MonsterResourceTrace resources(actor,owner.tick);
            resources.writeBaseStat("alignment",int(caster->alignment()),{});
            actor.nativeStats=resources.take().intrinsic;
        }
        actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
        actor.petStats.attributes.maxLife=actor.petStats.attributes.maxLifeFixed.raw()>>8;
        actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,{});
        actor.modeAlive=true;actor.rewardComplete=true;
        actor.riseUntil=actor.busyUntil=owner.tick+uint64_t(definition.riseTicks);
        actor.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,actor.id},FrameEventType::AiThink,owner.tick+25}));
        actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);
        prepareInitialTimers(actor,owner.tick);prepared.emplace(actor.id,std::move(actor));
    }
    if(prepared.empty()) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,std::move(prepared)};
}
ActorPublication System::prepareActorPublication(std::map<EntityId,Actor> actors,size_t allocatedIds,std::span<const EntityId> restored,std::shared_ptr<EntityIds::Publication> identities,std::span<const EntityId> retired,std::span<const EntityId> presets) const {
    ActorPublication result;result.cursor=ports_.ids.cursor();result.allocatedIds=allocatedIds;
    result.identities=identities?std::move(identities):allocatedIds?ports_.ids.preparePublication(allocatedIds):nullptr;
    result.restoredIdentities.insert(restored.begin(),restored.end());
    result.presetIdentities.insert(presets.begin(),presets.end());
    result.actors=std::move(actors);
    for(const auto id:retired) {
        const auto *live=find(id);
        if(!live || !canRemove(id) || !result.removals.emplace(id,live->revision).second)
            throw std::invalid_argument("Invalid staged actor retirement");
    }
    for(auto &[id,candidate]:result.actors) {
        const auto bind=[&](EntityId entity,NativeUnitType type) {
            if(ports_.ids.hasNative(entity,type)) return;
            if(!result.identities) result.identities=ports_.ids.preparePublication(allocatedIds);
            result.identities->bind(entity,type);
        };
        bind(id,NativeUnitType::Monster);
        if(candidate.birthEquipmentFirst && result.identities && result.identities->covers(EntityId{candidate.birthEquipmentFirst}))
            for(size_t index=0;index<candidate.birthEquipmentItems;++index)
                bind(EntityId{candidate.birthEquipmentFirst+index},NativeUnitType::Item);
        if(candidate.equipment) {
            const auto bindItem=[&](auto &&self,const ItemInstance &item)->void {
                bind(item.id,NativeUnitType::Item);
                for(const auto &child:item.socketedItems) self(self,child);
            };
            for(const auto &[itemId,item]:candidate.equipment->inventory.items) {
                (void)itemId;bindItem(bindItem,item);
            }
        }
        if(candidate.birthUniquesExpected) {
            if(result.uniquesExpected && result.uniquesExpected!=candidate.birthUniquesExpected)
                throw std::invalid_argument("Conflicting native birth unique snapshots");
            result.uniquesExpected=candidate.birthUniquesExpected;
            if(!result.uniquesNext) result.uniquesNext=*candidate.birthUniquesExpected;
            for(const auto row:candidate.birthLimitedUniques)
                if(!result.uniquesNext->insert(row).second) throw std::invalid_argument("Conflicting native birth limited unique");
        }
        const auto *live=find(id);
        ports_.effects.prepareMonsterModifierEvents(candidate,live);
        if(live) result.expected.emplace(id,live->revision);
        if(!candidate.initialWakeTimer && std::find(candidate.rule.nativeModifiers.begin(),candidate.rule.nativeModifiers.end(),41)!=candidate.rule.nativeModifiers.end() &&
            (!live || std::find(live->rule.nativeModifiers.begin(),live->rule.nativeModifiers.end(),41)==live->rule.nativeModifiers.end()))
            candidate.initialWakeTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,ports_.frameEvents.currentFrame()+75,false,0,41}));
        if(!live || (!live->alive() && candidate.alive())) prepareInitialTimers(candidate,ports_.frameEvents.currentFrame());
        if(candidate.nativeAi() &&
           (!live || candidate.alive()!=live->alive()) && (candidate.alive() || candidate.owner) && !candidate.initialAiTimer)
            candidate.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,id},FrameEventType::AiThink,ports_.frameEvents.currentFrame()+1}));
        if(candidate.nativeAi() && (!live || (!live->alive() && candidate.alive())) && !candidate.initialModeTimer)
            candidate.initialModeTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,id},FrameEventType::ModeChange,ports_.frameEvents.currentFrame()+1,true}));
    }
    for(auto &[id,candidate]:result.actors) if(!find(id) && candidate.owner && candidate.nativeStats)
        if(const auto *area=ports_.areas.find(candidate.area);area && area->definition.pd2Map)
            attributes::preparePd2MapMonster(candidate,*area->definition.pd2Map,ports_.frameEvents.currentFrame(),!candidate.alive(),ports_.travel.read().pd2SceneParticipants,area->definition.pd2RuntimeGlobal);
    auto birthEffects=ports_.effects.prepareBirthEffects(result.actors);
    if(!birthEffects.units.empty()) result.birthEffects=std::make_shared<effects::BirthEffectsPublication>(std::move(birthEffects));
    if(!validateActors(result)) throw std::invalid_argument("Invalid staged actor publication");
    return result;
}
bool System::validateActors(const ActorPublication &plan) const {
    if(plan.birthEffects && !ports_.effects.validateBirthEffects(*plan.birthEffects)) return false;
    if(plan.uniquesExpected && *plan.uniquesExpected!=ports_.loot.read().uniques) return false;
    if(plan.allocatedIds && (plan.cursor>UINT32_MAX || plan.allocatedIds>UINT32_MAX-plan.cursor)) return false;
    if(plan.identities && (!ports_.ids.validate(*plan.identities) || plan.identities->first()!=plan.cursor ||
        plan.identities->next()-plan.cursor!=plan.allocatedIds)) return false;
    if(plan.allocatedIds && !plan.identities) return false;
    for(const auto &[id,revision]:plan.removals) {
        const auto *live=find(id);
        if(!live || live->revision!=revision || plan.actors.contains(id) || !canRemove(id)) return false;
    }
    size_t additions=0,registrations=0,wanderRegistrations=0;
    for(const auto &[id,candidate]:plan.actors) {
        if(candidate.id!=id || !ports_.areas.find(candidate.area)) return false;
        if(candidate.owner && !candidate.conversion && candidate.rule.petType>0 && !candidate.petRegistrationOrder) ++registrations;
        const auto expected=plan.expected.find(id);const auto *live=find(id);
        if(expected!=plan.expected.end()) {
            if(!live || live->revision!=expected->second || candidate.area!=live->area || (candidate.position.x!=live->position.x || candidate.position.y!=live->position.y) ||
               expected->second==UINT64_MAX || candidate.revision<=expected->second) return false;
        } else {
            const bool restored=plan.restoredIdentities.contains(id);
            if(live || !candidate.roomAdmission) return false;
            if(restored) {
                bool saved=false;
                for(const auto &[room,units]:state_.inactive) {
                    (void)room;
                    saved|=std::any_of(units.begin(),units.end(),[&](const auto &unit){return unit.id==id && unit.revision<candidate.revision;});
                }
                if(!saved || id.value>=ports_.ids.cursor()) return false;
            } else if(plan.presetIdentities.contains(id)) {
                const auto &npcs=ports_.areas.at(candidate.area).definition.npcs;
                if(id.value>=ports_.ids.cursor() || !candidate.questNpc || candidate.identity.origin!=SpawnOrigin::Preset ||
                    std::none_of(npcs.begin(),npcs.end(),[&](const auto &npc){return npc.id==id && npc.rule.code==candidate.identity.monster;})) return false;
            } else if(id.value<plan.cursor || id.value-plan.cursor>=plan.allocatedIds) return false;
            if(!ports_.areas.validateRoomUnit(*candidate.roomAdmission,id)) return false;
            if(candidate.wanderTargetOrder) ++wanderRegistrations;
            ++additions;
        }
    }
    return additions<=65536 && additions<=plan.allocatedIds+plan.restoredIdentities.size()+plan.presetIdentities.size() && state_.actors.size()-plan.removals.size()<=65536-additions && registrations<UINT64_MAX-state_.nextPetRegistrationOrder &&
        wanderRegistrations<=UINT64_MAX-state_.nextWanderTargetOrder;
}
void System::publishActors(ActorPublication &plan) noexcept {
    if(!validateActors(plan)) std::terminate();
    if(plan.identities) ports_.ids.publish(*plan.identities);
    for(const auto &[id,revision]:plan.removals) {(void)revision;if(!remove(id)) std::terminate();}
    while(!plan.actors.empty()) {
        auto node=plan.actors.extract(plan.actors.begin());registerPet(node.mapped());const auto live=state_.actors.find(node.key());
        if(live==state_.actors.end() && node.mapped().wanderTargetOrder) node.mapped().wanderTargetOrder=state_.nextWanderTargetOrder++;
        node.mapped().birthUniquesExpected.reset();node.mapped().birthLimitedUniques.clear();
        node.mapped().initialEffects.reset();
        publishInitialAi(node.mapped());
        if(!node.mapped().alive() && (!node.mapped().owner || node.mapped().conversion))
            ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,node.key()},FrameEventType::AiThink,0);
        if(live!=state_.actors.end()) {
            if(live->second.alive() && !node.mapped().alive()) ports_.areas.recordMonsterDeath(node.key());
            live->second=std::move(node.mapped());
            refreshCollision(live->second);
        }
        else {ports_.areas.publishRoomUnit(*node.mapped().roomAdmission);const auto inserted=state_.actors.insert(std::move(node));refreshCollision(inserted.position->second);}
    }
    if(plan.birthEffects) ports_.effects.publishBirthEffects(*plan.birthEffects);
    std::erase_if(birthEquipment_,[&](const auto &entry){return state_.actors.contains(entry.second.first.actor);});
    if(plan.uniquesNext) ports_.loot.commitUniques(std::move(*plan.uniquesNext));
}
DomainResult<std::map<EntityId,Actor>> System::prepareNativePet(const ActorContext &owner,const MonsterRule &rule,Vec position,const SummonCastSpec &summon,std::shared_ptr<PersistentCharacter> equipment,size_t items,std::optional<WeaponDamage> weapon,std::span<const std::shared_ptr<const StatList>> sources,uint64_t random,std::optional<EquipmentStats> equipmentStats) const {
    if(state_.actors.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX || items>=UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    const auto *p=ports_.players.find(owner.player);if(!p) return {DomainStatus::InvalidActor,{}};
    if(!summon.native || !rule.stats || !rule.formulas) return {DomainStatus::Unavailable,{}};
    std::set<EntityId> originalIds;
    const auto inspect=[&](auto &&self,const ItemInstance &item)->bool {
        if(!item.id || !originalIds.insert(item.id).second || originalIds.size()>items) return false;
        for(const auto &child:item.socketedItems) if(!self(self,child)) return false;
        return true;
    };
    if(equipment) for(const auto &[id,item]:equipment->inventory.items)
        if(id!=item.id || !inspect(inspect,item)) return {DomainStatus::InvalidRequest,{}};
    if(originalIds.size()!=items) return {DomainStatus::InvalidRequest,{}};
    uint64_t cursor=ports_.ids.cursor();Actor actor;actor.id=EntityId{cursor++};actor.area=owner.area;actor.position=position;actor.revision=1;actor.owner=owner.player;
    actor.admissionTick=owner.tick;
    actor.identity.monster=summon.monster;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
    actor.implementation=summon.kind;actor.rule=rule;actor.rule.petType=summon.petType;actor.amazonPet=summon.amazon;actor.assassinPet=summon.assassin;actor.druidPet=summon.druid;actor.petStats=summon.stats;
    actor.nativeMinionOwner=p->actor;
    actor.rule.alignment=2;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.rule.difficulty=p->persistent.difficulty;
    actor.rule.coldDivisor=p->rules.skills->coldDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.freezeDivisor=p->rules.skills->freezeDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.level=summon.stats.level;actor.rule.defense=summon.stats.attributes.defense;actor.rule.attackRating=summon.stats.attributes.attackRating;if(summon.amazon && summon.amazon->decoy) actor.rule.deathTicks=p->rules.character->deathTicks;
    const auto &a=summon.stats.attributes;
    actor.rule.resistances={a.combat.physicalResist,a.combat.magicResist,a.fireResist,a.lightningResist,a.coldResist,a.poisonResist};
    actor.life=actor.maximumLife=a.maxLifeFixed.raw();actor.rewardComplete=true;actor.combatRandom=random;
    std::map<EntityId,EntityId> itemIds;
    auto remap=[&](auto &&self,ItemInstance &item)->void {const auto old=item.id;item.id=EntityId{cursor++};itemIds.emplace(old,item.id);
        for(size_t index=0;index<item.socketedItems.size();++index) {auto &child=item.socketedItems[index];child.location=SocketLocation{item.id,unsigned(index)};self(self,child);}};
    std::map<EntityId,ItemInstance> mapped;
    const auto order=equipment?inventoryItemOrder(equipment->inventory):std::vector<EntityId>{};
    if(equipment) for(const auto id:order) {auto &item=equipment->inventory.items.at(id);const auto old=id;remap(remap,item);if(weapon && weapon->item==old) weapon->item=item.id;
        auto *location=std::get_if<ContainerLocation>(&item.location);
        if(!location || location->container!=equipment->containers.equipment) return {DomainStatus::InvalidRequest,{}};
        location->container=actor.id;
        if(const auto modifier=actor.petStats.attributes.combat.weapons.find(old);modifier!=actor.petStats.attributes.combat.weapons.end()) {auto value=modifier->second;actor.petStats.attributes.combat.weapons.erase(modifier);actor.petStats.attributes.combat.weapons.emplace(item.id,std::move(value));}mapped.emplace(item.id,std::move(item));}
    if(cursor-actor.id.value-1!=items) return {DomainStatus::InvalidRequest,{}};
    if(equipment) {
        auto container=equipment->inventory.containers.at(equipment->containers.equipment);
        container.id=actor.id;container.spec.owner=actor.id;
        if(container.registeredWeapon) container.registeredWeapon=itemIds.at(container.registeredWeapon);
        equipment->inventory.items.swap(mapped);equipment->inventory.itemOrder.clear();
        for(const auto id:order) equipment->inventory.itemOrder.push_back(itemIds.at(id));
        equipment->player.id=actor.id;equipment->containers.equipment=actor.id;
        equipment->inventory.containers.clear();equipment->inventory.containers.emplace(actor.id,std::move(container));
    }
    if(equipmentStats) {
        if(equipmentStats->registeredWeapon && *equipmentStats->registeredWeapon)
            equipmentStats->registeredWeapon=itemIds.at(*equipmentStats->registeredWeapon);
        for(auto &hand:equipmentStats->weapons) if(hand.item) hand.item=itemIds.at(hand.item);
        if(equipmentStats->shield) equipmentStats->shield=itemIds.at(equipmentStats->shield);
        actor.equipmentStats=std::move(equipmentStats);
    }
    actor.petWeapon=std::move(weapon);actor.equipment=std::move(equipment);
    StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};
    binding.contributions.assign(sources.begin(),sources.end());
    const auto bound=statListWithOwner(binding,actor.id,itemIds);
    actor.nativeStats=attributes::prepareSummonStatSources(actor,summon,bound->contributions,ports_.areas.statTime(actor.area));
    actor.rule.level=nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"level");
    actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
    actor.petStats.attributes.maxLife=std::max(1,actor.petStats.attributes.maxLifeFixed.raw()>>8);
    actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,actor.petStats.attributes.combat);
    actor.rule.resistances={summon.native->base.attributes.combat.physicalResist,summon.native->base.attributes.combat.magicResist,
        summon.native->base.attributes.fireResist,summon.native->base.attributes.lightningResist,summon.native->base.attributes.coldResist,summon.native->base.attributes.poisonResist};
    const uint64_t aiDelay=summon.assassin && summon.assassin->definition->ai==AssassinPetDefinition::Ai::BladeCreeper?1:
        (summon.amazon && !summon.amazon->decoy) || (summon.assassin && summon.assassin->definition->warp)?20:25;
    actor.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
        {{},{FrameEventUnit::Monster,actor.id},FrameEventType::AiThink,owner.tick+aiDelay}));
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);prepareInitialTimers(actor,owner.tick);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
DomainResult<> System::warpPet(EntityId id,const ActorContext &owner,Vec position) {
    const auto found=state_.actors.find(id);const auto *area=ports_.areas.find(owner.area);
    if(found==state_.actors.end() || (!(found->second.amazonPet && found->second.amazonPet->warp) && !(found->second.assassinPet && found->second.assassinPet->definition->warp) && !found->second.druidPet && !found->second.necroSummon && !found->second.hireling) || found->second.owner!=owner.player || !area || area->generation!=owner.areaGeneration) return {DomainStatus::InvalidActor,{}};
    auto &pet=found->second;if(!area->definition.collision.walkable(position,pet.rule.collision)) return {DomainStatus::Unavailable,{}};
    for(const auto &[other,body]:state_.actors) if(other!=id && body.alive() && body.area==owner.area && (body.position-position).length()<float((body.rule.size+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}};
    for(const auto &[playerId,player]:ports_.players.all()) { (void)playerId; if(player.entered && player.area==owner.area && player.persistent.player.hp>0 && (player.position-position).length()<float((2+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}}; }
    stop(id);pet.area=owner.area;pet.position=position;ports_.areas.moveRoomUnit(id,pet.area,pet.position);refreshCollision(pet);pet.busyUntil=owner.tick;pet.neutralAt.reset();pet.frozenUntil=pet.chilledUntil=0;++pet.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::stageRetirements(effects::AuraUnitPublication &publication,std::span<const EntityId> ids,uint64_t tick) const {
    for(const auto id:ids) {
        const auto *unit=find(id);if(!unit || !unit->owner) return {DomainStatus::InvalidActor,{}};
        if(publication.actors) if(const auto staged=publication.actors->actors.find(id);staged!=publication.actors->actors.end()) unit=&staged->second;
        if(!unit->alive()) continue;
        auto prepared=prepareDeathActor(*unit,tick);if(!prepared) return {prepared.status,{}};
        // These actors leave the player's active pet list without a hostile
        // kill/treasure award. Their actual state lists still run death callbacks.
        prepared.value->rewardComplete=true;
        ports_.effects.stageDamageStates(publication,std::move(*prepared.value),{},tick);
        publication.facts.emplace_back(HitFact{id,1,unit->area,0,true,unit->position,0,0});
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::retire(EntityId id,uint64_t tick) {
    const auto *unit=find(id);if(!unit || !unit->owner) return {DomainStatus::InvalidActor,{}};
    return scriptDeath(id,tick);
}

}
