#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
Vec position(OnlinePoint p) {return {float(p.x),float(p.y)};}
OnlinePoint point(Vec p) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.y<0 || p.x>65535 || p.y>65535)
        throw std::runtime_error("Combat position outside native coordinates");
    return {uint16_t(std::floor(p.x)),uint16_t(std::floor(p.y))};
}
std::optional<uint8_t> life(const OnlineUnit &unit) {
    if(!unit.lifePercent) return {};
    // Full life may be 128 in an ordinary ratio packet but only 127 in
    // the hit format reserving bit 7. Do not manufacture progress when
    // switching between these two full-life encodings.
    return uint8_t(std::min(127,int(unit.lifeCarriesTriggerFlag?*unit.lifePercent&0x7f:*unit.lifePercent)));
}
}
void DebugAi::Impl::releaseCombat(Context &c) {
    if(combatOwned) {
        OnlineCombatCommand stop;stop.action=OnlineCombatCommand::Action::Stop;
        if(!c.combat.submit(stop)) note("combat-release-unavailable",c.combat.reason());
    }
    combatOwned=false;combatRun.repeating=false;
}
void DebugAi::Impl::observeCombat(Context &c) {
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(c.supplies && c.supplies->maxLife) combatMaxLife=c.supplies->maxLife;
    if(world.life && (combatLifeSamples.empty() || now-combatLifeSamples.back().at>=debugDecisionInterval))
        combatLifeSamples.push_back({now,*world.life});
    while(!combatLifeSamples.empty() && (combatLifeSamples.size()>32 || now-combatLifeSamples.front().at>std::chrono::seconds(4)))
        combatLifeSamples.pop_front();
    if(!combatTarget) return;
    const auto unit=world.units.find(*combatTarget);
    if(unit==world.units.end() || unit->second.assignmentRevision!=combatRun.assignment) return;
    const auto health=life(unit->second);
    if(health) {
        if(combatRun.lastLife && *health>*combatRun.lastLife) combatRun.healingObserved=true;
        if(!combatRun.bestLife) combatRun.bestLife=health;
        else if(*health<*combatRun.bestLife) {
            combatRun.bestLife=health;combatRun.progressAt=combatRun.strategyAt=now;
            if(combatRun.suspendedAt!=Clock::time_point{}) combatRun.suspendedAt=now;
            combatRun.repositioned=false;note("combat-net-life-progress",std::to_string(*health));
        }
        combatRun.lastLife=health;
    }
    if(unit->second.hitRevision>combatRun.hitRevision) {combatRun.hitRevision=unit->second.hitRevision;++combatRun.hits;}
}
bool DebugAi::Impl::combat(Context &c,const Goal &goal) {
    const auto &view=c.session.read();const auto now=Clock::now();
    if(!policy.enabled || c.town.read().town || view.world.npcRequested || view.world.waypointSource) return false;
    std::erase_if(bypassedThreats,[&](const auto &entry) {
        const auto unit=view.world.units.find(entry.first.first);
        if(unit==view.world.units.end() || unit->second.assignmentRevision!=entry.first.second || onlineMonsterCorpse(unit->second)) return true;
        // Elapsed time alone cannot repair the same failed attack angle.
        // Let navigation advance before another optional engagement, or retry
        // when the actual assigned target has moved to a different position.
        return now>=entry.second.retryAt && (((c.position-entry.second.observer).length()>=8 &&
            (c.position-entry.second.target).length()>policy.engage+policy.retreat) ||
            (unit->second.position && (position(*unit->second.position)-entry.second.target).length()>=4));
    });
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    const Vec offset=origin?position(*origin):Vec{};
    const auto clearLine=[&](Vec from,Vec to) {
        return map && origin && map->grid.lightSegment(from-offset+Vec{.5f,.5f},to-offset+Vec{.5f,.5f});
    };
    const auto objective=goal.kind==Kind::Hunt && goal.target?find(c,*goal.target):std::nullopt;
    const bool nearObjective=objective && (position(*objective->position)-c.position).length()<=policy.engage;
    const bool clearing=cursor<goals.size() && goals[cursor].kind==Kind::Quest && questRun.step<goals[cursor].steps.size() &&
        (goals[cursor].steps[questRun.step].clearCombat ||
         (goal.kind==Kind::Quest && goal.area==area && goals[cursor].steps[questRun.step].action==QuestAction::Clear));
    std::vector<OnlineUnit> enemies;std::optional<OnlineUnit> chosen;float nearest=float(policy.engage);
    for(const auto &[key,unit]:view.world.units) {
        if(key.type!=1 || !unit.position || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false) ||
            (!clearing && (!objective || objective->key!=key) && bypassedThreats.contains({key,unit.assignmentRevision}))) continue;
        const float distance=(position(*unit.position)-c.position).length();
        const bool visible=clearLine(c.position,position(*unit.position));
        if(distance<=policy.engage && (visible || distance<=policy.radius)) enemies.push_back(unit);
        const bool relevant=policy.scope=="clear" || clearing || nearObjective || (policy.scope=="defensive" && distance<=policy.radius);
        if(relevant && visible && distance<nearest) {nearest=distance;chosen=unit;}
    }
    // A stance change may leave the acquisition radius, and another enemy
    // may briefly be nearer. Finish the same valid engagement inside its
    // existing engage radius; the required objective still takes precedence.
    if(combatTarget) for(const auto &unit:enemies) if(unit.key==combatTarget &&
        unit.assignmentRevision==combatRun.assignment && clearLine(c.position,position(*unit.position))) {chosen=unit;break;}
    if(nearObjective && c.combat.hostile(*objective)) chosen=objective;
    if(!chosen) {
        if(combatTarget || combatOwned) {
            releaseCombat(c);retreatPoint.reset();
            const auto previous=combatTarget?view.world.units.find(*combatTarget):view.world.units.end();
            if(previous!=view.world.units.end() && previous->second.assignmentRevision==combatRun.assignment &&
                !onlineMonsterCorpse(previous->second) && c.combat.hostile(previous->second)) {
                if(combatRun.suspendedAt==Clock::time_point{}) {
                    combatRun.suspendedAt=now;note("combat-suspended","Retaining the same assigned target's recovery state; releasing input");
                }
                combatDiagnostics["status"]="suspended";return false;
            }
            cancelWalk(c);combatTarget.reset();combatRun={};note("combat-clear");
        }
        return false;
    }
    const bool required=clearing || (goal.kind==Kind::Hunt && objective && chosen->key==objective->key);
    bool boss=false;
    if(const auto id=c.combat.monsterClass(*chosen))
        if(const auto row=findCompiledMonsterRow(definitions->monsters,*id)) boss=definitions->monsters.number(*row,"boss").value_or(0)!=0;
    unsigned staticRank=0;bool learnedStatic=false;
    for(const auto &entry:c.combat.skills()) if(entry.id==definitions->staticFieldSkill) {
        staticRank=entry.level;learnedStatic=entry.classSkill && entry.base>0;
    }
    const auto staticRange=definitions->staticRadius(staticRank);
    std::optional<int> staticStop;
    if(view.load.difficulty && *view.load.difficulty<definitions->difficulties.rows().size())
        if(const auto floor=definitions->difficulties.number(*view.load.difficulty,"StaticFieldMin");floor && *floor>=0 && *floor<90)
            staticStop=std::max(25,*floor+5);
    if(combatTarget!=chosen->key || combatRun.assignment!=chosen->assignmentRevision || combatRun.skills.empty()) {
        releaseCombat(c);cancelWalk(c);combatTarget=chosen->key;combatRun={};
        combatRun.assignment=chosen->assignmentRevision;combatRun.progressAt=combatRun.strategyAt=now;
        combatRun.bestLife=combatRun.lastLife=life(*chosen);combatRun.hitRevision=chosen->hitRevision;
        const auto primary=policy.skill.value_or(definitions->skill("Attack"));const auto health=life(*chosen);
        if(policy.staticField && boss && learnedStatic && staticRank>=5 && staticRange && *staticRange>3 && staticStop && health &&
            int(*health)*100/127>=std::max(60,*staticStop+10) && primary!=definitions->staticFieldSkill &&
            knownSkill(c,definitions->staticFieldSkill)) {
            combatRun.skills.push_back(definitions->staticFieldSkill);combatRun.staticOpening=true;
            note("static-field-opening",std::to_string(staticRank)+" effective ranks / radius "+std::to_string(*staticRange));
        }
        combatRun.skills.push_back(primary);
        for(const auto skill:policy.fallbackSkills) if(std::find(combatRun.skills.begin(),combatRun.skills.end(),skill)==combatRun.skills.end())
            combatRun.skills.push_back(skill);
        if(travelPhase==TravelPhase::CrossWait && !legUnit) {travelPhase=TravelPhase::Route;legDestination.reset();}
        note("combat-target",std::to_string(chosen->key.id));
    }
    if(combatRun.suspendedAt!=Clock::time_point{}) {
        const auto suspended=now-combatRun.suspendedAt;
        combatRun.progressAt+=suspended;combatRun.strategyAt+=suspended;
        if(combatRun.approachUntil!=Clock::time_point{}) combatRun.approachUntil+=suspended;
        combatRun.suspendedAt={};note("combat-resumed","Continuing the same assigned target's recovery state");
    }
    observeCombat(c);
    auto &run=combatRun;const Vec targetPosition=position(*chosen->position);
    const bool ray=clearLine(c.position,targetPosition);
    const auto close=std::count_if(enemies.begin(),enemies.end(),[&](const auto &unit){return (position(*unit.position)-c.position).length()<=policy.radius;});
    const auto healing=potionCount(c,PotionKind::Life)+potionCount(c,PotionKind::Rejuvenation);
    const auto manaPotions=potionCount(c,PotionKind::Mana)+potionCount(c,PotionKind::Rejuvenation);
    int lifePercent=-1,lossPercent=0;
    if(combatMaxLife && *combatMaxLife>0 && view.world.life) {
        lifePercent=int(uint64_t(*view.world.life)*100/unsigned(*combatMaxLife));
        uint16_t peak=*view.world.life;for(const auto &sample:combatLifeSamples) peak=std::max(peak,sample.life);
        lossPercent=int(uint64_t(peak-*view.world.life)*100/unsigned(*combatMaxLife));
    }
    combatDiagnostics={{"target",{{"type",chosen->key.type},{"id",chosen->key.id},{"assignment",run.assignment},
        {"position",{chosen->position->x,chosen->position->y}},{"life",run.lastLife?Json(*run.lastLife):Json(nullptr)},
        {"bestLife",run.bestLife?Json(*run.bestLife):Json(nullptr)}}},
        {"required",required},{"lineClear",ray},{"lifeFeedbackKnown",run.lastLife.has_value()},
        {"healingObserved",run.healingObserved},{"hitFeedbackCount",run.hits},
        {"playerLifePercent",lifePercent<0?Json(nullptr):Json(lifePercent)},{"recentLifeLossPercent",lossPercent},
        {"mana",view.world.mana?Json(*view.world.mana):Json(nullptr)},{"healingPotions",healing},{"manaPotions",manaPotions},
        {"closeEnemies",close},{"bossFromTable",boss},{"skillIndex",run.skillIndex},{"skills",run.skills},
        {"casts",run.casts},{"noNetProgressMs",std::chrono::duration_cast<std::chrono::milliseconds>(now-run.progressAt).count()},
        {"attempts",run.attempts},{"obstacle",run.obstacle},{"status","assess"},
        {"staticFieldRank",staticRank},{"staticFieldRadius",staticRange?Json(*staticRange):Json(nullptr)},
        {"staticFieldStopPercent",staticStop?Json(*staticStop):Json(nullptr)}};
    const auto obstruct=[&](std::string detail) {
        if(!run.lastLife) detail+="; target life feedback unavailable";
        combatDiagnostics["status"]="blocked";combatDiagnostics["obstacle"]=detail;combatDiagnostics["attempts"]=run.attempts;
        releaseCombat(c);cancelWalk(c);
        // Lack of net life progress is not proof of immunity. Preserve all
        // observations and the actual scene; never skip a required kill.
        if(required) throw std::runtime_error("Combat blocked on assigned unit "+std::to_string(chosen->key.id)+": "+detail+
            "; obstruction, misses, recovery or insufficient feedback remain possible");
        bypassedThreats[{chosen->key,chosen->assignmentRevision}]={now+std::chrono::seconds(10),c.position,targetPosition};
        note("unproductive-threat-bypassed",detail);combatTarget.reset();retreatPoint.reset();return false;
    };
    if(now-run.progressAt>=std::chrono::seconds(policy.stallTimeoutSeconds))
        return obstruct("local combat execution budget exhausted; last obstacle: "+run.obstacle);
    if(retreatPoint) {
        if(walkState(c)!="failed" && now<retreatUntil && (position(*retreatPoint)-c.position).length()>2) {
            combatDiagnostics["status"]="retreat";wait("retreat","Following the known safe retreat while observing life and supplies");
            walkPoint(c,*retreatPoint);mobility(c);return true;
        }
        if(walkState(c)=="failed") note("retreat-unavailable",c.walk.snapshot().value("reason",std::string{}));
        cancelWalk(c);retreatPoint.reset();
    }
    const auto selectPosition=[&](bool retreat,int range)->std::optional<OnlinePoint> {
        if(!map || !origin) return {};
        if(!retreat && run.triedPositions.size()>=128) return {};
        const auto grid=c.town.navigationGrid(view);std::optional<OnlinePoint> selected;
        const auto safety=[&](Vec at) {
            float nearestEnemy=(at-targetPosition).length();
            for(const auto &enemy:enemies) nearestEnemy=std::min(nearestEnemy,(at-position(*enemy.position)).length());
            return nearestEnemy;
        };
        float best=retreat?safety(c.position)+1:-std::numeric_limits<float>::max();
        for(int radius:retreat?std::vector<int>{policy.retreat}:std::vector<int>{4,8}) for(int direction=0;direction<16;++direction) {
            const float angle=float(direction)*6.28318530718f/16;
            const Vec at=c.position+Vec{std::cos(angle),std::sin(angle)}*float(radius);
            if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535) continue;
            const auto destination=point(at);const auto exact=position(destination);
            if(!retreat && (run.triedPositions.contains({destination.x,destination.y}) || !clearLine(exact,targetPosition))) continue;
            const float targetDistance=(exact-targetPosition).length();
            if(!retreat && (targetDistance>range+2 || targetDistance<2)) continue;
            const auto path=c.walk.navigationSearch().path(grid,c.position-offset+Vec{.5f,.5f},exact-offset+Vec{.5f,.5f},false,playerMovement,true);
            if(path.empty() || point(path.back()+offset)!=destination) continue;
            const float score=retreat?safety(exact):-std::abs(targetDistance-std::max(2.f,float(range)*.75f))+.1f*safety(exact);
            if(score>best) {best=score;selected=destination;}
        }
        return selected;
    };
    const bool singleThreat=(targetPosition-c.position).length()<=policy.engage && (ray || close>0 || lossPercent>=policy.lifeDropPercent);
    const bool lowLife=lifePercent>=0 && lifePercent<=policy.retreatLifePercent;
    const bool falling=lossPercent>=policy.lifeDropPercent;
    const bool scarce=singleThreat && (required || boss) && !healing && lifePercent>=0 && lifePercent<70;
    if((close>=policy.crowd || (singleThreat && (lowLife || falling || scarce))) && now>=retreatReady) {
        const auto safe=selectPosition(true,policy.range);
        retreatReady=now+std::chrono::seconds(4);
        if(safe) {
            releaseCombat(c);cancelWalk(c);run.reposition.reset();retreatPoint=safe;retreatUntil=now+std::chrono::seconds(2);
            combatDiagnostics["status"]="retreat";note("retreat-started",lowLife?"low life":falling?"observed life loss":scarce?"single threat and scarce healing":"crowd");
            walkPoint(c,*safe);mobility(c);return true;
        }
        run.obstacle="survival pressure with no safe known retreat";combatDiagnostics["obstacle"]=run.obstacle;
        if(lifePercent>=0 && lifePercent<=supplyPolicy.criticalLifePercent && !healing) return obstruct(run.obstacle);
    }
    const auto window=std::chrono::seconds(policy.noProgressSeconds);
    if(run.reposition) {
        const auto state=walkState(c);const bool reached=(position(*run.reposition)-c.position).length()<=3;
        const bool failed=!reached && (state=="failed" || state=="completed" || now>=run.approachUntil);
        if(!failed && !reached) {
            if(!c.walk.active()) walkPoint(c,*run.reposition);
            combatDiagnostics["status"]="reposition";wait("combat-reposition","Changing an observed attack angle through a complete known path");mobility(c);return true;
        }
        run.obstacle=failed?"attack position was not reached":"new attack position reached; awaiting effect";
        run.reposition.reset();cancelWalk(c);run.strategyAt=failed?now-window:now;
    }
    const auto aura=battleAura(c);const auto hand=aura?OnlineSkillHand::Left:OnlineSkillHand::Right;
    const auto nextSkill=[&](std::string cause) {
        if(run.skillIndex<run.skills.size()) run.attempts.push_back({{"skill",run.skills[run.skillIndex]},{"reason",cause}});
        if(run.attempts.size()>64) run.attempts.erase(run.attempts.begin());
        releaseCombat(c);cancelWalk(c);++run.skillIndex;run.repositioned=false;run.strategyAt=now;run.approachUntil={};run.nextCast=now;
        run.obstacle=std::move(cause);note("combat-strategy-next",run.obstacle);
        combatDiagnostics["skillIndex"]=run.skillIndex;combatDiagnostics["attempts"]=run.attempts;combatDiagnostics["obstacle"]=run.obstacle;
    };
    if(run.staticOpening && run.skillIndex==0 && staticStop && run.lastLife && int(*run.lastLife)*100/127<=*staticStop) {
        nextSkill("Static Field health threshold observed; continuing with the configured damage attacks");
        run.skills.erase(run.skills.begin());--run.skillIndex;
        run.staticOpening=false;run.triedPositions.clear();
    }
    std::optional<size_t> skillRow;
    const auto qualification=[&](uint16_t skill) {
        std::string unavailable;
        skillRow.reset();for(size_t row=0;row<definitions->skills.rows().size();++row)
            if(definitions->skills.number(row,"Id")==skill) {skillRow=row;break;}
        if(!skillRow || !knownSkill(c,skill,bool(aura))) unavailable="skill rank / hand / original table unavailable";
        else if(skill!=definitions->staticFieldSkill && !c.combat.skillTargetEligible(*chosen,skill)) unavailable="native target eligibility currently rejects this skill";
        else if(c.supplies && c.supplies->skillUsable) {
            if(const auto usable=c.supplies->skillUsable(skill);usable && !*usable) unavailable="normal character projection reports unusable skill (resource / equipment / state)";
        }
        return unavailable;
    };
    if(run.qualificationWait) {
        for(size_t i=0;i<run.skills.size();++i) if(qualification(run.skills[i]).empty()) {
            run.skillIndex=i;run.qualificationWait=false;run.strategyAt=now;run.obstacle.clear();
            note("combat-qualification-restored",std::to_string(run.skills[i]));break;
        }
    }
    while(run.skillIndex<run.skills.size()) {
        const auto unavailable=qualification(run.skills[run.skillIndex]);
        if(unavailable.empty()) break;
        nextSkill(unavailable);
    }
    if(run.skillIndex>=run.skills.size()) {
        run.qualificationWait=true;
        combatDiagnostics["status"]="qualification-wait";combatDiagnostics["attempts"]=run.attempts;
        if(now-run.strategyAt>=window) return obstruct("all configured attacks currently unavailable; "+run.obstacle);
        wait("combat-resources","Rechecking actual skill qualification and resources before declaring this target blocked");return true;
    }
    const uint16_t skill=run.skills[run.skillIndex];const auto row=*skillRow;
    const int range=skill==definitions->staticFieldSkill && staticRange?std::max(2,*staticRange-2):
        skill==definitions->skill("Attack") || definitions->skills.value(row,"range")=="h2h"?3:policy.range;
    if(((pendingDoor || pendingWaypoint) && (!ray || (targetPosition-c.position).length()>range)) ||
        (view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending)) {
        // A real opening/selection/animation wait has its own contract and
        // deadline. It is not an unsuccessful damage observation window.
        run.strategyAt=now;combatDiagnostics["status"]="normal-action-wait";
        wait("combat-action-wait","Observing the current native action before judging attack effectiveness");return true;
    }
    if(c.supplies && c.supplies->castInputBusy.value_or(false)) {
        // Do not interrupt an ordinary attack animation. Its elapsed time
        // still belongs to the effect window, so repeated casts with no
        // damage cannot continually reset the recovery timer.
        combatDiagnostics["status"]="attack-animation";return true;
    }
    if(now-run.strategyAt>=window && now>=run.nextCast) {
        if(!run.repositioned) {
            releaseCombat(c);const auto at=selectPosition(false,range);
            run.repositioned=true;
            if(at) {
                run.triedPositions.emplace(at->x,at->y);run.reposition=at;run.approachUntil=now+std::chrono::seconds(8);
                cancelWalk(c);walkPoint(c,*at);combatDiagnostics["status"]="reposition";note("combat-effect-reposition","No new low life value; reassessing line and attack angle");mobility(c);return true;
            }
            run.obstacle=ray?"no different reachable firing position":"blocked line and no reachable clear firing position";
        }
        nextSkill(run.obstacle.empty()?"no observed net life progress after position reassessment":run.obstacle);
        if(run.skillIndex>=run.skills.size()) return obstruct("all configured strategies exhausted; "+run.obstacle);
        return true;
    }
    if((targetPosition-c.position).length()>range || !ray) {
        releaseCombat(c);
        if(!pendingDoor && !pendingWaypoint && openDoor(c)) return true;
        if(run.approachUntil==Clock::time_point{}) run.approachUntil=now+std::chrono::seconds(8);
        if(now<run.approachUntil && approachPoint(c,*chosen->position,"combat target: line / range reassessment",float(std::max(2,range-1)))) {
            phase="combat-approach";combatDiagnostics["status"]="approach";mobility(c);return true;
        }
        run.obstacle=ray?"no usable approach within attack range":"observed line blocked / no usable approach";
        run.strategyAt=now-window;return true;
    }
    run.approachUntil={};
    if(aura && !selectSkill(c,*aura,OnlineSkillHand::Right)) {wait("combat-aura","Waiting for the normal battle aura selection");return true;}
    if(!selectSkill(c,skill,hand)) {wait("combat-select","Waiting for normal attack skill selection");return true;}
    const auto delay=definitions->skills.number(row,"delay");
    bool delayed=false;
    if(!definitions->skills.value(row,"delay").empty() && (!delay || *delay>0)) {
        const auto player=c.combat.states().find({0,view.load.playerUnitId.value_or(0)});
        for(size_t state=0;state<definitions->states.rows().size();++state) if(definitions->states.value(state,"state")=="skilldelay")
            if(const auto id=definitions->states.number(state,"Id");id && player!=c.combat.states().end() && player->second.decoded)
                delayed=player->second.states.contains(uint8_t(*id));
    }
    combatDiagnostics["skill"]=skill;combatDiagnostics["skillDelayFrames"]=delay?Json(*delay):Json(nullptr);
    combatDiagnostics["status"]=delayed?"native-skill-delay":"attack";
    if(delayed || now<run.nextCast || (c.supplies && c.supplies->castInputBusy.value_or(false)) ||
        (view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending)) return true;
    cancelWalk(c);OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.target=chosen->key;cast.stationary=true;
    if(skill==definitions->staticFieldSkill) {cast.target.reset();cast.point=point(c.position);}
    // Same held pointer as the ordinary controller. Skills with a known
    // positive native delay use separate clicks, not a synthetic channel.
    cast.repeat=run.repeating && ((delay && *delay<=0) || definitions->skills.value(row,"delay").empty());
    if(submitCombat(c,cast,DebugActionOwner::Combat,"combat-cast",c.position)) {
        combatOwned=true;run.repeating=true;++run.casts;++castRequests;lastAttack=now;
        run.nextCast=now+(delay && *delay>0?std::chrono::milliseconds(int64_t(*delay)*40):debugDecisionInterval);
        wait("combat-cast","Observing actual life effect after a normal attack input");
    } else {
        run.obstacle=c.combat.reason();combatDiagnostics["obstacle"]=run.obstacle;
        run.nextCast=now+debugDecisionInterval;
    }
    return true;
}
} // namespace d2x
