#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/combat/scaling.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::effects {
const CombatEffectSet *System::unitEffects(EntityId unit) const {
    if(const auto *player=ports_.players.findActor(unit);player && player->transient.effects) return player->transient.effects.get();
    if(const auto found=units_.find(unit);found!=units_.end()) {
        if(ports_.monsters.find(unit)) return &found->second.states;
    }
    return nullptr;
}
NativeContactBatch System::prepareDamageEvents(EntityId source,EntityId target,int64_t physical,int64_t total,bool missile,bool returnFire,uint64_t tick,bool suppress) const {
    NativeContactBatch result;result.source=source;result.target=target;result.physical=physical;result.total=total;
    result.missile=missile;result.returnFire=returnFire;result.suppress=suppress;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto defender=participants.find(target),attacker=participants.find(source);
    result.area=defender.player?defender.player->area:defender.monster?defender.monster->area:RegionId{};
    if(!missile && !suppress && physical>0) {
        auto modifiers=defender.player?defender.player->totals.character.combat:unitModifiers(target,tick).combat;
        if(defender.monster && defender.monster->combatCompanion()) mergeCombatModifiers(modifiers,defender.monster->petStats.attributes.combat);
        result.thorns=nativeThornsDamage(nativeChecked(physical),modifiers.thornsPercent,attacker.player || (attacker.monster && attacker.monster->hireling));
    }
    return result;
}
NativeContactBatch System::prepareAttackEvents(EntityId source,EntityId target,uint64_t tick) const {
    NativeContactBatch result;result.source=source;result.target=target;result.eventStage=4;
    if(const auto *states=unitEffects(target)) for(const auto &callback:states->unitEvents(NativeUnitEvent::AttackedInMelee,tick))
        result.callbacks.push_back({target,source,callback});
    return result;
}
NativeContactBatch System::prepareMeleeDrainEvents(EntityId source,EntityId target,uint64_t tick) const {
    NativeContactBatch result;result.source=source;result.target=target;result.eventStage=5;
    if(const auto *states=unitEffects(source)) for(const auto &callback:states->unitEvents(NativeUnitEvent::MeleeAttack,tick))
        result.callbacks.push_back({source,target,callback});
    return result;
}
NativeContactBatch System::prepareThornsEvents(EntityId source,EntityId target,int64_t physical,uint64_t tick) const {
    auto result=prepareDamageEvents(source,target,physical,physical,false,false,tick,false);
    result.suppress=true;return result;
}
NativeContactBatch System::prepareMissileEvents(EntityId source,EntityId target,uint64_t tick,bool returnFire) const {
    NativeContactBatch result;result.source=source;result.target=target;result.eventStage=4;result.returnFire=returnFire;
    if(const auto *states=unitEffects(target)) for(const auto &callback:states->unitEvents(NativeUnitEvent::HitByMissile,tick))
        result.callbacks.push_back({target,source,callback});
    return result;
}
DomainResult<> System::advanceDamageEvents(NativeContactBatch &batch,uint64_t tick) {
    const auto resetCallback=[&] {
        batch.pendingDamage.reset();batch.pendingOccurrence=0;batch.queued=false;batch.random.reset();
        batch.randomCommitted=false;batch.callbackStarted=false;batch.phase=0;batch.remaining=0;batch.share=0;batch.overlay=-1;batch.owner={};
    };
    if(!batch.eventStage) {
        if(batch.thorns>0) {
            if(!batch.pendingDamage) {
                if(necroOccurrence_>=(uint64_t{1}<<59)-1) throw std::overflow_error("Native contact occurrence exhausted");
                batch.pendingOccurrence=(uint64_t{1}<<59)|(++necroOccurrence_);
                batch.pendingDamage=std::make_shared<combat::SpellImpact>(combat::SpellImpact{batch.target,batch.target,batch.area,DamageType::Physical,batch.thorns,{batch.source}});
                batch.pendingDamage->occurrence=batch.pendingOccurrence;batch.pendingDamage->reaction=true;
                batch.pendingDamage->unblockable=true;batch.pendingDamage->hitClass=0x8d;
            }
            if(!batch.queued) {const auto queued=ports_.combat.enqueue(*batch.pendingDamage);if(!queued) return queued;batch.queued=true;}
            if(ports_.combat.advanceAuraPulse(batch.target,batch.pendingOccurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
            resetCallback();
        }
        batch.eventStage=batch.suppress?4:1;
    }
    for(;;) {
        while(batch.next<batch.callbacks.size()) {
            if(!batch.callbackStarted) {
                const auto &callback=batch.callbacks[batch.next];const auto *states=unitEffects(callback.unit);
                if(!states || !states->registered(callback.event)) {++batch.next;continue;}
                batch.callbackStarted=true;
            }
            const auto result=advanceNativeCallback(batch,tick);
            if(!result) return result;
            ++batch.next;resetCallback();
        }
        if(batch.eventStage==5) {
            batch.callbacks.clear();batch.next=0;batch.eventStage=6;
            if(const auto *states=unitEffects(batch.target)) for(const auto &callback:states->unitEvents(NativeUnitEvent::AttackedInMelee,tick))
                batch.callbacks.push_back({batch.target,batch.source,callback});
            continue;
        }
        if(batch.eventStage>=3) return {DomainStatus::Applied,std::monostate{}};
        batch.callbacks.clear();batch.next=0;
        const bool attacker=batch.eventStage==1;
        const auto unit=attacker?batch.source:batch.target,other=attacker?batch.target:batch.source;
        const auto event=attacker?(batch.missile?NativeUnitEvent::DealtMissileDamage:NativeUnitEvent::DealtMeleeDamage):
            (batch.missile?NativeUnitEvent::DamagedByMissile:NativeUnitEvent::DamagedInMelee);
        if(!attacker || !batch.suppressAttacker)
            if(const auto *states=unitEffects(unit)) for(const auto &callback:states->unitEvents(event,tick)) batch.callbacks.push_back({unit,other,callback});
        ++batch.eventStage;
    }
}
DomainResult<> System::advanceNativeCallback(NativeContactBatch &batch,uint64_t tick) {
    const auto &callback=batch.callbacks.at(batch.next);const int function=callback.event.binding.function;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto unit=participants.find(callback.unit),other=participants.find(callback.other);
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const int rank=callback.event.source.level<0?1:callback.event.source.level;
    const auto valuesFor=[&](EntityId caster,const auto &visit)->bool {
        if(const auto *player=ports_.players.findActor(caster);player && player->rules.skills && player->rules.skills->formulas) {
            auto input=skills::formulaInput(*player);SkillFormulaValues values(*player->rules.skills->formulas,input);visit(values);return true;
        }
        if(const auto *monster=ports_.monsters.find(caster);monster && monster->rule.formulas && monster->rule.stats)
            if(const auto sampled=unitStatValues(caster,tick)) {
                auto input=skills::formulaInput(*monster,*sampled);SkillFormulaValues values(*monster->rule.formulas,input);visit(values);return true;
            }
        return false;
    };
    const auto overlay=[&](EntityId recipient,int visual)->DomainResult<> {
        if(visual<0) return done();
        const auto target=participants.find(recipient);if(!target.id()) return done();
        const auto region=target.player?target.player->area:target.monster->area;
        const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},{OverlayFact{recipient,uint8_t(target.player?0:1),region,visual}}});
        return {published.status,published?std::optional{std::monostate{}}:std::nullopt};
    };
    const auto heal=[&](EntityId recipient,int64_t requested)->DomainResult<int64_t> {
        const auto target=participants.find(recipient);
        if(requested<=0 || !target.id()) return {DomainStatus::Applied,int64_t{0}};
        if(target.player) {
            const auto &player=*target.player;if(player.persistent.player.hp<=0) return {DomainStatus::Applied,int64_t{0}};
            const auto gain=std::min(requested,std::max<int64_t>(0,int64_t(player.totals.character.maxLifeFixed.raw())-player.persistent.player.hp.raw()));
            const auto &area=ports_.areas.at(player.area);
            const auto committed=ports_.transactions.resources({player.player,player.actor,player.area,area.generation,0,tick},player.characterRevision,
                Fixed8::fromRaw(nativeAdd(player.persistent.player.hp.raw(),nativeChecked(gain))),player.persistent.player.mana,player.persistent.player.stamina);
            return {committed.status,committed?std::optional{gain}:std::nullopt};
        }
        if(target.monster->life<=0) return {DomainStatus::Applied,int64_t{0}};
        const auto gain=std::min(requested,std::max<int64_t>(0,target.monster->maximumLife-target.monster->life));
        const auto committed=ports_.monsters.heal(recipient,gain);
        return {committed.status,committed?std::optional{gain}:std::nullopt};
    };
    if(!unit.id() || (!other.id() && function!=26)) return done();
    const auto calculate=[&](EntityId caster,std::string_view field)->RuleNumber {
        if(const auto *player=ports_.players.findActor(caster);player && player->rules.skills && player->rules.skills->formulas) {
            auto input=skills::formulaInput(*player);SkillFormulaValues values(*player->rules.skills->formulas,input);
            return values.calculation(FormulaDomain::Skill,callback.event.source.definition,rank,field);
        }
        if(const auto *monster=ports_.monsters.find(caster);monster && monster->rule.formulas && monster->rule.stats)
            if(const auto sampled=unitStatValues(caster,tick)) {
                auto input=skills::formulaInput(*monster,*sampled);SkillFormulaValues values(*monster->rule.formulas,input);
                return values.calculation(FormulaDomain::Skill,callback.event.source.definition,rank,field);
            }
        const auto *catalog=unit.player && unit.player->rules.skills?unit.player->rules.skills->formulas.get():unit.monster?unit.monster->rule.formulas.get():nullptr;
        if(catalog) {
            const std::map<int,int> ranks;SkillFormulaInput input{ranks,ranks};SkillFormulaValues values(*catalog,input);
            return values.calculation(FormulaDomain::Skill,callback.event.source.definition,rank,field);
        }
        return {};
    };
    if(function==1) {
        if(!batch.returnFire || !participants.canHarm(callback.unit,callback.other)) return done();
        const auto *catalog=unit.player && unit.player->rules.skills?unit.player->rules.skills->formulas.get():
            unit.monster?unit.monster->rule.formulas.get():nullptr;
        if(!catalog) return {DomainStatus::Unavailable,{}};
        const auto &program=catalog->skills.at(callback.event.source.definition);
        const auto missile=ports_.missiles.nativeDefinition(program.reference("srvmissilea"));
        if(!missile) return {DomainStatus::Unavailable,{}};
        const auto region=unit.player?unit.player->area:unit.monster->area;
        const auto otherRegion=other.player?other.player->area:other.monster->area;
        const auto &area=ports_.areas.at(region);
        const auto target=other.position()+ports_.areas.at(otherRegion).definition.origin-area.definition.origin;
        // EventFunc01 owns a raw flags0x20 missile; the reacting monster is
        // its owner, independently of a player controller or summon owner.
        missiles::NativeSpawn request{callback.unit,region,area.generation,tick,unit.position(),target,*missile,program.id,rank};
        const auto spawned=ports_.missiles.spawnNative(request);
        return {spawned.status,spawned?std::optional{std::monostate{}}:std::nullopt};
    } else if(function==2 || function==3) {
        if(function==2 && batch.physical<=0) return done();
        if(!batch.pendingDamage) {
            RuleNumber minimum,maximum,length;std::string_view element;
            const auto evaluate=[&](SkillFormulaValues &values) {
                const auto id=callback.event.source.definition;
                if(function==2) {minimum=maximum=0;length=values.calculation(FormulaDomain::Skill,id,rank,"calc1");element="frze";}
                else {minimum=values.parameter(FormulaDomain::Skill,id,rank,"enms");maximum=values.parameter(FormulaDomain::Skill,id,rank,"exms");length=values.parameter(FormulaDomain::Skill,id,rank,"edln");element=values.elementalCode(id);}
            };
            if(unit.player && unit.player->rules.skills && unit.player->rules.skills->formulas) {
                auto input=skills::formulaInput(*unit.player);SkillFormulaValues values(*unit.player->rules.skills->formulas,input);evaluate(values);
            } else if(unit.monster && unit.monster->rule.formulas && unit.monster->rule.stats) {
                const auto sampled=unitStatValues(callback.unit,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
                auto input=skills::formulaInput(*unit.monster,*sampled);SkillFormulaValues values(*unit.monster->rule.formulas,input);evaluate(values);
            }
            if(!minimum || !maximum || !length) return {DomainStatus::Unavailable,{}};
            if(element!="cold" && element!="frze" && element!="fire" && element!="ltng" && element!="mag" && element!="pois" && !element.empty()) return {DomainStatus::NotImplemented,{}};
            batch.overlay=-1;valuesFor(callback.unit,[&](SkillFormulaValues &values){batch.overlay=values.skillOverlay(callback.event.source.definition,false);});
            const auto kind=element=="cold" || element=="frze"?DamageType::Cold:element=="fire"?DamageType::Fire:
                element=="ltng"?DamageType::Lightning:element=="mag"?DamageType::Magic:element=="pois"?DamageType::Poison:DamageType::Physical;
            auto random=unit.player?unit.player->combatRandom:unit.monster->combatRandom;const auto expected=random;
            const auto amount=nativeAdd(*minimum,int32_t(limitedRandom(random,uint32_t(nativeSubtract(*maximum,*minimum)))));
            const auto region=other.player?other.player->area:other.monster->area;
            auto damage=std::make_shared<combat::SpellImpact>(combat::SpellImpact{callback.unit,callback.unit,region,kind,amount,{callback.other}});
            damage->reaction=true;damage->unblockable=true;damage->hitClass=0x0d;damage->freeze=element=="frze";
            if(kind==DamageType::Cold) damage->coldFrames=uint32_t(*length);
            if(kind==DamageType::Poison) damage->poisonFrames=uint32_t(*length);
            if(necroOccurrence_>=(uint64_t{1}<<59)-1) throw std::overflow_error("Native contact occurrence exhausted");
            damage->occurrence=(uint64_t{1}<<59)|(++necroOccurrence_);batch.pendingOccurrence=damage->occurrence;
            batch.random=std::pair{expected,random};batch.pendingDamage=std::move(damage);
        }
    } else if(function==4) {
        if(batch.physical<=0 || (unit.player?unit.player->persistent.player.hp<=0:unit.monster->life<=0)) return done();
        if(!batch.pendingDamage) {
            const auto field=unit.player || (unit.monster && unit.monster->hireling)?(other.monster?"calc2":"calc3"):"calc1";
            const auto percent=calculate(callback.event.source.entity,field);if(!percent) return {DomainStatus::Unavailable,{}};
            const auto amount=nativePercentage(nativeChecked(batch.physical),*percent,100);if(amount<=0) return done();
            const auto region=unit.player?unit.player->area:unit.monster->area;
            auto damage=std::make_shared<combat::SpellImpact>(combat::SpellImpact{callback.unit,callback.other,region,DamageType::Physical,amount,{callback.unit}});
            damage->reaction=true;damage->unblockable=true;damage->hitClass=0x8d;
            if(necroOccurrence_>=(uint64_t{1}<<59)-1) throw std::overflow_error("Native contact occurrence exhausted");
            damage->occurrence=(uint64_t{1}<<59)|(++necroOccurrence_);batch.pendingOccurrence=damage->occurrence;batch.pendingDamage=std::move(damage);
        }
    } else if(function==5) {
        if(batch.phase==0) {
            if(batch.physical<=0 || (other.player?other.player->persistent.player.hp<=0:other.monster->life<=0)) return done();
            const auto percent=calculate(callback.event.source.entity,"calc1");if(!percent) return {DomainStatus::Unavailable,{}};
            const auto gain=nativePercentage(nativeChecked(batch.physical),*percent,100);
            DomainResult<> healed;
            if(other.player) {
                const auto &player=*other.player;const auto &area=ports_.areas.at(player.area);
                healed=ports_.transactions.resources({player.player,player.actor,player.area,area.generation,0,tick},player.characterRevision,
                    Fixed8::fromRaw(std::clamp(nativeAdd(player.persistent.player.hp.raw(),gain),1,player.totals.character.maxLifeFixed.raw())),player.persistent.player.mana,player.persistent.player.stamina);
            } else {
                MonsterResourceTrace resources(*other.monster,tick);
                resources.seedLife(Fixed8::fromRaw(std::clamp(nativeAdd(nativeChecked(other.monster->life),gain),1,nativeChecked(other.monster->maximumLife))));
                healed=ports_.monsters.commitLife(resources.take())?done():DomainResult<>{DomainStatus::Stale,{}};
            }
            if(!healed) return healed;
            valuesFor(callback.unit,[&](SkillFormulaValues &values){batch.overlay=values.skillOverlay(callback.event.source.definition,true);});
            batch.phase=1;
        }
        return overlay(callback.other,batch.overlay>0?batch.overlay:-1);
    } else if(function==23) {
        if(!unit.monster || !unit.monster->owner) return done();
        const auto *owner=ports_.players.find(*unit.monster->owner);
        if(!owner || owner->persistent.player.hp<=0 || unit.monster->life<=0) return done();
        if(!batch.phase) {
            if(other.alignment()!=UnitAlignment::Evil) return done();
            const int drain=other.monster?other.monster->rule.drain:100;
            if(drain==0) return done();
            RuleNumber percent;
            if(!valuesFor(callback.unit,[&](SkillFormulaValues &values) {
                percent=values.parameter(FormulaDomain::Skill,callback.event.source.definition,rank,"dm12");
                batch.share=values.skillNumber(callback.event.source.definition,"param3");
            }) || !percent || *percent<=0) return done();
            const auto physical=drain==100?nativeChecked(batch.physical):nativePercentage(nativeChecked(batch.physical),drain,100);
            const auto life=other.player?other.player->persistent.player.hp.raw():nativeChecked(other.monster->life);
            batch.remaining=nativePercentage(std::min(physical,life),*percent,100);
            if(batch.remaining<=0) return done();
            batch.owner=owner->actor;batch.overlay=unit.monster->necroPet?unit.monster->necroPet->healOverlay:-1;batch.phase=1;
        }
        if(batch.phase==1) {
            const auto healed=heal(batch.owner,batch.share>0?nativePercentage(nativeChecked(batch.remaining),batch.share,100):0);if(!healed) return {healed.status,{}};
            batch.remaining-=*healed.value;batch.phase=2;
        }
        if(batch.phase==2) {const auto healed=heal(callback.unit,batch.remaining);if(!healed) return {healed.status,{}};batch.remaining-=*healed.value;batch.phase=3;}
        if(batch.phase==3) {const auto healed=heal(batch.owner,batch.remaining);if(!healed) return {healed.status,{}};batch.phase=4;}
        if(batch.phase==4) {const auto published=overlay(callback.unit,batch.overlay);if(!published) return published;batch.phase=5;}
        return batch.share>0?overlay(batch.owner,batch.overlay):done();
    } else if(function==26) {
        if(!unit.monster || !unit.monster->owner || batch.total<=0) return done();
        const auto *owner=ports_.players.find(*unit.monster->owner);
        if(!owner || owner->persistent.player.hp.raw()<256) return done();
        int percentage=0;valuesFor(callback.unit,[&](SkillFormulaValues &values){percentage=values.skillNumber(callback.event.source.definition,"param5");});
        const auto reduction=nativePercentage(nativeChecked(batch.total),percentage,100);
        if(!reduction) return done();
        const auto &area=ports_.areas.at(owner->area);
        const auto committed=ports_.transactions.resources({owner->player,owner->actor,owner->area,area.generation,0,tick},owner->characterRevision,
            Fixed8::fromRaw(std::max(256,nativeSubtract(owner->persistent.player.hp.raw(),reduction))),owner->persistent.player.mana,owner->persistent.player.stamina);
        if(!committed) return committed;
        batch.total=std::max(0,nativeSubtract(nativeChecked(batch.total),reduction));return done();
    } else if(function==27) {
        if(!unit.monster || !unit.monster->necroPet) return done();
        int slow=unit.monster->necroPet->slowPercent;
        if(const auto values=unitStatValues(callback.unit,tick);values && unit.monster->rule.stats)
            slow=nativeStatValue(*values,*unit.monster->rule.stats,"item_slow");
        if(!slow) return done();
        const bool championOrUnique=other.monster && (other.monster->identity.rank==MonsterRank::Champion || other.monster->identity.rank==MonsterRank::Unique || other.monster->identity.rank==MonsterRank::SuperUnique);
        slow=nativeSlowPercentage(slow,bool(other.player),championOrUnique);
        const auto &state=unit.monster->necroPet->slowState;
        const auto &rules=*(other.player?other.player->rules.character->stats:other.monster->rule.stats);
        const auto duration=curseStateDuration(750,unitCurseResistance(callback.other,tick),state.curse);
        if(!duration) return done();
        CombatEffectSpec spec;spec.state=state;spec.duration=uint32_t(*duration);spec.source={CombatEffectSource::Skill,callback.unit,0,1};
        spec.stacking=EffectStacking::CurseLevel;spec.nativeState=true;spec.nativeRemoval=NativeStateRemoval::BasicAura;spec.refreshPassivesOnRemoval=true;
        CurseSpec program;program.state=state;program.nativeValues.emplace();
        for(const auto name:{"velocitypercent","attackrate","other_animrate"}) {
            const StatKey key{rules.names.at(name),0};program.nativeWrites.push_back({key,-slow});program.nativeValues->emplace(key,-slow);
        }
        program.primaryStat=program.nativeWrites.front().key;spec.nativeValues=program.nativeValues;
        if(!configurePd2Curse(spec,callback.other,tick)) return done();
        if(other.player) {
            const auto &player=*other.player;const auto &area=ports_.areas.at(player.area);
            auto next=state_;auto &recovery=next.players[player.actor];
            PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(player.area));
            const auto application=applyCurseState(recovery.states,program,spec,program.nativeWrites,rules,tick,[&]{resources.sample(recovery.states,true);});
            if(!application.accepted) return done();
            transactions::CharacterEdit edit{{player.player,player.actor,player.area,area.generation,0,tick},player.inventoryRevision,player.characterRevision,resources.record()};
            edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(recovery.states,tick);
            for(const auto &removed:application.removed) if(!recovery.states.hasState(removed.effect.spec.state.id,tick)) edit.publicFacts.emplace_back(StateFact{player.actor,0,player.area,removed.effect.spec.state.id,false});
            const auto *installed=recovery.states.find(application.handle);if(!installed) throw std::logic_error("Missing native slowed player list");
            edit.publicFacts.emplace_back(StateFact{player.actor,0,player.area,state.id,true,nativeEffectStats(*player.rules.skills,installed->spec)});
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
            const auto committed=ports_.transactions.commit(std::move(*plan.value));if(committed) publishPlayers(next.players);return committed;
        }
        const auto &monster=*other.monster;UnitEffect candidate;
        if(const auto current=units_.find(monster.id);current!=units_.end()) candidate=current->second;
        candidate.area=monster.area;
        MonsterResourceTrace resources(monster,tick,&candidate.states);
        const auto application=applyCurseState(candidate.states,program,spec,program.nativeWrites,rules,tick,[&]{resources.sample(candidate.states,true);});
        if(!application.accepted) return done();
        auto publication=std::make_shared<AuraUnitPublication>();
        for(const auto &removed:application.removed) if(!candidate.states.hasState(removed.effect.spec.state.id,tick)) publication->facts.emplace_back(StateFact{monster.id,1,monster.area,removed.effect.spec.state.id,false});
        const auto *installed=candidate.states.find(application.handle);if(!installed) throw std::logic_error("Missing native slowed monster list");
        auto native=combatEffectNativeValues(installed->spec);std::vector<StatValue> facts;
        if(native) for(const auto &[key,value]:*native) facts.push_back({key,value});
        candidate.nativeStats[state.id]=facts;publication->facts.emplace_back(StateFact{monster.id,1,monster.area,state.id,true,std::move(facts)});
        prepareUnitEffects(*publication,monster.id,candidate,resources.take(),tick);
        if(!validateAuraUnits(*publication)) return {DomainStatus::Stale,{}};
        const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},monster.area},std::move(publication->facts)});if(!published) return {published.status,{}};
        publishAuraUnits(*publication);return done();
    } else {
        return {DomainStatus::NotImplemented,{}};
    }
    if(batch.random && !batch.randomCommitted) {
        const auto [expected,next]=*batch.random;
        if(unit.player) {
            const auto &player=*unit.player;const auto &area=ports_.areas.at(player.area);
            transactions::CharacterEdit edit{{player.player,player.actor,player.area,area.generation,0,tick},player.inventoryRevision,player.characterRevision,player.persistent.player};
            edit.random=transactions::UnitRandomEdit{expected,next};
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
            const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
        } else {
            const auto committed=ports_.monsters.consumeRandom(callback.unit,expected,next);if(!committed) return committed;
        }
        batch.randomCommitted=true;
    }
    if(batch.phase==0) {
        if(!batch.queued) {const auto queued=ports_.combat.enqueue(*batch.pendingDamage);if(!queued) return queued;batch.queued=true;}
        if(ports_.combat.advanceAuraPulse(batch.pendingDamage->projectile,batch.pendingOccurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        batch.phase=1;
    }
    return overlay(callback.other,batch.overlay);
}
}
