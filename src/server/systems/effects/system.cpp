#include "system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/effects/queries.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/systems/attributes/calculation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <exception>
#include "gameplay/skills/rank_bonus.hpp"
namespace d2x::server::effects {
uint64_t System::reserveAuraEventOrder() {
    if(nextAuraEventOrder_==UINT64_MAX) throw std::overflow_error("Aura event order exhausted");
    return nextAuraEventOrder_++;
}
PlayerPoisonCandidate System::preparePlayerPoison(EntityId actor) const {
    const auto current=state_.players.find(actor);
    if(current==state_.players.end()) return {};
    return {current->second.poison,current->second.poisonRevision,false};
}
bool System::validatePlayerEffects(const PlayerEffectsPublication &plan) const {
    if(!ports_.frameEvents.validate(plan.timers)) return false;
    if(!ports_.frameEvents.validate(plan.skillTimers)) return false;
    if(!plan.poison) return true;
    const auto current=state_.players.find(plan.actor);
    const auto revision=current==state_.players.end()?0:current->second.poisonRevision;
    return revision!=UINT64_MAX && (!plan.expectedPoisonRevision || revision==*plan.expectedPoisonRevision);
}
PlayerEffectsPublication System::preparePlayerEffects(const ActorContext &actor,const CombatEffectSet &states,
    const attributes::Totals &totals,const CharacterRecord &record,const RightAuraTimerChange &right) const {
    const auto *player=ports_.players.find(actor.player);
    if(!player || player->actor!=actor.actor) throw std::invalid_argument("Missing prepared effect owner");
    PlayerEffectsPublication result;result.actor=actor.actor;result.states=states;
    result.insertion[actor.actor].states=states;
    result.timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Player,actor.actor},FrameEventType::RemoveState,states.expirationEvents());
    result.skillTimers=prepareStormTimers(actor,*player,states,totals,record,right);
    if(const auto current=state_.players.find(actor.actor);current!=state_.players.end() && current->second.poison &&
       !states.hasState(current->second.poison->damage.state,actor.tick)) {
        result.poison=std::optional<PoisonStatus>{};result.expectedPoisonRevision=current->second.poisonRevision;
    }
    return result;
}
void System::publishPlayerEffects(PlayerEffectsPublication &plan) noexcept {
    ports_.frameEvents.publish(std::move(plan.timers));
    ports_.frameEvents.publish(std::move(plan.skillTimers));
    const auto current=state_.players.find(plan.actor);
    if(current!=state_.players.end()) {
        current->second.states=plan.states;
        if(plan.poison) {current->second.poison=*plan.poison;++current->second.poisonRevision;}
    }
    else {
        auto node=plan.insertion.extract(plan.actor);
        if(node.empty()) std::terminate();
        state_.players.insert(std::move(node));
        if(plan.poison) {auto &published=state_.players.at(plan.actor);published.poison=*plan.poison;++published.poisonRevision;}
    }
}
void System::publishPlayers(std::map<EntityId,Recovery> &candidate) noexcept {
    for(auto &[actor,recovery]:candidate) {
        const auto *player=ports_.players.findActor(actor);
        if(player && player->transient.effects) recovery.states=*player->transient.effects;
        if(const auto current=state_.players.find(actor);current!=state_.players.end() && current->second.poisonRevision>recovery.poisonRevision) {
            recovery.poison=current->second.poison;recovery.poisonRevision=current->second.poisonRevision;
        }
    }
    state_.players.swap(candidate);
}
std::pair<uint8_t,uint8_t> System::potionRecoveryPercent(PlayerId id,uint64_t tick) const {
    const auto *player=ports_.players.find(id);
    if(!player || player->persistent.player.hp<=0 || !player->rules.skills) return {};
    const auto *states=unitEffects(player->actor);if(!states) return {};
    const auto &rules=*player->rules.character->stats;
    const auto percent=[&](int state,Fixed8 current,int maximum,bool mana)->uint8_t {
        if(maximum<=0 || !states->hasState(state,tick)) return 0;
        const auto key=StatKey{rules.names.at(mana?"manarecovery":"hpregen"),0};
        for(const auto &effect:states->entries()) if(effect.spec.state.id==state && effect.spec.nativeValues) {
            const auto value=effect.spec.nativeValues->find(key);
            const auto rate=int64_t(value==effect.spec.nativeValues->end()?0:value->second)+(mana?player->totals.character.manaRegenPerFrame:0);
            const auto frame=effect.nativeExpireFrame?*effect.nativeExpireFrame:effect.expiresAt?nativeNarrow(uint32_t(*effect.expiresAt)):nativeNarrow(uint32_t(tick));
            const auto remaining=std::max(0,nativeSubtract(frame,nativeNarrow(uint32_t(tick))));
            const auto total=std::min(int64_t(maximum)*256,int64_t(current.raw())+std::max<int64_t>(0,rate)*remaining);
            return uint8_t(std::clamp<int64_t>(mana?100*total/(int64_t(maximum)*256):100*(total>>8)/maximum,0,100));
        }
        return 0;
    };
    return {percent(player->rules.skills->healthPotion.id,player->persistent.player.hp,player->totals.character.maxLife,false),
        percent(player->rules.skills->manaPotion.id,player->persistent.player.mana,player->totals.character.maxMana,true)};
}
TransientAttributes projectEffectAttributes(const CombatEffectSet &effects,uint64_t tick,bool includePendingRemoval) {
    TransientAttributes result;result.modifiers=combatEffectModifiers(effects.entries(),tick,includePendingRemoval);
    result.effects=std::make_shared<CombatEffectSet>(effects);
    result.states=effects.stateIds(tick,includePendingRemoval);
    result.sources=combatEffectStatSources(effects.entries(),tick,includePendingRemoval);
    return result;
}
PlayerResourceTrace::PlayerResourceTrace(const PlayerState &player,uint64_t tick,Fixed8 manaCost,std::optional<StatTimeContext> time)
    :player_(player),current_(player.persistent),previous_(player.totals),tick_(tick),time_(time?time:player.totals.time),
     passiveRefresh_(player.transient.effects?player.transient.effects->passiveRefreshEpoch():0),
     staminaRestore_(player.transient.effects?player.transient.effects->staminaRestoreEpoch():0) {current_.player.mana-=manaCost;}
