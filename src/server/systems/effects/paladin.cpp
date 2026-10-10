#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <cmath>
namespace d2x::server::effects {
namespace {
TransientAttributes projection(const CombatEffectSet &states,uint64_t tick) {
    TransientAttributes p;p.modifiers=states.modifiers(tick);
    for(const auto &e:states.entries()) if(e.activeAt(tick)) p.states.insert(e.spec.state.id);
    return p;
}
std::vector<std::pair<int,int64_t>> native(const SkillRules &rules,const CharacterModifiers &m) {
    const std::pair<std::string_view,int> values[]{
        {"damagepercent",m.combat.damagePercent},{"item_tohit_percent",m.combat.attackRatingPercent},
        {"attackrate",m.combat.attackRate},{"other_animrate",m.otherAnimationRate},{"velocitypercent",m.velocityPercent},
        {"skill_armor_percent",m.combat.defensePercent},{"damageresist",m.combat.physicalResist},
        {"fireresist",m.fireResist},{"coldresist",m.coldResist},{"lightresist",m.lightningResist},
        {"maxfireresist",m.combat.fireMaxResist},{"maxcoldresist",m.combat.coldMaxResist},{"maxlightresist",m.combat.lightningMaxResist},
        {"firemindam",m.combat.fireMinimum},{"firemaxdam",m.combat.fireMaximum},
        {"coldmindam",m.combat.coldMinimum},{"coldmaxdam",m.combat.coldMaximum},
        {"lightmindam",m.combat.lightningMinimum},{"lightmaxdam",m.combat.lightningMaximum},
        {"thorns_percent",m.combat.thornsPercent},{"skill_concentration",m.combat.concentrationChance},
        {"manarecoverybonus",m.combat.manaRecovery},{"skill_staminapercent",m.staminaPercent},{"staminarecoverybonus",m.staminaRecoveryBonus}};
    std::vector<std::pair<int,int64_t>> result;
    for(const auto &[name,value]:values) if(value) result.emplace_back(rules.nativeStats.at(std::string(name)),value);
    return result;
}
void removeDefinition(CombatEffectSet &states,EntityId source,int skill) {
    std::vector<EffectHandle> handles;
    for(const auto &e:states.entries()) if(e.spec.source.entity==source && e.spec.source.definition==skill) handles.push_back(e.handle);
    for(const auto h:handles) states.remove(h);
}
}
DomainResult<> System::removeAura(const ActorContext &actor,int skill) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &r=next.players[actor.actor];removeDefinition(r.states,actor.actor,skill);
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    edit.transient=projection(r.states,actor.tick);
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) state_.players.swap(next.players);return result;
}
CharacterModifiers System::stateModifiers(EntityId id,int state,uint64_t tick) const {
    const CombatEffectSet *effects=nullptr;
    if(const auto it=state_.players.find(id);it!=state_.players.end()) effects=&it->second.states;
    else if(const auto it=units_.find(id);it!=units_.end()) effects=&it->second.states;
    if(effects) for(const auto &e:effects->entries()) if(e.spec.state.id==state && e.activeAt(tick)) return e.spec.modifiers;
    return {};
}
DomainResult<> System::paladinAura(const ActorContext &actor,const AuraDefinition &a,EntityId id,uint64_t duration,bool owner,bool healing) {
    const auto *caster=ports_.players.find(actor.player);if(!caster || !caster->rules.skills) return {DomainStatus::InvalidActor,{}};
    const combat::Participants participants{ports_.players,ports_.monsters};
    const auto recipientView=participants.find(id);
    if(!recipientView.aliveIn(actor.area) || (id!=actor.actor &&
       !(a.hostile?participants.canHarm({caster,nullptr},recipientView):participants.allied({caster,nullptr},recipientView,ports_.social))))
        return {DomainStatus::InvalidActor,{}};
    CombatEffectSpec spec;spec.state=owner?a.ownerState:a.state;
    spec.source={CombatEffectSource::Skill,actor.actor,a.skill,a.rank};spec.duration=duration;spec.stacking=EffectStacking::AuraLevel;
    spec.modifiers=owner?auraOwnerModifiers(a):a.modifiers;
    const PlayerState *recipient=recipientView.player;
    if(recipient) {
        if(!recipient->entered || recipient->area!=actor.area || recipient->persistent.player.hp<=0 ||
           (recipient!=caster && (a.hostile || !ports_.social.sameParty(actor.player,recipient->player)))) return {DomainStatus::InvalidActor,{}};
        auto next=state_;auto &r=next.players[id];
        if(r.states.size()>=128 && !r.states.hasState(spec.state.id,actor.tick)) return {DomainStatus::Capacity,{}};
        if(spec.state.id>=0) r.states.apply(spec,actor.tick);
        if(a.harmfulDurationPercent<100) {
            r.states.shortenCurableCurses(actor.tick,a.harmfulDurationPercent);
            if(r.poison && r.poison->until>actor.tick) {
                r.poison->until=actor.tick+(r.poison->until-actor.tick)*uint64_t(std::max(0,a.harmfulDurationPercent))/100;
                std::optional<CombatEffectSpec> poison;
                for(const auto &effect:r.states.entries()) if(effect.spec.state.id==r.poison->damage.state) {poison=effect.spec;break;}
                if(poison) {poison->duration=std::max<uint64_t>(1,r.poison->until-actor.tick);r.states.apply(std::move(*poison),actor.tick);}
            }
        }
        const ActorContext target{recipient->player,recipient->actor,recipient->area,ports_.areas.at(recipient->area).generation,0,actor.tick};
        transactions::CharacterEdit edit{target,recipient->inventoryRevision,recipient->characterRevision,recipient->persistent.player};
        if(healing && a.lifePerPulse>0) edit.player.hp=std::min(float(recipient->totals.character.maxLife),edit.player.hp+a.lifePerPulse);
        if(owner && a.manaPerPulse>0) {
            const int state=caster->rules.character->noManaRegenState;r.states.removeState(state);
            if(healing) {
                if(edit.player.mana<a.manaPerPulse) return {DomainStatus::Unavailable,{}};
                edit.player.mana-=a.manaPerPulse;
                CombatEffectSpec stop;stop.state.id=state;stop.source=spec.source;stop.duration=duration;r.states.apply(std::move(stop),actor.tick);
            }
        }
        edit.transient=projection(r.states,actor.tick);
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) state_.players.swap(next.players);return result;
    }
    const auto *m=recipientView.monster;if(!m) return {DomainStatus::InvalidActor,{}};
    if(a.skill==114) {
        if(m->rule.coldEffect>=0) return {DomainStatus::Unavailable,{}};
        spec.modifiers.velocityPercent=std::max(spec.modifiers.velocityPercent,m->rule.coldEffect);
        spec.modifiers.combat.attackRate=std::max(spec.modifiers.combat.attackRate,m->rule.coldEffect);
        spec.modifiers.otherAnimationRate=spec.modifiers.combat.attackRate;
    }
    if(a.skill==123) {
        auto existing=unitModifiers(id,actor.tick);
        const auto previous=stateModifiers(id,spec.state.id,actor.tick);
        existing.fireResist-=previous.fireResist;existing.lightningResist-=previous.lightningResist;existing.coldResist-=previous.coldResist;
        if(m->rule.resistances[2]+existing.fireResist>=100) spec.modifiers.fireResist/=5;
        if(m->rule.resistances[3]+existing.lightningResist>=100) spec.modifiers.lightningResist/=5;
        if(m->rule.resistances[4]+existing.coldResist>=100) spec.modifiers.coldResist/=5;
    }
    auto next=units_;auto &unit=next[id];unit.area=actor.area;unit.converted=bool(m->conversion);
    if(spec.state.id>=0) {
        if(unit.states.size()>=128 && !unit.states.hasState(spec.state.id,actor.tick)) return {DomainStatus::Capacity,{}};
        if(!unit.states.apply(spec,actor.tick).accepted) return {DomainStatus::Applied,std::monostate{}};
        auto stats=native(*caster->rules.skills,spec.modifiers);
        if(unit.nativeStats[spec.state.id]!=stats || !unitStates(id,actor.tick).contains(spec.state.id)) {
            if(!ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,spec.state.id,true,stats}}})) return {DomainStatus::Capacity,{}};
        }
        unit.nativeStats[spec.state.id]=std::move(stats);
    }
    if(a.harmfulDurationPercent<100) unit.states.shortenCurableCurses(actor.tick,a.harmfulDurationPercent);
    ports_.monsters.velocityModifier(id,unit.states.modifiers(actor.tick).velocityPercent);
    units_.swap(next);
    if(a.harmfulDurationPercent<100) ports_.monsters.shortenPoison(id,actor.tick,a.harmfulDurationPercent);
    if(healing && a.lifePerPulse>0) ports_.monsters.heal(id,int64_t(a.lifePerPulse*256));
    if(a.skill==114) {
        auto random=ports_.random;const bool shatter=limitedRandom(random,100)<20;
        ports_.monsters.holyFreeze(id,actor.tick+duration,shatter);ports_.random=random;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::advancePaladinAuras(uint64_t tick) {
    bool blocked=false;
    const combat::Participants participants{ports_.players,ports_.monsters};
    for(const auto &[key,p]:ports_.players.all()) {
        const ActorContext actor{key,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick};
        auto &cycle=paladinCycles_[p.actor];
        const int selected=p.persistent.player.selectedSkills.at(p.persistent.player.weaponSet*2+1);
        const auto rank=p.totals.skillRanks.find(selected);
        const int effectiveRank=rank==p.totals.skillRanks.end()?0:rank->second;
        const bool available=p.entered && p.persistent.player.hp>0 && p.rules.skills && p.rules.skills->auras.contains(selected) && rank!=p.totals.skillRanks.end() && rank->second>0;
        if(cycle.skill>=0 && (!available || cycle.skill!=selected || cycle.area!=p.area || cycle.rank!=effectiveRank)) {
            if(!removeAura(actor,cycle.skill)) {blocked=true;continue;}
            cycle=PaladinCycle{};
        }
        if(!available) continue;
        const auto &rule=p.rules.skills->auras.at(selected);
        if(cycle.skill<0) {cycle.skill=selected;cycle.rank=effectiveRank;cycle.area=p.area;cycle.random=childRandom(ports_.random);cycle.next=rule.immediate?tick:tick+uint64_t(std::max(5,rule.spec.periodFrames));}
        if(!cycle.pending && tick<cycle.next) {
            if(!state_.players[p.actor].states.hasState(rule.spec.ownerState.id,tick)) {
                auto a=evaluateAura(rule.spec,effectiveRank,p.persistent.player.skillRanks,skills::mastery(p,p.rules.skills->fireMasteries),skills::mastery(p,p.rules.skills->lightningMasteries),p.totals.character.combat.coldSkillDamagePercent,p.totals.skillRanks.contains(99)?p.totals.skillRanks.at(99):0);
                a.modifiers={};a.ownerModifiers={};a.ownerDamageBonus=0;
                if(!paladinAura(actor,a,p.actor,cycle.next-tick+1,true,false)) blocked=true;
            }
            continue;
        }
        const auto &area=ports_.areas.at(p.area).definition;
        if(!cycle.pending) {
            cycle.aura=evaluateAura(rule.spec,effectiveRank,p.persistent.player.skillRanks,skills::mastery(p,p.rules.skills->fireMasteries),skills::mastery(p,p.rules.skills->lightningMasteries),p.totals.character.combat.coldSkillDamagePercent,p.totals.skillRanks.contains(99)?p.totals.skillRanks.at(99):0);
            cycle.occurrence=++paladinOccurrence_;
            cycle.targets={p.actor};cycle.target=0;
            const auto &a=cycle.aura;
            if(!a.hostile && (a.filter&1)) for(const auto &[peerId,peer]:ports_.players.all()) {
                if(peerId==key || !peer.entered || peer.area!=p.area || peer.persistent.player.hp<=0 || !ports_.social.sameParty(key,peerId) ||
                   !area.activation.nearby(p.position,peer.position) || (peer.position-p.position).length()>a.radius) continue;
                if((a.filter&0x200) && !area.collision.missileSegment(p.position,peer.position,{0x0805,1})) continue;
                cycle.targets.push_back(peer.actor);
            }
            for(const auto &[id,m]:ports_.monsters.read().actors) {
                if(m.area!=p.area || !area.activation.nearby(p.position,m.position) || (m.position-p.position).length()>a.radius || !(a.filter&2)) continue;
                if((a.filter&0x200) && !area.collision.missileSegment(p.position,m.position,{0x0805,1})) continue;
                if(a.filter&0x1000) {if(m.life>0 || !m.rewardComplete || tick<m.busyUntil || m.corpseUnavailable || !m.rule.corpseSelectable) continue;}
                else if(m.life<=0 || !m.damageable()) continue;
                if((a.filter&4) && !m.rule.undead) continue;
                if((a.filter&0x4000) && m.rule.boss) continue;
                if((a.filter&0x40000) && m.rule.primeEvil) continue;
                if(a.hostile) {if(area.town || !participants.canHarm({&p,nullptr},{nullptr,&m})) continue;}
                else if(a.skill!=124 && !participants.allied({&p,nullptr},{nullptr,&m},ports_.social)) continue;
                if(a.skill==124 && area.town) continue;
                cycle.targets.push_back(id);
            }
            bool healing=p.persistent.player.hp<p.totals.character.maxLife;
            for(const auto id:cycle.targets) if(const auto *m=ports_.monsters.find(id);m && m->life>0 && m->life<m->maximumLife) healing=true;
            if(cycle.aura.manaPerPulse>p.persistent.player.mana || !healing) cycle.aura.lifePerPulse=0;
            cycle.pending=true;
        }
        const uint64_t duration=uint64_t(std::max(5,cycle.aura.periodFrames))+1;
        while(cycle.target<cycle.targets.size()) {
            const auto id=cycle.targets[cycle.target];
            DomainResult<> result{DomainStatus::Applied,std::monostate{}};
            if(id==p.actor) result=paladinAura(actor,cycle.aura,id,duration,true,cycle.aura.lifePerPulse>0);
            else if(cycle.skill==124) {
                const auto *m=ports_.monsters.find(id);
                     if(m && m->area==p.area && m->life<=0 && m->rewardComplete && tick>=m->busyUntil &&
                         !m->corpseUnavailable && m->rule.corpseSelectable && !area.town) {
                    auto random=cycle.random;
                    if(limitedRandom(random,100)<unsigned(cycle.aura.redemptionChance)) {
                        transactions::CharacterEdit edit{actor,p.inventoryRevision,p.characterRevision,p.persistent.player};
                        edit.player.hp=std::min(float(p.totals.character.maxLife),edit.player.hp+cycle.aura.redemptionLife);
                        edit.player.mana=std::min(float(p.totals.character.maxMana),edit.player.mana+cycle.aura.redemptionMana);
                        edit.publicFacts.emplace_back(StateFact{id,1,p.area,p.rules.skills->redeemed.id,true});
                        auto plan=ports_.transactions.prepare(std::move(edit));
                        if(!plan) result={plan.status,{}};
                        else {result=ports_.transactions.commit(std::move(*plan.value));if(result) result=ports_.monsters.redeem(id);}
                    }
                    if(result.status!=DomainStatus::Capacity) cycle.random=random;
                }
            } else result=paladinAura(actor,cycle.aura,id,duration,false,cycle.aura.lifePerPulse>0);
            if(result.status==DomainStatus::Capacity) {blocked=true;break;}
            ++cycle.target;
        }
        if(cycle.target<cycle.targets.size()) continue;
        if(cycle.aura.element>=0 && cycle.aura.maximumDamage>0 && !area.town) {
            std::vector<EntityId> targets;for(const auto id:cycle.targets) if(id!=p.actor) if(const auto *m=ports_.monsters.find(id);m && m->life>0 && (cycle.skill!=114 || m->rule.coldEffect<0)) targets.push_back(id);
            auto random=cycle.random;const int64_t low=int64_t(cycle.aura.minimumDamage*256),spread=int64_t((cycle.aura.maximumDamage-cycle.aura.minimumDamage)*256);
            combat::SpellImpact pulse{p.actor,p.actor,p.area,DamageType(cycle.aura.element),low+(spread>0?limitedRandom(random,uint32_t(spread)):0),std::move(targets)};
            pulse.occurrence=(uint64_t(1)<<61)|cycle.occurrence;pulse.hitClass=uint8_t(cycle.aura.hitClass);pulse.reaction=true;pulse.unblockable=true;pulse.knockback=(cycle.aura.resultFlags&8)!=0;
            auto plan=ports_.combat.prepareSpells({pulse});if(!plan) {blocked=true;continue;}
            ports_.combat.commitSpells(std::move(*plan.value));cycle.random=random;
        }
        cycle.pending=false;cycle.targets.clear();cycle.next=tick+uint64_t(std::max(5,cycle.aura.periodFrames));
    }
    std::erase_if(paladinCycles_,[&](const auto &entry){return std::none_of(ports_.players.all().begin(),ports_.players.all().end(),[&](const auto &p){return p.second.actor==entry.first;});});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
