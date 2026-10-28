#include "system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "runtime.hpp"
#include "shape.hpp"
#include "druid_melee.hpp"
#include "weapon_melee.hpp"
#include "pd2/vengeance.hpp"
#include "pd2/charge.hpp"
#include "pd2/execution.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "gameplay/skills/barbarian_sequence.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/assassin_sequence.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/skills/targeting.hpp"
#include "server/systems/effects/attribute_projection.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace d2x::server::skills {
namespace {
int volleyFormula(const PlayerState &player,const SkillCastSpec &skill,std::string_view field) {
    auto input=formulaInput(player);SkillFormulaValues values(*player.rules.skills->formulas,input);
    const auto value=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,field);
    if(!value) throw std::logic_error("Missing native volley formula");
    return *value;
}
std::vector<EntityId> volleyTargets(const Ports &ports,const ActorContext &actor,const PlayerState &player,const SkillCastSpec &skill) {
    const auto &program=*skill.weapon;
    const bool strafe=program.bow && program.bow->strafe;
    const int radius=strafe?volleyFormula(player,skill,"aurarangecalc"):player.totals.equipment.weapons[0].rangeAdder+4;
    return ports.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,radius,strafe?0xa783u:0x2a783u,false,{},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
}
}
DomainResult<> System::avoidance(const ActorContext &actor,WeaponAvoidance result,EntityId attacker) {
    const auto ended=ports_.effects.endMode(actor.actor,actor.tick);if(!ended) return ended;
    const auto &p=*ports_.players.find(actor.player);
    if(result==WeaponAvoidance::Evade) {
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{SoundFact{actor.actor,0,actor.area,12}}});
        return {output.status,output?std::optional{std::monostate{}}:std::nullopt};
    }
    const int id=result==WeaponAvoidance::Dodge?13:18;
    const auto rank=p.totals.skillRanks.find(id);if(rank==p.totals.skillRanks.end()) return {DomainStatus::Unavailable,{}};
    const auto animation=p.rules.skills->weaponAnimations.find("s1"+p.totals.equipment.animationClass);
    if(animation==p.rules.skills->weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
    const auto &a=animation->second;
    std::map<EntityId,Cast> candidate;candidate.emplace(actor.actor,Cast{actor.actor,uint16_t(id),actor.tick,actor.tick,actor.tick+uint64_t(std::max(1,(a.frames*256+a.speed-1)/a.speed)),actor.area,attacker});
    const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{AttackFact{actor.actor,attacker,0,uint8_t(ports_.players.findActor(attacker)?0:1),actor.area,p.position,p.position,actor.tick,uint16_t(id),uint8_t(rank->second),true}}});
    if(!output) return {output.status,{}};
    cancel(actor.player,actor.actor);state_.casts.erase(actor.actor);state_.casts.merge(candidate);ports_.movement.execute(actor,{MovementAction::Stop,{},false});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::weaponCast(const ActorContext &actor,const Request &request,int id) {
    auto &releases_=runtime_->releases;
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    const auto &rules=*p.rules.skills;
    if(area.definition.town || !request.target) return {DomainStatus::Unavailable,{}};
    if(busy(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    const auto source=selectedSource(p,id,request.right);
    if(!source) return {DomainStatus::Unavailable,{}};
    const int effectiveRank=source->rank;
    if(effectiveRank<=0 || effectiveRank>255) return {DomainStatus::Unavailable,{}};
    auto skill=evaluate(p,id,effectiveRank);
    if(source->charged) {skill.charge=SkillCharge{source->charged->item,source->charged->layer};skill.manaCost=skill.startMana=0;}
    const auto &program=*skill.weapon;
    const auto matchesWeapon=[&](const WeaponDamage &weapon) {
        // D2Common sub_6FDB1130 accepts an empty hand for the generic
        // weapon/melee/hand-to-hand requirements when no second hand is needed.
        if(!weapon.item && !program.dualWield &&
            (program.requiredType=="weap" || program.requiredType=="mele" || program.requiredType=="h2h")) return true;
        const auto matches=[&](const std::string &type) {return !type.empty() && std::find(weapon.types.begin(),weapon.types.end(),type)!=weapon.types.end();};
        if(matches(program.requiredType)) return true;
        if(skill.pd2) for(const auto &type:skill.pd2->weaponTypes) if(matches(type)) return true;
        return false;
    };
    if(program.smite && !p.totals.equipment.shield) return {DomainStatus::Unavailable,{}};
    if(p.persistent.player.mana<skill.manaCost && (program.frenzy || (program.spear && program.spear->attackWithoutMana) || skill.effect==SkillBehavior::PoisonDagger)) return attack(actor,request,0);
    const auto *weapon=program.commonAttack?commonAttackWeapon(p.totals.equipment,program.thrown,program.leftHand):std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return !w.leftHand && (program.smite ||
            matchesWeapon(w));});
    if(!weapon || weapon==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount || weapon->fasterAttack<=-120 ||
        (program.thrown && !weapon->throwable) || p.persistent.player.mana<skill.manaCost) return {DomainStatus::Unavailable,{}};
    if((program.commonAttack || program.dualWield) && (weapon->ranged || program.thrown)) {
        if(!weapon->projectile) return {DomainStatus::Unavailable,{}};
        skill=commonProjectileSkill(skill,*weapon->projectile);
    }
    const WeaponDamage *offhand=nullptr;
    if(program.dualWield) {
        const auto last=p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount;
        const auto second=std::find_if(p.totals.equipment.weapons.begin(),last,[&](const auto &w){return w.leftHand && w.item &&
            matchesWeapon(w) && (!program.thrown || w.throwable);});
        if(second==last || !weapon->item) return {DomainStatus::Unavailable,{}};
        offhand=&*second;
    }
    if(program.whirlwind) for(const auto &other:p.totals.equipment.weapons) if(other.item && other.leftHand && matchesWeapon(other)) offhand=&other;
    if(skill.assassin && skill.assassin->program==AssassinProgram::ElementalCharge)
        for(const auto &other:p.totals.equipment.weapons)
            if(other.item && other.leftHand && matchesWeapon(other)) offhand=&other;
    const bool fend=program.spear && program.spear->kind==SpearSkillSpec::Kind::Fend;
    const bool zeal=skill.effect==SkillBehavior::Zeal || (skill.druid && skill.druid->program==DruidProgram::Fury);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    Vec destination;EntityId target;uint8_t targetType=1;
    if(const auto *unit=std::get_if<UnitTarget>(&*request.target)) {
        const auto victim=participants.find(unit->id);
        if(unit->type>1 || bool(victim.player)!=(unit->type==0) || !victim.aliveIn(actor.area) || !participants.canHarm(actor.actor,unit->id)) return {DomainStatus::InvalidRequest,{}};
        destination=victim.position();target=unit->id;targetType=unit->type;
        if((program.chargeVelocity>0 || program.whirlwind) && meleeDistance(p.position,2,destination,victim.size())<=weapon->rangeAdder+1) return attack(actor,request,0);
        if(!program.whirlwind && program.chargeVelocity==0 && (!weapon->ranged || program.smite) && !program.thrown &&
            !playerMeleeReach(area.definition.collision,p.position,destination,victim.size(),weapon->rangeAdder+int(victim.moving()))) {
            if(request.stationary) return {DomainStatus::Unavailable,{}};
            const auto movement=ports_.movement.execute(actor,{MovementAction::Move,destination,true,{},MotionTarget{unit->id,unit->type}});
            return {movement==CommandStatus::Applied?DomainStatus::Conflict:DomainStatus::Unavailable,{}};
        }
    } else {
        if(!weapon->ranged && !program.thrown && !fend && !zeal && !program.commonAttack && !program.chargeVelocity && !program.whirlwind) return {DomainStatus::InvalidRequest,{}};
        const auto &point=std::get<PointTarget>(*request.target);
        if(point.area!=actor.area || point.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        destination=point.position;
    }
    if(!std::isfinite(destination.x) || !std::isfinite(destination.y) || std::abs(destination.x-p.position.x)>50 || std::abs(destination.y-p.position.y)>50 ||
        destination.x<0 || destination.y<0 || destination.x>=area.definition.collision.width || destination.y>=area.definition.collision.height) return {DomainStatus::InvalidRequest,{}};
    const auto mode=program.mode.empty()?(program.thrown?"th":"a1"):program.mode.c_str();
    int modeRate{};
    if(skill.pd2 && skill.pd2->startFunction==79) {
        auto input=formulaInput(p);SkillFormulaValues values(*rules.formulas,input);
        for(const auto &write:values.auraStats(id,effectiveRank,6,true))
            if(write.key.id==rules.stats->names.at("attackrate")) modeRate=nativeAdd(modeRate,write.value);
    }
    AttackAnimation a;WeaponAttackTiming timing;
    const auto *shape=activeShape(rules,ports_.effects.unitStates(actor.actor,actor.tick));
    if(shape) {
        const auto clock=shapeActionTiming(p,*shape,mode);if(!clock) return {DomainStatus::Unavailable,{}};
        timing=*clock;a={clock->frames,clock->speed,clock->actionFrame,0,clock->events};
    } else {
        const auto animation=rules.weaponAnimations.find(std::string(mode)+p.totals.equipment.animationClass);
        if(animation==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
        a=animation->second;timing={mode,a.frames,effectiveAttackSpeed(a.speed,weapon->fasterAttack,weapon->baseSpeed,nativeAdd(p.totals.character.combat.attackRate,modeRate)),a.actionFrame,a.startFrame};
    }
    std::vector<int> hits{timing.actionTick()};
    std::optional<ActionTimeline> timeline;
    auto flags=a.events;
    const auto sequenceFlags=[&](const auto &sequence) {
        flags.clear();for(const auto &frame:sequence) flags.push_back(frame.hit?1:0);
    };
    if(skill.assassin && program.sequence==16) {
        const auto sequence=assassinSequence(program.sequence,p.totals.equipment.animationClass);
        if(sequence.empty()) return {DomainStatus::Unavailable,{}};
        timing.sequence=true;timing.frames=int(sequence.size());timing.startFrame=0;
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
        hits=weaponSequenceHitTicks(sequence,timing.speed);
        sequenceFlags(sequence);
    }
    if(program.sequence==4 || program.sequence==10 || program.sequence==11 || program.sequence==15) {
        timing.sequence=true;
        const auto sequence=barbarianSequence(program.sequence,p.totals.equipment.animationClass);
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
        timing.frames=int(sequence.size());timing.startFrame=0;hits=weaponSequenceHitTicks(sequence,timing.speed);
        sequenceFlags(sequence);
        if(program.sequence==4) flags[10]=3;
        if(hits.empty()) return {DomainStatus::Unavailable,{}};
    }
    if(program.spear && (program.spear->kind==SpearSkillSpec::Kind::Jab || program.spear->kind==SpearSkillSpec::Kind::Impale)) {
        timing.sequence=true;
        const auto sequence=amazonWeaponSequence(program.spear->kind==SpearSkillSpec::Kind::Jab?1:8,p.totals.equipment.animationClass);
        if(sequence.frames.empty()) return {DomainStatus::Unavailable,{}};
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
        timing.frames=int(sequence.frames.size());timing.startFrame=0;hits=weaponSequenceHitTicks(sequence.frames,timing.speed);
        sequenceFlags(sequence.frames);
        if(program.spear->kind==SpearSkillSpec::Kind::Jab) {
            const bool two=p.totals.equipment.animationClass=="2ht";
            for(const auto index:two?std::array{1,8,15}:std::array{1,7,13}) flags[size_t(index)]=3;
        }
    }
    const bool strafe=program.bow && program.bow->strafe;
    if(!program.whirlwind && !program.chargeVelocity) {
        timeline=playerActionTimeline(timing.frames,timing.speed,timing.startFrame,flags);
        hits.clear();
        for(const auto &event:timeline->events) if(event.event==1 || event.event==2) {
            hits.push_back(event.tick);
            if(!timing.sequence) break;
        }
        if(hits.empty()) hits.push_back(timeline->end);
    }
    int volleyDuration=timeline?timeline->end:timing.durationTicks();
    int rewindCount=0;
    if(program.whirlwind) {
        const float speed=float(p.definition.runVelocity)*25.f/16.f*float(std::max(50,p.totals.character.velocityPercent))/100.f;
        if(speed<=0 || (destination-p.position).length()<.001f || !area.definition.collision.nativeMovementSegment(p.position,destination,{0x804,2})) return {DomainStatus::Unavailable,{}};
        volleyDuration=int(std::ceil((destination-p.position).length()*25.f/speed))+1;hits={4};
    }
    if(strafe || fend || zeal) {
        const auto targets=volleyTargets(ports_,actor,p,skill);
        // St08/09/37 keep an explicit target; only an absent target invokes
        // the GUID finder. The subsequent Do callback checks current range.
        if(!target) target=nativeSkillSuccessor(targets,UINT32_MAX,ports_.monsters.nativeIdentities());
        const int minimum=strafe?volleyFormula(p,skill,"calc3"):0;
        const int maximum=volleyFormula(p,skill,"calc1");
        rewindCount=zeal?maximum:strafe?
            std::min(maximum,std::max(int(targets.size()),std::min(minimum,maximum))):
            std::min(int(targets.size()),maximum);
        if(!target || rewindCount<=0) return {DomainStatus::Unavailable,{}};
        destination=participants.find(target).position();targetType=participants.find(target).player?0:1;
    }
    auto prior=state_.casts.find(actor.actor);
    if(prior!=state_.casts.end() && skill.delayFrames>0 && prior->second.cooldownUntil>actor.tick) return {DomainStatus::Conflict,{}};
    std::shared_ptr<WeaponMeleeExecution> weaponStart;
    if(program.allocatesMeleeAtStart()) {
        Release start{actor,skill,{}, {actor.area,actor.areaGeneration,destination},target,actor.tick};
        start.weapon=*weapon;start.unitType=targetType;
        const auto prepared=startWeaponMelee(start,actor);if(!prepared) return prepared;
        weaponStart=start.weaponMelee;
    }
    std::optional<effects::PotionPlan> attackState;
    if(program.attackState.id>=0) {
        if(!p.rules.skills->formulas || !p.rules.skills->stats) return {DomainStatus::Unavailable,{}};
        auto context=formulaInput(p);SkillFormulaValues formulas(*p.rules.skills->formulas,context);
        const auto &rules=*p.rules.skills->stats;
        CombatEffectSpec effect;effect.state=program.attackState;
        effect.source={CombatEffectSource::Skill,actor.actor,id,effectiveRank};
        effect.nativeState=true;effect.refreshPassivesOnRemoval=true;effect.nativeValues.emplace();
        effect.stacking=program.whirlwind?EffectStacking::SkillLevel:EffectStacking::ReuseState;
        std::vector<StatValue> writes;
        const auto append=[&](const auto &stats) {for(const auto &write:stats) {
            writes.push_back(write);
            if(write.key.id==rules.names.at("attackrate")) writes.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
        }};
        if(skill.pd2 && skill.pd2->startFunction==78) {
            // Current St78 (0x3B4860) owns a mode-only state 224. Its
            // splash stat samples the old charges, before Do9 adds a stack.
            const auto amount=formulas.calculation(FormulaDomain::Skill,id,effectiveRank,"calc2");
            if(!amount) return {DomainStatus::Unavailable,{}};
            int32_t splash=*amount;
            if(skill.weapon->frenzy) {
                splash=0;
                if(const auto states=ports_.effects.unitEffects(actor.actor))
                    if(const auto previous=states->findState(program.frenzyState.id))
                        if(const auto stats=combatEffectNativeValues(previous->spec)) {
                            const auto count=StatSnapshot{*stats}.get({rules.names.at("skill_frenzy"),0});
                            const auto divisor=formulas.calculation(FormulaDomain::Skill,id,effectiveRank,"calc4");
                            if(!divisor) return {DomainStatus::Unavailable,{}};
                            splash=pd2MultiplyDivide(*amount,count,uint32_t(*divisor));
                        }
            } else if(skill.assassin) {
                splash=0;
                for(const auto &charge:ports_.effects.progressiveCharges(actor.actor)) if(charge.skill==id && charge.count<3)
                    splash=nativeMultiply(*amount,charge.count);
            }
            writes.emplace_back(StatKey{rules.names.at("inc_splash_radius"),0},splash);
        } else append(formulas.auraStats(id,effectiveRank,6,true));
        if(program.whirlwind) {
            const auto length=formulas.calculation(FormulaDomain::Skill,id,effectiveRank,"auralencalc");
            if(!length) return {DomainStatus::Unavailable,{}};
            if(*length) effect.duration=uint32_t(*length);
            append(formulas.passiveBuffStats(id,effectiveRank));
            writes.emplace_back(StatKey{rules.names.at("modifierlist_skill"),0},id);
            writes.emplace_back(StatKey{rules.names.at("modifierlist_level"),0},effectiveRank);
        } else if(program.attackDurationMaximum>0) {
            const auto length=formulas.calculation(FormulaDomain::Skill,id,effectiveRank,"calc2");
            if(!length) return {DomainStatus::Unavailable,{}};
            effect.duration=uint32_t(*length>0?*length:10);
            effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
        } else effect.modeActionRevision=actor.sequence;
        auto prepared=ports_.effects.skillState(actor,std::move(effect),writes);if(!prepared) return {prepared.status,{}};
        attackState=std::move(*prepared.value);
        if(program.whirlwind) {
            CombatEffectSpec moving;moving.state=program.movingState;moving.nativeState=true;
            moving.modeActionRevision=actor.sequence;moving.source={CombatEffectSource::Skill,actor.actor,id,effectiveRank};
            auto &states=attackState->next.players[actor.actor].states;
            if(!states.apply(std::move(moving),actor.tick).accepted) return {DomainStatus::Conflict,{}};
            attackState->transient=effects::projectEffectAttributes(states,actor.tick);
            attackState->publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,program.movingState.id,true});
        }
    }
    std::shared_ptr<DruidMeleeExecution> druidStart;
    if(skill.druid && (skill.druid->startFunction==56 || skill.druid->startFunction==57 || skill.druid->startFunction==58)) {
        Release start{actor,skill,{}, {actor.area,actor.areaGeneration,destination},target,actor.tick};
        start.weapon=*weapon;start.unitType=targetType;
        const auto prepared=startDruidMelee(start,actor);if(!prepared) return prepared;
        druidStart=start.druidMelee;
    }
    auto cost=ports_.inventory.weaponCost(actor,*weapon,skill,!program.manaOnRelease,strafe,0,attackState?std::optional{attackState->transient}:std::nullopt);if(!cost) return {cost.status,{}};
    if(attackState) {
        auto change=std::move(cost.value->change);
        auto record=attackState->character;if(!program.manaOnRelease) record.mana-=skill.manaCost;
        std::visit([&](auto &edit) {
            using Edit=std::decay_t<decltype(edit)>;
            if constexpr(std::is_same_v<Edit,transactions::CharacterEdit>) edit.player=record;
            else if constexpr(std::is_same_v<Edit,transactions::InventoryEdit>) edit.character=record;
            if constexpr(std::is_same_v<Edit,transactions::CharacterEdit> || std::is_same_v<Edit,transactions::InventoryEdit>) {
                edit.resourceBase=attackState->resourceBase;edit.publicFacts=attackState->publicFacts;edit.transient=attackState->transient;
                if(!program.manaOnRelease) edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};
            }
        },change);
        cost=ports_.transactions.prepare(std::move(change));if(!cost) return {cost.status,{}};
    }
    const auto fact=AttackFact{actor.actor,target,0,targetType,actor.area,p.position,destination,actor.sequence,uint16_t(id),uint8_t(effectiveRank),program.whirlwind};
    if(auto *edit=std::get_if<transactions::CharacterEdit>(&cost.value->change)) edit->publicFacts.emplace_back(fact);
    else std::get<transactions::InventoryEdit>(cost.value->change).publicFacts.emplace_back(fact);
    Release release{actor,skill,{}, {actor.area,actor.areaGeneration,destination},target,actor.tick+uint64_t(hits.front())};
    release.druidMelee=std::move(druidStart);
    release.weaponMelee=std::move(weaponStart);
    release.unitType=targetType;release.weapon=*weapon;release.manaPaid=!program.manaOnRelease;release.weaponHits=std::move(hits);
    if(offhand) release.offhand=*offhand;
    release.weaponSpeed=timing.speed;release.weaponFrames=timing.frames;release.weaponRollback=program.rollbackPercent;
    release.rewindRemaining=rewindCount;release.animationAnchor=int64_t(actor.tick);release.animationEvents=std::move(flags);
    if(program.whirlwind) {
        release.whirlwind=true;release.tick=actor.tick+1;release.nextWhirlHit=actor.tick+4;
        release.unit={};
        release.chargeSpeed=float(p.definition.runVelocity)*25.f/16.f*float(std::max(50,p.totals.character.velocityPercent))/100.f;

    }
    if(program.smite) release.shield=p.totals.equipment.shield;
    if(program.chargeVelocity>0) {
        release.charging=true;release.tick=actor.tick+1;
        if(skill.pd2 && skill.pd2->doFunction==180) {
            release.pd2Execution=std::make_shared<Pd2CastExecution>();
            release.pd2Execution->chargeDistance=unitCoordinateDistance(int(p.position.x),int(p.position.y),int(destination.x),int(destination.y));
        }
        release.chargeSpeed=float(p.definition.runVelocity)*25.f/16.f*float(program.chargeVelocity+std::max(50,p.totals.character.velocityPercent))/100.f;
    }
    auto cast=Cast{actor.actor,uint16_t(id),actor.tick,actor.sequence,actor.tick+uint64_t(volleyDuration),actor.area,target};
    cast.usesSkillPointer=true;
    std::optional<State::PreparedTimeline> preparedTimeline;
    if(timeline) {
        preparedTimeline=state_.prepareTimeline(actor.actor,actor.tick,*timeline);
        cast.timeline=preparedTimeline->generation;release.eventDriven=true;
    } else {
        preparedTimeline=state_.prepareProgramTimeline(actor.actor,actor.tick);
        cast.timeline=preparedTimeline->generation;release.eventDriven=true;
    }
    if(program.chargeVelocity>0) cast.until=actor.tick+1;
    if(prior!=state_.casts.end()) cast.cooldownUntil=prior->second.cooldownUntil;
    // Allocate candidate nodes before publishing inventory/mana/action as one transaction.
    std::map<EntityId,Release> preparedRelease;preparedRelease.emplace(actor.actor,std::move(release));
    std::map<EntityId,Cast> preparedCast;preparedCast.emplace(actor.actor,cast);
    auto preparedUse=state_.prepareUsedSkill(actor.actor,{id,source->charged?source->charged->item.id:EntityId{}});
    removeSkillUse(preparedUse.mapped(),cost.value->player->totals.clearedSkillUses);
    const auto committed=ports_.transactions.commit(std::move(*cost.value));if(!committed) return committed;
    if(attackState) ports_.effects.commit(std::move(*attackState));
    runtime_->druidStarts.erase(actor.actor);
    if(const auto old=runtime_->weaponStarts.find(actor.actor);old!=runtime_->weaponStarts.end() &&
       old->second.weaponMelee!=preparedRelease.at(actor.actor).weaponMelee && old->second.weaponMelee && old->second.weaponMelee->occurrence)
        ports_.combat.discardNativeMelee(actor.actor,*old->second.weaponMelee->occurrence);
    runtime_->weaponStarts.erase(actor.actor);
    if(releases_.contains(actor.actor)) ports_.companions.cancel(actor.actor);
    releases_.erase(actor.actor);releases_.merge(preparedRelease);state_.casts.erase(actor.actor);state_.casts.merge(preparedCast);
    state_.commitUsedSkill(std::move(preparedUse));
    if(preparedTimeline) state_.commitTimeline(std::move(*preparedTimeline));
    else state_.cancelTimeline(actor.actor);
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::weaponRelease(Release &pending,const ActorContext &actor,Vec destination) {
    if(pending.druidMelee) return activateDruidMelee(pending,actor,destination);
    if(pending.weaponMelee) return drainWeaponMelee(pending,actor);
    if(pending.skill.weapon->doFunction==67 && pending.animationAnchor!=int64_t(actor.tick)-7) {
        // Do67 first rewinds to eventFrame+1 on entering melee, then to
        // eventFrame before allocating the hit. Retain this entered callback
        // if its new timeline generation is published before output accepts.
        const auto timeline=rewoundPlayerActionTimeline(pending.weaponFrames,pending.weaponSpeed,7,pending.animationEvents);
        auto prepared=state_.prepareTimeline(actor.actor,actor.tick,timeline);
        auto &cast=state_.casts.at(actor.actor);cast.timeline=prepared.generation;cast.until=actor.tick+uint64_t(timeline.end);
        pending.animationAnchor=int64_t(actor.tick)-7;pending.nativeCallbackStarted=true;
        state_.commitTimeline(std::move(prepared));
    }
    const auto advanceVolley=[&] {
        if(pending.skill.weapon->frenzy && pending.nativeContact) {
            const auto &skill=pending.skill;
            state_.skillParameter(actor.actor,{skill.sourceId,skill.charge?skill.charge->item.id:EntityId{}},0,pending.nativeContact->hit?1:0);
            pending.frenzyPrepared=false;
        }
        pending.nativeContact.reset();
        if(pending.rewindRemaining<=1) return;
        const auto *player=ports_.players.find(actor.player);if(!player) return;
        const auto next=nativeSkillSuccessor(volleyTargets(ports_,actor,*player,pending.skill),ports_.monsters.nativeIdentities().guid(pending.unit),ports_.monsters.nativeIdentities());
        if(next) {pending.unit=next;pending.unitType=ports_.players.findActor(next)?0:1;}
        else pending.rewindRemaining=1;
    };
    if(pending.skill.pd2 && pending.skill.pd2->doFunction==180) {
        const auto prepared=pd2ChargeBeforeHit(ports_,pending,actor);if(prepared!=DomainStatus::Applied) return prepared;
    }
    if(pending.nativeMeleeOccurrence) {
        if(ports_.combat.advanceAuraPulse(actor.actor,*pending.nativeMeleeOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        if(pending.skill.pd2 && pending.skill.pd2->doFunction==174) {
            const auto follow=pd2VengeanceAfterHit(ports_,pending,actor,runtime_->pd2Vengeance);if(follow!=DomainStatus::Applied) return follow;
        }
        if(pending.skill.pd2 && pending.skill.pd2->doFunction==175) {
            const auto follow=activatePd2(pending,actor,destination);if(follow!=DomainStatus::Applied) return follow;
        }
        pending.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;advanceVolley();return DomainStatus::Applied;
    }
    const auto &p=*ports_.players.find(actor.player);
    const auto &hand=pending.offhand && pending.nextWeaponHit%2?*pending.offhand:*pending.weapon;
    const auto found=std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return w.item==hand.item && w.weaponClass==hand.weaponClass && w.leftHand==hand.leftHand;});
    if(found==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount) return DomainStatus::Stale;
    if(pending.skill.weapon->smite && (!pending.shield || pending.shield!=p.totals.equipment.shield)) return DomainStatus::Stale;
    if(pending.skill.weapon->commonAttack && commonAttackWeapon(p.totals.equipment,pending.skill.weapon->thrown,pending.skill.weapon->leftHand)!=&*found) return DomainStatus::Stale;
    const bool strafe=pending.skill.weapon->bow && pending.skill.weapon->bow->strafe;
    const bool secondSwing=pending.offhand && !pending.skill.assassin && !pending.skill.weapon->thrown && pending.nextWeaponHit>0;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId selectedTarget=pending.unit;
    if(pending.rewindRemaining>0) {
        const auto current=participants.find(selectedTarget);
        if(!current.id() || (!strafe && !playerMeleeReach(ports_.areas.at(actor.area).definition.collision,p.position,current.position(),current.size(),found->rangeAdder)))
            selectedTarget=nativeSkillSuccessor(volleyTargets(ports_,actor,p,pending.skill),ports_.monsters.nativeIdentities().guid(pending.unit),ports_.monsters.nativeIdentities());
        if(!selectedTarget) return DomainStatus::Unavailable;
        destination=participants.find(selectedTarget).position();
    } else if(secondSwing) {
        const auto target=nativeSkillSuccessor(volleyTargets(ports_,actor,p,pending.skill),ports_.monsters.nativeIdentities().guid(pending.unit),ports_.monsters.nativeIdentities());
        if(!target) return DomainStatus::Unavailable;
        selectedTarget=target;destination=participants.find(target).position();
    }
    const bool projectile=(found->ranged && !pending.skill.weapon->smite) || pending.skill.weapon->thrown;
    if(projectile && pending.skill.weapon->dualWield) {
        if(!found->projectile) return DomainStatus::Unavailable;
        pending.skill=commonProjectileSkill(pending.skill,*found->projectile);
    }
    const bool nativeMelee=!projectile && (pending.skill.assassin || pending.skill.druid || pending.skill.weapon->nativeMelee());
    if(pending.skill.weapon->frenzy && !pending.frenzyPrepared) {
        const auto &skill=pending.skill;
        if(state_.skillParameter(actor.actor,{skill.sourceId,skill.charge?skill.charge->item.id:EntityId{}},0)) {
            const auto applied=ports_.effects.frenzy(actor,skill);if(!applied) return applied.status;
        }
        pending.frenzyPrepared=true;
    }
    auto event=nativeMelee?DomainResult<reactions::ItemEventPlan>{DomainStatus::Applied,reactions::ItemEventPlan{}}:
        ports_.itemTriggers.prepare(actor,{ItemSkillEvent::Attack},selectedTarget,destination);
    if(!event) return event.status;
    if(pending.skill.charge) {
        const auto source=p.persistent.inventory.items.find(pending.skill.charge->item.id);
        if(source==p.persistent.inventory.items.end() || !p.totals.activeEquipment.contains(source->first)) return DomainStatus::Stale;
        pending.skill.charge->item=source->second.handle();
    }
    std::optional<transactions::Plan> cost;
    if(projectile && pending.skill.pd2 && !pending.skill.weapon->commonAttack) {
        pending.weapon=*found;pending.unit=selectedTarget;
        const auto result=activatePd2(pending,actor,destination);
        if(result==DomainStatus::Applied) {ports_.itemTriggers.commit(std::move(*event.value),true);advanceVolley();}
        return result;
    }
    if(!pending.manaPaid || (projectile && !strafe && !pending.skill.weapon->noAmmo)) {
        auto prepared=ports_.inventory.weaponCost(actor,*found,pending.skill,!pending.manaPaid,projectile && !strafe);
        if(!prepared) return prepared.status;
        cost=std::move(*prepared.value);
    }
    if(projectile) {
        WeaponSkillDamage snapshot;snapshot.weapon=*found;snapshot.level=p.persistent.player.level;
        missiles::Spawn request{actor,pending.skill,{},destination,true};request.weapon=snapshot;request.cost=std::move(cost);
        request.guidedTarget=selectedTarget;
        const auto result=ports_.missiles.spawn(request).status;
        if(result==DomainStatus::Applied) {pending.unit=selectedTarget;ports_.itemTriggers.commit(std::move(*event.value),true);advanceVolley();}
        return result;
    }
    if(pending.skill.weapon->commonAttack && !selectedTarget) {
        const auto result=cost?ports_.transactions.commit(std::move(*cost)).status:DomainStatus::Applied;
        if(result==DomainStatus::Applied) ports_.itemTriggers.commit(std::move(*event.value),true);
        return result;
    }
    const auto target=participants.find(selectedTarget);
    const auto &area=ports_.areas.at(actor.area);
    if(!target.aliveIn(actor.area) || !participants.canHarm(actor.actor,selectedTarget) ||
        (!nativeMelee && !playerMeleeReach(area.definition.collision,p.position,target.position(),target.size(),found->rangeAdder))) return DomainStatus::Unavailable;
    WeaponSkillDamage snapshot;snapshot.weapon=*found;snapshot.level=p.persistent.player.level;
    missiles::Spawn request{actor,pending.skill,{},destination,true};request.weapon=snapshot;request.cost=std::move(cost);
    if(nativeMelee) {
        if(!pending.nativeContact) pending.nativeContact=std::make_shared<combat::NativeSkillContact>();
        request.nativeMeleeContact=pending.nativeContact;
    }
    const auto result=ports_.missiles.directContact(request,{selectedTarget});
    if(!result) return result.status;
    pending.unit=selectedTarget;ports_.itemTriggers.commit(std::move(*event.value),true);
    if(result.value->occurrence) {
        pending.nativeMeleeOccurrence=result.value->occurrence;
        pending.nativeCallbackStarted=true;
        if(ports_.combat.advanceAuraPulse(actor.actor,*pending.nativeMeleeOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        if(pending.skill.pd2 && pending.skill.pd2->doFunction==174) {
            const auto follow=pd2VengeanceAfterHit(ports_,pending,actor,runtime_->pd2Vengeance);if(follow!=DomainStatus::Applied) return follow;
        }
        if(pending.skill.pd2 && pending.skill.pd2->doFunction==175) {
            const auto follow=activatePd2(pending,actor,destination);if(follow!=DomainStatus::Applied) return follow;
        }
        pending.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;
    }
    advanceVolley();return DomainStatus::Applied;
}
}