ResourceMaximums PlayerResourceTrace::maximum() const {
    const auto &value=previous_.character;return {value.maxLifeFixed,value.maxManaFixed,value.maxStaminaFixed};
}
void PlayerResourceTrace::seedResources(Fixed8 life,Fixed8 mana,Fixed8 stamina) {
    current_.player.hp=life;current_.player.mana=mana;current_.player.stamina=stamina;
}
void PlayerResourceTrace::inheritPrepared(CharacterRecord record,attributes::Totals totals,const CombatEffectSet &states) {
    current_.player=std::move(record);previous_=std::move(totals);
    passiveRefresh_=states.passiveRefreshEpoch();staminaRestore_=states.staminaRestoreEpoch();maximums_.clear();
}
void PlayerResourceTrace::sample(const CombatEffectSet &states,bool includePendingRemoval) {
    const auto transient=projectEffectAttributes(states,tick_,includePendingRemoval);
    const auto passiveRefresh=passiveRefresh_==states.passiveRefreshEpoch()?attributes::PassiveCacheRefresh::Retain:attributes::PassiveCacheRefresh::Refresh;
    passiveRefresh_=states.passiveRefreshEpoch();
    auto next=attributes::calculate(player_.definition,current_,*player_.rules.items,*player_.rules.equipment,*player_.rules.character,
        {},transient.modifiers,transient.states,transient.statSources(),&previous_,time_,false,{}, {},{}, {},passiveRefresh);
    // CharacterRecord owns current stamina; derived FullStats contains only
    // the posted list's stamina contribution. Propagate its delta before
    // MAXSTAMINA callbacks, as ExpireStatList withdraws stat 10 before 11.
    const StatKey stamina{player_.rules.character->stats->names.at("stamina"),0};
    current_.player.stamina=Fixed8::fromRaw(nativeAdd(current_.player.stamina.raw(),
        nativeSubtract(next.nativeStats.get(stamina),previous_.nativeStats.get(stamina))));
    const auto &before=previous_.character,&after=next.character;
    ResourceMaximums current{before.maxLifeFixed,before.maxManaFixed,before.maxStaminaFixed};
    for(const auto &maximum:next.resourceMaximums) if(maximum!=current) {
        applyResourceMaximums(current_.player.hp,current_.player.mana,current_.player.stamina,current,maximum);
        current=maximum;maximums_.push_back(maximum);
    }
    const ResourceMaximums maximum{after.maxLifeFixed,after.maxManaFixed,after.maxStaminaFixed};
    if(maximum!=current) {
        applyResourceMaximums(current_.player.hp,current_.player.mana,current_.player.stamina,
            current,maximum);
        maximums_.push_back(maximum);
    }
    if(staminaRestore_!=states.staminaRestoreEpoch()) {
        staminaRestore_=states.staminaRestoreEpoch();current_.player.stamina=after.maxStaminaFixed;
    }
    previous_=std::move(next);
}
void PlayerResourceTrace::withdrawPassiveState(int state,const CombatEffectSet &states) {
    if(state<=0 || !previous_.statSources) return;
    auto root=std::make_shared<StatList>(*previous_.statSources);root->fullValues.reset();
    const auto removed=std::erase_if(root->contributions,[&](const auto &source) {
        return source && source->source.kind==StatSourceKind::Passive &&
            player_.rules.character->formulas->skills.at(source->source.definition).passiveState==state;
    });
    if(removed) {previous_.statSources=std::move(root);sample(states,true);}
}
EffectApplication applySkillState(CombatEffectSet &states,const SkillCastSpec &skill,const CombatEffectSpec &spec,const StatRuleCatalog &rules,const CombatStateRules &stateRules,uint64_t tick,
    const std::function<void()> &observe) {
    std::vector<RemovedCombatEffect> groupRemoved;std::vector<int> clearedStates;
    // SrvDo018 clears the entire armour group, including the same skill,
    // before allocating its replacement. A recast must refill Bone Armor.
    const bool defensive=skill.effect==SkillBehavior::FrozenArmor || skill.effect==SkillBehavior::ShiverArmor ||
        skill.effect==SkillBehavior::ChillingArmor || skill.effect==SkillBehavior::BoneArmor || skill.effect==SkillBehavior::HolyShield || skill.effect==SkillBehavior::NativeDefensiveBuff;
    if(spec.nativeValues && defensive && spec.state.group) {
        for(const auto &[id,definition]:stateRules) if(definition.group==spec.state.group && states.hasState(id,tick)) {
            states.setNativeState(id,false);if(observe) observe();
            const auto *entry=states.findState(id);
            if(!entry) clearedStates.push_back(id);
            else {
                auto removed=states.remove(entry->handle,observe);
                groupRemoved.insert(groupRemoved.end(),removed.begin(),removed.end());
            }
        }
    }
    auto seed=spec;
    if(spec.nativeValues) {
        seed.nativeValues.emplace();seed.modifiers=nativeCharacterModifiers(StatSnapshot{},rules,spec.modifiers);
        seed.physicalShield=seed.physicalShieldMaximum=0;
    }
    auto result=states.apply(std::move(seed),tick,observe);
    result.clearedStates=std::move(clearedStates);
    result.removed.insert(result.removed.begin(),groupRemoved.begin(),groupRemoved.end());
    if(result.accepted && spec.nativeValues && !states.writeNativeStats(result.handle,skill.nativeEffectWrites,rules,spec,observe))
        throw std::runtime_error("Missing allocated native skill state");
    // SkillBar::SKILLS_ApplyWarcryStats refreshes after the six ordered
    // AuraStat writes, including when an existing list is reused.
    if(result.accepted && spec.nativeValues && skill.warcry) states.refreshPassiveSkills(observe);
    return result;
}
EffectApplication applyCurseState(CombatEffectSet &states,const CurseSpec &program,const CombatEffectSpec &spec,std::span<const StatValue> values,
    const StatRuleCatalog &rules,uint64_t tick,const std::function<void()> &observe,const std::function<void(const EffectApplication &)> &afterPost) {
    const bool targetControl=spec.curseAi==CurseAi::Confuse || spec.curseAi==CurseAi::Attract;
    if(program.rejectZeroPrimary && !targetControl && program.nativeValues && program.primaryStat && !values.empty() && values.front().key==*program.primaryStat && !values.front().value)
        return {{},{},false};
    auto seed=spec;
    if(program.nativeValues) {
        seed.unitEvents=program.unitEvents;
        seed.nativeState=seed.state.id>=0;
        if(spec.curseAi==CurseAi::Confuse || spec.curseAi==CurseAi::Attract) {
            seed.nativeRemoval=NativeStateRemoval::ClearTargets;seed.refreshPassivesOnRemoval=false;
        } else {
            seed.refreshPassivesOnRemoval=true;
            if(spec.curseAi!=CurseAi::None) seed.nativeRemoval=NativeStateRemoval::RestoreAi;
        }
        seed.nativeValues.emplace();seed.nativeValues->clear();
        if(program.primaryStat && !values.empty() && values.front().key==*program.primaryStat && (!targetControl || values.front().value))
            seed.nativeValues->insert_or_assign(values.front().key,values.front().value);
        seed.modifiers=nativeCharacterModifiers(StatSnapshot{*seed.nativeValues},rules,spec.modifiers);
    }
    auto result=states.apply(std::move(seed),tick,observe);
    if(result.accepted && afterPost) afterPost(result);
    if(result.accepted && program.nativeValues) {
        std::vector<StatValue> writes;
        for(size_t slot=0;slot<values.size();++slot) {
            // The first stat is already seeded before posting a new list;
            // an equal-level reused list preserves that original first stat.
            if(slot==0 && program.primaryStat && values[slot].key==*program.primaryStat) continue;
            if(values[slot].value || program.writeZeroSecondary) writes.push_back(values[slot]);
        }
        auto incoming=spec;incoming.unitEvents=program.unitEvents;
        if(!states.writeNativeStats(result.handle,writes,rules,incoming,observe)) throw std::runtime_error("Missing allocated native curse state");
    }
    return result;
}
DomainResult<PotionPlan> System::potion(const ActorContext &actor,const PotionDefinition &potion) const {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || player->actor!=actor.actor || !player->entered || player->area!=actor.area || !area || area->generation!=actor.areaGeneration || player->persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    PotionPlan plan{state_,player->persistent.player,{}};
    auto &recovery=plan.next.players[actor.actor];if(player->transient.effects) recovery.states=*player->transient.effects;
    const auto &attributes=player->totals.character;const auto &rules=*player->rules.character->stats;
    const auto shareHealing=[&](int32_t amount) {
        // Skills::sub_6FD0F8B0 shares the actual added amount with the owner's
        // blood golem before potion recovery is divided into per-frame rates.
        for(const auto id:ports_.monsters.pets(actor.player)) {
            const auto &pet=*ports_.monsters.find(id);
            if(pet.rule.petType!=3) continue; // Native PlayerPets golem slot.
            if(!pet.alive() || !pet.necroPet || pet.necroPet->kind!=NecroSummonKind::Blood) break;
            const auto requested=nativePercentage(amount,pet.necroPet->ownerHealingSharePercent,100);
            const auto gain=std::min(requested,nativeChecked(pet.maximumLife-pet.life));
            if(gain<=0) break;
            UnitEffect unit;if(const auto current=units_.find(id);current!=units_.end()) unit=current->second;unit.area=pet.area;
            MonsterResourceTrace resources(pet,actor.tick,&unit.states);
            resources.seedLife(Fixed8::fromRaw(nativeAdd(nativeChecked(pet.life),gain)));
            if(!plan.units) plan.units=std::make_shared<AuraUnitPublication>();
            prepareUnitEffects(*plan.units,id,unit,resources.take(),actor.tick);
            return nativeSubtract(amount,gain);
        }
        return amount;
    };
    std::optional<int32_t> restoration;
    if(potion.kind==PotionKind::Healing || potion.kind==PotionKind::Mana) {
        auto random=player->combatRandom;
        auto amount=nativeChecked(rollPotionRestoration(potion,player->definition.code,
            potion.kind==PotionKind::Healing?attributes.vitality:attributes.energy,random));
        if(amount<=0) return {DomainStatus::Unavailable,{}};
        if(potion.kind==PotionKind::Healing) amount=shareHealing(amount);
        restoration=amount;if(random!=player->combatRandom) plan.unitRandom=std::pair{player->combatRandom,random};
    } else if(potion.kind==PotionKind::Rejuvenation) {
        if(potion.amount<=0 || potion.amount>100) return {DomainStatus::Unavailable,{}};
        const auto life=shareHealing(nativePercentage(attributes.maxLifeFixed.raw(),potion.amount,100));
        plan.character.hp=Fixed8::fromRaw(std::min(attributes.maxLifeFixed.raw(),nativeAdd(plan.character.hp.raw(),life)));
        plan.character.mana=Fixed8::fromRaw(std::min(attributes.maxManaFixed.raw(),nativeAdd(plan.character.mana.raw(),nativePercentage(attributes.maxManaFixed.raw(),potion.amount,100))));
    }
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    resources.seedResources(plan.character.hp,plan.character.mana,plan.character.stamina);
    if(potion.state.id>=0) {
        const auto before=recovery.states.stateIds(actor.tick,true);
        const auto observe=[&]{resources.sample(recovery.states,true);};
        const auto afterWrite=[&](StatValue write) {
            if(write.key.id==rules.names.at("staminarecoverybonus") && write.value>0 && resources.record().stamina<resources.maximum().stamina) {
                const auto &record=resources.record();
                resources.seedResources(record.hp,record.mana,Fixed8::fromRaw(nativeAdd(resources.maximum().stamina.raw(),256)));
                plan.staminaPotionSurplus=true;
            }
        };
        const auto installed=applyPotionState(recovery.states,potion,actor.actor,actor.tick,rules,*player->rules.character->states,restoration,observe,afterWrite);
        if(!installed.accepted) return {DomainStatus::Conflict,{}};
        for(const int state:before) if(!recovery.states.hasState(state,actor.tick)) plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,state,false});
        const auto *effect=recovery.states.find(installed.handle);if(!effect) throw std::logic_error("Missing posted potion list");
        plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,potion.state.id,true,nativeEffectStats(rules,effect->spec)});
    }
    resources.sample(recovery.states);plan.character=resources.record();plan.resourceBase=resources.maximum();
    plan.transient=projectEffectAttributes(recovery.states,actor.tick);
    if(recovery.poison && !recovery.states.hasState(recovery.poison->damage.state,actor.tick)) recovery.poison.reset();
    return {DomainStatus::Applied,std::move(plan)};
}
DomainResult<PotionPlan> System::skillState(const ActorContext &actor,CombatEffectSpec spec,std::span<const StatValue> writes) const {
    const auto *player=ports_.players.find(actor.player);
    if(!player || player->actor!=actor.actor || !player->entered || player->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    PotionPlan plan{state_,player->persistent.player,{}};auto &states=plan.next.players[actor.actor].states;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    const auto observe=[&]{resources.sample(states,true);};
    const auto previousModes=states.endModeEffects(spec.modeActionRevision,observe);
    const auto applied=states.apply(spec,actor.tick,observe);
    if(!applied.accepted) return {DomainStatus::Conflict,{}};
    if(spec.nativeValues && !states.writeNativeStats(applied.handle,writes,*player->rules.character->stats,spec,observe))
        throw std::runtime_error("Missing native attack state list");
    for(const auto &removed:previousModes) if(removed.effect.spec.state.id>=0 && !states.hasState(removed.effect.spec.state.id,actor.tick))
        plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,removed.effect.spec.state.id,false});
    for(const auto &removed:applied.removed) if(removed.effect.spec.state.id>=0 && !states.hasState(removed.effect.spec.state.id,actor.tick))
        plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,removed.effect.spec.state.id,false});
    const auto *installed=states.find(applied.handle);if(!installed) throw std::runtime_error("Missing posted attack state list");
    if(spec.state.id>=0) plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,spec.state.id,true,nativeEffectStats(*player->rules.skills,installed->spec)});
    plan.character=resources.record();plan.resourceBase=resources.maximum();
    plan.transient=projectEffectAttributes(states,actor.tick);return {DomainStatus::Applied,std::move(plan)};
}
DomainResult<> System::frenzy(const ActorContext &actor,const SkillCastSpec &skill) {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->rules.skills || !player->rules.skills->formulas || !skill.weapon) return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.weapon;const auto &rules=*player->rules.skills->stats;
    const StatKey stackStat{rules.names.at("skill_frenzy"),0};int stacks=1;
    if(const auto found=state_.players.find(actor.actor);found!=state_.players.end())
        for(const auto &effect:found->second.states.entries()) if(effect.activeAt(actor.tick) && effect.spec.state.id==program.frenzyState.id) {
            const auto values=combatEffectNativeValues(effect.spec);
            const auto count=values?StatSnapshot{*values}.get(stackStat):effect.spec.source.level;
            stacks=nativeAdd(count,1);break;
        }
    stacks=std::min(stacks,skill.rank);
    auto context=skills::formulaInput(*player);SkillFormulaValues formulas(*player->rules.skills->formulas,context);
    const auto length=formulas.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"auralencalc");
    if(!length) return {DomainStatus::Unavailable,{}};
    CombatEffectSpec effect;effect.state=program.frenzyState;effect.duration=uint32_t(*length);
    effect.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};effect.stacking=EffectStacking::ReuseState;
    effect.nativeState=true;effect.refreshPassivesOnRemoval=true;effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
    effect.nativeValues.emplace();
    std::vector<StatValue> writes{{stackStat,stacks}};
    for(const auto &write:formulas.auraStats(skill.sourceId,stacks,6,true)) {
        writes.push_back(write);
        if(write.key.id==rules.names.at("attackrate")) writes.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
    }
    return apply(actor,std::move(effect),false,writes);
}
DomainResult<PotionPlan> System::heal(const ActorContext &actor) const {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || player->actor!=actor.actor || !player->entered || player->area!=actor.area ||
       !area || area->generation!=actor.areaGeneration || player->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    if(!player->rules.character) return {DomainStatus::Unavailable,{}};
    PotionPlan plan{state_,player->persistent.player,{}};auto &recovery=plan.next.players[actor.actor];
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    const auto maximum=resources.maximum();resources.seedResources(maximum.life,maximum.mana,maximum.stamina);
    const auto removePlayerState=[&](int state) {
        const auto *list=recovery.states.findState(state);if(!list) return;
        const auto removed=recovery.states.remove(list->handle,[&]{resources.sample(recovery.states,true);});
        for(const auto &effect:removed) if(!recovery.states.hasState(effect.effect.spec.state.id,actor.tick))
            plan.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
    };
    // HealPlayer fills resources, removes poison/freeze, then walks curable
    // state IDs. PetIterate_Heal fills HP, walks curable IDs, then poison/freeze.
    removePlayerState(player->rules.skills->poisoned.id);removePlayerState(player->rules.skills->frozen.id);
    for(const int state:player->rules.character->healerCureStates)
        if(player->rules.character->states->at(state).curable && recovery.states.hasState(state,actor.tick)) removePlayerState(state);
    if(recovery.poison && !recovery.states.hasState(recovery.poison->damage.state,actor.tick)) recovery.poison.reset();
    plan.character=resources.record();plan.resourceBase=resources.maximum();plan.transient=projectEffectAttributes(recovery.states,actor.tick);
    auto units=units_;plan.units=std::make_shared<AuraUnitPublication>();
    for(const auto id:ports_.monsters.pets(actor.player)) {
        const auto &m=*ports_.monsters.find(id);
        if(!m.nativeStats || !m.nativeStats->sources || !m.rule.stats) return {DomainStatus::Unavailable,{}};
        auto &unit=units[m.id];unit.area=m.area;MonsterResourceTrace trace(m,actor.tick,&unit.states);
        bool affected=m.life<m.maximumLife || bool(m.poison) || m.chilledUntil>actor.tick || m.frozenUntil>actor.tick;
        trace.seedLife(trace.read().maximum);
        const auto removePetState=[&](int state) {
            const auto *list=unit.states.findState(state);if(!list) return;
            const auto removed=unit.states.remove(list->handle,[&]{trace.sample(unit.states,true);});
            affected|=!removed.empty();
            for(const auto &effect:removed) if(!unit.states.hasState(effect.effect.spec.state.id,actor.tick)) {
                unit.nativeStats.erase(effect.effect.spec.state.id);
                plan.units->facts.emplace_back(StateFact{m.id,1,m.area,effect.effect.spec.state.id,false});
            }
        };
        for(const int state:player->rules.character->healerCureStates)
            if(player->rules.character->states->at(state).curable && unit.states.hasState(state,actor.tick)) removePetState(state);
        removePetState(player->rules.skills->poisoned.id);removePetState(player->rules.skills->frozen.id);
        if(!affected) continue;
        trace.sample(unit.states);auto life=trace.take();
        const bool ailments=bool(m.poison) || m.chilledUntil>actor.tick || m.frozenUntil>actor.tick;
        life.cureColdPoison=true;life.changed|=ailments;
        const auto appendRemoved=[&](int state) {
            if(state<0 || std::any_of(plan.units->facts.begin(),plan.units->facts.end(),[&](const auto &fact) {
                const auto *removed=std::get_if<StateFact>(&fact);return removed && removed->actor==m.id && removed->state==state && !removed->enabled;
            })) return;
            plan.units->facts.emplace_back(StateFact{m.id,1,m.area,state,false});
        };
        if(m.poison) appendRemoved(m.poison->damage.state);
        if(m.chilledUntil) appendRemoved(m.rule.coldState);if(m.frozenUntil) appendRemoved(m.rule.frozenState.id);
        // The coupled clear flag must accompany a changed reservation.
        life.changed=true;prepareUnitEffects(*plan.units,m.id,unit,std::move(life),actor.tick);
    }
    return {DomainStatus::Applied,std::move(plan)};
}
DomainResult<> System::removeState(const ActorContext &actor,int state) {
    if(ports_.monsters.find(actor.actor)) return removeUnitState(actor.actor,state,actor.tick);
    const auto *player=ports_.players.find(actor.player);
    if(!player || player->actor!=actor.actor || !player->entered || player->area!=actor.area) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &states=next.players[actor.actor].states;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    const auto removed=states.removeState(state,[&]{resources.sample(states,true);});
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();
    for(const auto &effect:removed) if(!states.hasState(effect.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
    edit.transient=projectEffectAttributes(states,actor.tick);
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next.players);
    return result;
}
DomainResult<WellRestoration> System::restoreWell(const ActorContext &actor,Fixed8 life,Fixed8 mana,Fixed8 stamina,const std::vector<NativeStateCure> &cureStates) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || player->actor!=actor.actor || !player->entered || player->area!=actor.area || player->persistent.player.hp<=0 ||
       !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &recovery=next.players[actor.actor];auto units=units_;
    bool used=life!=player->persistent.player.hp || mana!=player->persistent.player.mana || stamina!=player->persistent.player.stamina;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));resources.seedResources(life,mana,stamina);
    std::vector<RemovedCombatEffect> removedStates;
    for(const auto &[state,requiresStateFlag]:cureStates) {
        if(requiresStateFlag && !recovery.states.hasState(state,actor.tick)) continue;
        const auto *list=recovery.states.findState(state);if(!list) continue;
        auto removed=recovery.states.remove(list->handle,[&]{resources.sample(recovery.states,true);});
        used|=!removed.empty();
        removedStates.insert(removedStates.end(),removed.begin(),removed.end());
    }
    if(recovery.poison && !recovery.states.hasState(recovery.poison->damage.state,actor.tick)) recovery.poison.reset();
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    edit.player=resources.record();edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(recovery.states,actor.tick);
    for(const auto &effect:removedStates) if(!recovery.states.hasState(effect.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
    auto unitPublication=std::make_shared<AuraUnitPublication>();
    for(const auto id:ports_.monsters.pets(actor.player)) {
        const auto &m=*ports_.monsters.find(id);
        const bool healed=m.life<m.maximumLife;
        bool changed=healed || bool(m.poison) || m.chilledUntil>actor.tick || m.frozenUntil>actor.tick;
        auto &unit=units[id];unit.area=m.area;
        MonsterResourceTrace resources(m,actor.tick,&unit.states);resources.seedLife(resources.read().maximum);
        const auto appendRemoved=[&](int state) {
            if(state<0 || std::any_of(edit.publicFacts.begin(),edit.publicFacts.end(),[&](const auto &fact) {
                const auto *removed=std::get_if<StateFact>(&fact);return removed && removed->actor==id && removed->state==state && !removed->enabled;
            })) return;
            edit.publicFacts.emplace_back(StateFact{id,1,m.area,state,false});
        };
        for(const auto &[state,requiresStateFlag]:cureStates) {
            if(requiresStateFlag && !unit.states.hasState(state,actor.tick)) continue;
            const auto *list=unit.states.findState(state);if(!list) continue;
            unit.states.remove(list->handle,[&]{resources.sample(unit.states,true);});unit.nativeStats.erase(state);
            appendRemoved(state);changed=true;
        }
        if(!changed) continue;
        // OBJMODE_PetIterate_Heal marks the well used only when pet HP
        // increases. Its state-removal calls still run without that flag.
        used|=healed;resources.sample(unit.states);
        auto life=resources.take();life.cureColdPoison=true;life.changed=true;
        prepareUnitEffects(*unitPublication,id,unit,std::move(life),actor.tick);
        if(m.poison) appendRemoved(m.poison->damage.state);
        if(m.chilledUntil) appendRemoved(m.rule.coldState);if(m.frozenUntil) appendRemoved(m.rule.frozenState.id);
    }
    if(!used && unitPublication->units.empty()) return {DomainStatus::Applied,WellRestoration{false}};
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},std::move(unitPublication));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return {result.status,{}};
    publishPlayers(next.players);return {DomainStatus::Applied,WellRestoration{used}};
}
void System::commit(PotionPlan &&plan) noexcept {
    publishPlayers(plan.next.players);
}
DomainResult<> System::apply(const ActorContext &actor, CombatEffectSpec spec, bool restoreStamina,
    std::span<const StatValue> writes,bool refreshPassives) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || !player->entered || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (spec.state.id < 0 || spec.state.id > 255 || spec.physicalShield || spec.curseAi != CurseAi::None)
        return {DomainStatus::Unavailable, {}}; // Those execution hooks are not yet installed.
    auto next = state_; auto &states = next.players[actor.actor].states;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    // Stamina shrine refills before its list is allocated/posted. Each raw
    // maximum write then executes the ordinary resource callback.
    if(restoreStamina) resources.seedResources(player->persistent.player.hp,player->persistent.player.mana,resources.maximum().stamina);
    const auto observe=[&]{resources.sample(states,true);};
    const auto application=states.apply(spec,actor.tick,observe);
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    if(!writes.empty() && !states.writeNativeStats(application.handle,writes,*player->rules.character->stats,spec,observe))
        throw std::runtime_error("Missing native shrine state list");
    if(refreshPassives) states.refreshPassiveSkills(observe);
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();
    edit.transient = projectEffectAttributes(states, actor.tick);
    for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{player->actor,0,player->area,removed.effect.spec.state.id,false});
    const auto *installed=states.find(application.handle);
    if(!installed) throw std::runtime_error("Missing installed shrine state");
    edit.publicFacts.emplace_back(StateFact{player->actor,0,player->area,spec.state.id,true,nativeEffectStats(*player->rules.skills,installed->spec)});
    auto plan = ports_.transactions.prepare(std::move(edit));
    if (!plan) return {plan.status, {}};
    const auto result = ports_.transactions.commit(std::move(*plan.value));
    if (result) publishPlayers(next.players);
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    if(!frame_ || tick.tick>*frame_) {
        if(frame_ && framePhase_<10) throw std::logic_error("Effect frame advanced before completion");
        frame_=tick.tick;framePhase_=0;framePlayers_.clear();
    } else if(tick.tick<*frame_) throw std::logic_error("Effect frame moved backwards");
    if(framePhase_==0) ++framePhase_;
    if(framePhase_==1) ++framePhase_;
    bool blocked=false;
    if(framePhase_==2) {
    std::erase_if(state_.players, [&](const auto &entry) {
        for (const auto &[id, player] : ports_.players.all()) { (void)id; if (player.actor == entry.first) return false; }
        return true;
    });
    std::erase_if(stormLast_,[&](const auto &entry){return !ports_.players.findActor(entry.first.first);});
    std::erase_if(stormPulses_,[&](const auto &entry){
        if(ports_.frameEvents.find(entry.first)) return false;
        if(entry.second.projectile) ports_.missiles.removeTransient(entry.second.projectile);return true;
    });
    std::erase_if(paladinCycles_,[&](const auto &entry){return !ports_.players.findActor(entry.first);});
    std::erase_if(unitAuraCycles_,[&](const auto &entry){return !ports_.monsters.find(entry.first);});
    std::erase_if(itemAuraCycles_,[&](const auto &entry){return !ports_.players.findActor(entry.first.first) && !ports_.monsters.find(entry.first.first);});
    ports_.actions.pruneRecoveries(ports_.players);
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered) continue;
        const auto &area = ports_.areas.at(player.area);
        const ActorContext actor{id, player.actor, player.area, area.generation, 0, tick.tick};
        auto &playerFrame=framePlayers_.try_emplace(player.actor).first->second;
        if(playerFrame.complete) continue;
        auto [entry, inserted] = state_.players.try_emplace(player.actor); (void)inserted;
        auto next = entry->second;
        const bool dead = player.persistent.player.hp <= 0;
        std::optional<PlayerResourceTrace> resources;
        if(dead || next.states.hasDueExpiration(tick.tick))
            resources.emplace(player,tick.tick);
        const auto observeRemoval=[&]{resources->sample(next.states,true);};
        std::vector<RemovedCombatEffect> removed;
        std::vector<int> clearedStates;
        {
            if(dead) {
                removed=next.states.onDeath(EffectUnitKind::Player,observeRemoval);
                if(!player.rules.character->states) throw std::runtime_error("Missing native player death masks");
                clearedStates=next.states.clearNonDeathStates(EffectUnitKind::Player,*player.rules.character->states,tick.tick,observeRemoval);
                auto expired=next.states.expireOrdinary(tick.tick,observeRemoval,EffectUnitKind::Player);
                removed.insert(removed.end(),expired.begin(),expired.end());
            } else {
                auto expired=next.states.expireOrdinary(tick.tick,observeRemoval);
                removed.insert(removed.end(),expired.begin(),expired.end());
            }
        }
        if(dead) {next.poison.reset();next.wound.reset();}
        if (dead) {
            if (!removed.empty() || !clearedStates.empty() || !player.totals.itemAuraTimers.empty()) {
                transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,resources->record()};
                restoreStaminaOnEffectRemoval(edit.player.stamina,resources->maximum().stamina,removed);
                edit.resourceBase=resources->maximum();
                edit.transient=projectEffectAttributes(next.states,tick.tick);
                edit.cancelItemAuras=true;
                for(const auto &effect:removed) if(effect.effect.spec.state.id>=0 && !next.states.hasState(effect.effect.spec.state.id,tick.tick))
                    edit.publicFacts.emplace_back(StateFact{player.actor,0,player.area,effect.effect.spec.state.id,false});
                for(const int state:clearedStates) if(!player.rules.character->states->at(state).noSend &&
                    std::none_of(removed.begin(),removed.end(),[&](const auto &effect){return effect.effect.spec.state.id==state;}))
                    edit.publicFacts.emplace_back(StateFact{player.actor,0,player.area,state,false});
                auto plan=ports_.transactions.prepare(std::move(edit));
                if (!plan || !ports_.transactions.commit(std::move(*plan.value))) { blocked=true; continue; }
            }
            if(player.transient.effects) next.states=*player.transient.effects;
            std::swap(entry->second,next); ports_.actions.clearRecovery(player.actor);playerFrame.complete=true;continue;
        }
        next.killer={};next.killerName.clear();next.killerClass.clear();
        if(next.poison && !next.states.hasState(next.poison->damage.state,tick.tick)) next.poison.reset();
        if(next.wound && !next.states.hasState(player.rules.skills->openWounds.id,tick.tick)) next.wound.reset();
        if(!removed.empty()) {
            auto record=resources->record();restoreStaminaOnEffectRemoval(record.stamina,resources->maximum().stamina,removed);
            transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(record)};
            edit.transient=projectEffectAttributes(next.states,tick.tick);edit.resourceBase=resources->maximum();
            for(const auto &effect:removed) if(effect.effect.spec.state.id>=0 && !next.states.hasState(effect.effect.spec.state.id,tick.tick))
                edit.publicFacts.emplace_back(StateFact{player.actor,0,player.area,effect.effect.spec.state.id,false});
            auto plan=ports_.transactions.prepare(std::move(edit));
            if(!plan || !ports_.transactions.commit(std::move(*plan.value))) {blocked=true;continue;}
        }
        if(player.transient.effects) next.states=*player.transient.effects;
        playerFrame.complete=true;
        std::swap(entry->second,next);
    }
    if(blocked) return StepStatus::Blocked;
    ++framePhase_;
    }
    if(framePhase_==3) {if(advanceItemTriggers(tick.tick)==StepStatus::Blocked) return StepStatus::Blocked;++framePhase_;}
    if(framePhase_==4) ++framePhase_;
    if(framePhase_==5) ++framePhase_;
    if(framePhase_==6) {if(advanceUnits(tick.tick)==StepStatus::Blocked) return StepStatus::Blocked;++framePhase_;}
    if(framePhase_==7) ++framePhase_;
    if(framePhase_==8) {if(advanceMonsterCurses(tick.tick)==StepStatus::Blocked) return StepStatus::Blocked;++framePhase_;}
    if(framePhase_==9) ++framePhase_;
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
