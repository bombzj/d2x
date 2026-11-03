#include "ai_impl.hpp"
#include "content/monsters/monster_identity.hpp"
#include "world/collision.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::Definitions::loadCombatObservations() {
    const auto attack=skill("Attack");
    for(size_t row=0;row<skills.rows().size();++row) {
        const auto id=skills.number(row,"Id");if(!id || *id<0 || *id>65535) continue;
        skillRows[uint16_t(*id)]=row;uint16_t mask=4;
        if(*id==attack || skills.value(row,"range")=="h2h") mask=5;
        else for(const auto field:{"srvmissile","srvmissilea","srvmissileb","srvmissilec"}) {
            const auto name=skills.value(row,field);if(name.empty()) continue;
            for(size_t missile=0;missile<missiles.rows().size();++missile) if(missiles.value(missile,"Missile")==name)
                mask|=nativeMissileContactMask(missiles.number(missile,"CollideType").value_or(0))&5;
        }
        attackMasks[uint16_t(*id)]=mask;
    }
    staticFieldSkill=skill("Static Field");
    for(size_t row=0;row<skills.rows().size();++row) if(skills.number(row,"Id")==staticFieldSkill) {
        staticFieldRow=row;staticFieldRange=compileRuleFormula(skills.value(row,"aurarangecalc"));break;
    }
    for(size_t row=0;row<missiles.rows().size();++row) {
        const auto id=missiles.number(row,"Id"),size=missiles.number(row,"Size");
        if(id && size && *id>=0 && *size>0 && *size<=255 && missiles.number(row,"ClientCol").value_or(0))
            hazardSizes[*id]=*size;
    }
    // SkillMonst Do152 / SkillSor Do22 / Do102 and PrimeFirewall's child.
    // These are observed visual hazards, never a second damage simulation.
    for(const auto name:{"diablight","diabfire","diabwall","vampirefirewall"}) {
        bool found=false;
        for(size_t row=0;row<missiles.rows().size();++row) if(missiles.value(row,"Missile")==name) {
            const auto id=missiles.number(row,"Id"),size=missiles.number(row,"Size");
            if(!id || !size || *size<1 || *size>3) throw std::runtime_error("Unverified native hazard missile: "+std::string(name));
            hazardSizes[*id]=*size;dangerousMissiles.insert(*id);found=true;break;
        }
        if(!found) throw std::runtime_error("Missing native hazard missile: "+std::string(name));
    }
}
std::optional<int> DebugAi::Impl::Definitions::staticRadius(unsigned rank) const {
    if(!rank || rank>255 || !staticFieldRange) return {};
    return evaluateRuleFormula(staticFieldRange,[&](const RuleReference &ref)->RuleNumber {
        if(ref.kind!=RuleReference::Kind::Parameter) return {};
        const auto &name=ref.parameter;
        if(name=="lvl") return int(rank);
        if(name.size()==4 && name.starts_with("par") && name[3]>='1' && name[3]<='8')
            return skills.number(staticFieldRow,"Param"+name.substr(3));
        if(name.size()==4 && name.starts_with("ln") && (name[2]=='1' || name[2]=='3' || name[2]=='5' || name[2]=='7') && name[3]==name[2]+1) {
            const auto first=skills.number(staticFieldRow,"Param"+name.substr(2,1)),second=skills.number(staticFieldRow,"Param"+name.substr(3,1));
            if(!first || !second) return {};
            const auto result=int64_t(*first)+int64_t(rank-1)*(*second);
            if(result<INT32_MIN || result>INT32_MAX) return {};
            return int32_t(result);
        }
        return {};
    });
}
std::vector<DebugAi::Impl::VisualThreat> DebugAi::Impl::visibleThreats(const Context &c) const {
    std::vector<VisualThreat> hazards;
    if(!policy.avoidHazards) return hazards;
    if(c.supplies) for(const auto &missile:c.supplies->missiles) {
        const auto size=definitions->hazardSizes.find(missile.id);
        if(!missile.hostile || missile.age<0 || missile.remaining<=0 || size==definitions->hazardSizes.end() ||
            (missile.position-c.position).length()>64) continue;
        bool boss = false;
        if (missile.source) if (const auto unit = c.session.read().world.units.find(*missile.source);
            unit != c.session.read().world.units.end() && unit->first.type == 1) {
            const auto row = findCompiledMonsterRow(definitions->monsters, c.combat.monsterClass(unit->second).value_or(UINT16_MAX));
            boss = row && definitions->monsters.number(*row, "boss").value_or(0);
        }
        hazards.push_back({missile.id,missile.position,missile.position+missile.velocity*
            std::min(float(policy.hazardLookaheadMs)/1000.f,missile.remaining),float(size->second+policy.hazardPadding),false,boss || definitions->dangerousMissiles.contains(missile.id)});
    }
    for(const auto &[key,unit]:c.session.read().world.units) {
        if(key.type!=1 || !unit.position || onlineMonsterCorpse(unit) || !c.combat.hostileSource(unit) || c.combat.monsterTargetEligible(unit,false)) continue;
        const auto row=findCompiledMonsterRow(definitions->monsters,c.combat.monsterClass(unit).value_or(UINT16_MAX));
        if(!row || !definitions->monsters.number(*row,"killable").value_or(0) ||
            definitions->monsters.number(*row,"npc").value_or(0) || definitions->monsters.number(*row,"interact").value_or(0)) continue;
        const Vec at{float(unit.position->x),float(unit.position->y)};
        if((at-c.position).length()<=32) hazards.push_back({int(unit.classId.value_or(0)),at,at,float(policy.kiteMinDistance),true,true});
    }
    const auto incoming = std::count_if(hazards.begin(), hazards.end(), [&](const auto &hazard) {
        return !hazard.unitSource && threatClearance({&hazard, 1}, c.position) < 0;
    });
    const auto &world = c.session.read().world;
    bool vulnerable = combatMaxLife && *combatMaxLife > 0 && world.life &&
        uint64_t(*world.life) * 100 <= uint64_t(*combatMaxLife) * policy.retreatLifePercent;
    if (combatMaxLife && *combatMaxLife > 0 && world.life)
        for (const auto &sample : combatLifeSamples)
            vulnerable |= sample.life > *world.life && uint64_t(sample.life - *world.life) * 100 >=
                uint64_t(*combatMaxLife) * policy.lifeDropPercent;
    if (incoming < 3 && !vulnerable)
        std::erase_if(hazards, [](const auto &hazard) { return !hazard.dangerous; });
    return hazards;
}
float DebugAi::Impl::threatClearance(std::span<const VisualThreat> hazards,Vec at) {
    const auto segmentDistance=[](Vec at,Vec from,Vec to) {
        const Vec delta=to-from;const float length=delta.x*delta.x+delta.y*delta.y;
        const Vec relative=at-from;
        const float t=length>0?std::clamp((relative.x*delta.x+relative.y*delta.y)/length,0.f,1.f):0.f;
        return (at-(from+delta*t)).length();
    };
    float nearest=64;
    for(const auto &hazard:hazards) nearest=std::min(nearest,segmentDistance(at,hazard.from,hazard.to)-hazard.radius);
    return nearest;
}
bool DebugAi::Impl::avoidHazards(Context &c) {
    if(!policy.enabled || !policy.avoidHazards || c.town.read().town) return false;
    const auto now=Clock::now();const auto hazards=visibleThreats(c);
    const auto safety=[&](Vec at) {return threatClearance(hazards,at);};
    const float current=safety(c.position);
    const auto sources=std::count_if(hazards.begin(),hazards.end(),[](const auto &threat){return threat.unitSource;});
    hazardDiagnostics={{"dangerousHostileMissiles",hazards.size()-size_t(sources)},{"unitThreatSources",sources},{"clearance",current},{"status","clear"}};
    if(hazardDodge) {
        const bool reached=(Vec{float(hazardDodge->x),float(hazardDodge->y)}-c.position).length()<=3;
        const Vec destination{float(hazardDodge->x),float(hazardDodge->y)};
        if(!reached && now<hazardDodgeUntil && walkState(c)!="failed" && safety(destination)>=1) {
            if(!c.walk.active()) walk(c,{{"goal","point"},{"x",hazardDodge->x},{"y",hazardDodge->y},{"interact",false},{"purpose","combat-evade"}});
            mobility(c);wait("combat-evade","Moving to the selected observed safe position");hazardDiagnostics["status"]="evading";return true;
        }
        cancelWalk(c);hazardDodge.reset();
    }
    if(hazards.empty() || current>=0) return false;
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin) return false;
    const auto grid=c.town.navigationGrid(c.session.read());const Vec offset{float(origin->x),float(origin->y)};
    const auto &rooms=c.town.navigationRooms();
    const auto inside=[](const auto &room,Vec at){return room.loaded && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;};
    const auto source=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room){return room.level==area && inside(room,c.position);});
    const bool teleport=teleportAvailable(c);float best=current;std::optional<OnlinePoint> selected;
    for(const int radius:{8,12,18}) for(int direction=0;direction<16;++direction) {
        const float angle=float(direction)*6.28318530718f/16;
        const Vec at=c.position+Vec{std::cos(angle),std::sin(angle)}*float(radius);
        if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535) continue;
        const OnlinePoint point{uint16_t(at.x),uint16_t(at.y)};const Vec exact{float(point.x),float(point.y)},local=exact-offset+Vec{.5f,.5f};
        if(!tacticalPositionAllowed(c,exact) || !grid.nativeMovementSegment(local,local,playerMovement)) continue;
        const auto landing=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room){return room.level==area && inside(room,exact);});
        if(landing==rooms.end()) continue;
        if(teleport && (source==rooms.end() || (landing!=source && std::find(source->near.begin(),source->near.end(),size_t(landing-rooms.begin()))==source->near.end()))) continue;
        const float clearance=safety(exact);if(clearance<2) continue;
        bool occupied=false;
        for(const auto &[key,unit]:c.session.read().world.units) if(key.type==1 && unit.position && c.combat.hostileSource(unit) && !onlineMonsterCorpse(unit) &&
            (exact-Vec{float(unit.position->x),float(unit.position->y)}).length()<4) {occupied=true;break;}
        if(occupied) continue;
        if(!teleport) {
            const auto path=c.walk.navigationSearch().path(map->grid,c.position-offset+Vec{.5f,.5f},local,false,playerMovement,true);
            if(path.empty() || (path.back()-local).length()>1) continue;
            if(!tacticalPathSafe(c,path,offset,hazards)) continue;
        }
        const float score=clearance-.1f*float(radius);
        if(score>best) {best=score;selected=point;}
    }
    if(!selected) {hazardDiagnostics["status"]="no-observed-safe-position";return false;}
    releaseCombat(c);cancelWalk(c);
    combatRun.kitePoint.reset();combatRun.kiteAfterCast=false;
    if(combatTarget && combatRun.suspendedAt==Clock::time_point{}) combatRun.suspendedAt=now;
    hazardDodge=selected;hazardDodgeUntil=now+std::chrono::seconds(2);
    hazardDiagnostics["status"]="evading";hazardDiagnostics["destination"]={selected->x,selected->y};
    note("hazard-dodge-selected",std::to_string(selected->x)+","+std::to_string(selected->y));
    walk(c,{{"goal","point"},{"x",selected->x},{"y",selected->y},{"interact",false},{"purpose","combat-evade"}});
    mobility(c);return true;
}
} // namespace d2x
