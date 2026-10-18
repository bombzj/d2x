#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/diablo_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::ai {
StepStatus System::diabloFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &catalog=*unit.rule.formulas;
    if(!controller.homeCommand) {
        const auto at=unit.position+area.definition.origin;
        controller.homeCommand=Vec{std::floor(at.x),std::floor(at.y)};
    }
    const auto home=*controller.homeCommand-area.definition.origin;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto point=[&](combat::ParticipantView target) {
        return target.position()+ports_.areas.at(target.player?target.player->area:target.monster->area).definition.origin-area.definition.origin;
    };
    const auto selected=[&](combat::ParticipantView target,bool right)->std::optional<std::pair<int,int>> {
        if(target.player) {
            const auto &player=*target.player;const auto &record=player.persistent.player;
            const auto index=record.weaponSet*2+(right?1:0);const int skill=record.selectedSkills.at(index);
            if(skill<0 || !catalog.skills.contains(skill)) return {};
            const auto owner=record.selectedSkillOwners.at(index);
            const auto slot=NativeSkillSlot{skill,EntityId{owner==UINT32_MAX?0:owner}};
            if(std::find(player.totals.skillOrder.begin(),player.totals.skillOrder.end(),slot)==player.totals.skillOrder.end()) return {};
            return std::pair{skill,nativeSkillSlotLevel(slot,player.totals.skillOrder,player.totals.skillRanks,
                player.totals.chargedSkills,catalog.maximumSkillRank)};
        }
        if(!right || !target.monster->rightSkill) return {};
        const auto &monster=*target.monster;
        const auto stats=ports_.effects.unitStatValues(monster.id,tick.tick);
        if(!stats) throw std::logic_error("Missing prime evil target stats");
        const auto ranks=nativeMonsterSkillRanks(monster.rule.baseSkillRanks,*stats,*monster.rule.stats,*monster.rule.formulas);
        const auto rank=ranks.find(monster.rightSkill->skill);
        return std::pair{monster.rightSkill->skill,rank==ranks.end()?0:rank->second};
    };
    const auto score=[&](EntityId candidate) {
        const auto target=participants.find(candidate);
        const auto stats=ports_.effects.unitStatValues(candidate,tick.tick);
        if(!stats) throw std::logic_error("Missing prime evil target stats");
        const auto &rules=target.player?*target.player->rules.character->stats:*target.monster->rule.stats;
        const auto stat=[&](std::string_view name){return nativeStatValue(*stats,rules,name);};
        const auto at=point(target);DiabloTargetScore input;
        input.damageResist=stat("damageresist");input.magicResist=stat("magicresist");input.fireResist=stat("fireresist");
        input.lightResist=stat("lightresist");input.coldResist=stat("coldresist");input.maximumDamage=stat("maxdamage");
        input.fireDamage=stat("firemaxdam");input.lightDamage=stat("lightmaxdam");input.magicDamage=stat("magicmaxdam");
        input.coldDamage=stat("coldmaxdam");input.poisonDamage=stat("poisonmaxdam");
        if(const auto skill=selected(target,false)) {input.leftRank=skill->second;input.leftAttackRank=uint8_t(catalog.skills.at(skill->first).number("attackrank"));}
        if(const auto skill=selected(target,true)) {input.rightRank=skill->second;input.rightAttackRank=uint8_t(catalog.skills.at(skill->first).number("attackrank"));}
        input.lifePercentage=playerLifePercentage(stat("hitpoints"),stat("maxhp"));input.threat=target.monster?target.monster->rule.threat:14;
        input.melee=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,at,target.size(),unit.rule.meleeRange);
        input.clearRay=area.definition.collision.missileSegment(unit.position,at,{4,1});
        input.cold=ports_.effects.unitStates(candidate,tick.tick).contains(unit.rule.coldState);
        input.returnHome=monsterAiDistance(unit.position,(unit.rule.size>>1)+1,home)>85 && monsterAiDistance(at,(target.size()>>1)+1,home)<85;
        return diabloTargetScore(input);
    };
    const auto query=acquireNativeBossTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},unit,score);
    const auto target=query.target?query.target->unit:UnitTarget{};
    const auto targetView=participants.find(target.id);
    const auto at=query.target?query.target->position:Vec{}-area.definition.origin;
    const travel::Portal *portal=nullptr;
    if(targetView.player) if(const auto found=ports_.travel.read().portals.find(targetView.player->player);found!=ports_.travel.read().portals.end()) portal=&found->second;
    int action=controller.phase;
    if(!action) {
        if(!query.target) {
            const auto global=unit.position+area.definition.origin;
            const auto room=ports_.areas.roomAtPosition(unit.area,unit.position,int(std::floor(global.x)),int(std::floor(global.y)));
            const bool missile=room && ports_.areas.nativeCollisionMask(*room,int(std::floor(global.x)),int(std::floor(global.y)),2,0x40,ports_.spatial.nativeCollisionSample());
            action=missile?16:limitedRandom(controller.random,1000)>=1?11:4;
        } else {
            const auto stats=ports_.effects.unitStatValues(target.id,tick.tick);if(!stats) return StepStatus::Blocked;
            const auto &rules=targetView.player?*targetView.player->rules.character->stats:*targetView.monster->rule.stats;
            const auto stat=[&](std::string_view name){return nativeStatValue(*stats,rules,name);};
            DiabloWeights input;input.targets=query.count;input.score=query.score;input.distance=monsterAiDistance(unit.position,unit.rule.size,at);
            input.fireResist=stat("fireresist");input.lightResist=stat("lightresist");input.lifePercentage=playerLifePercentage(stat("hitpoints"),stat("maxhp"));
            input.playerDifficulty=unit.rule.difficulty;input.melee=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,at,targetView.size(),unit.rule.meleeRange);
            input.blocked=!area.definition.collision.missileSegment(unit.position,at,{4,1});input.cold=ports_.effects.unitStates(target.id,tick.tick).contains(unit.rule.coldState);
            const int distance=monsterAiDistance(at,(targetView.size()>>1)+1,home);input.far=distance>85;input.further=distance>105;
            if(portal) input.portal=uint16_t(portal->field)==108 && monsterAiDistance(portal->fieldPosition+ports_.areas.at(portal->field).definition.origin,0,*controller.homeCommand)<85;
            for(const bool right:{false,true}) if(const auto skill=selected(targetView,right))
                input.specialSkill|=skill->first==59 || skill->first==56 || (skill->first==51 && skill->second>3) || (skill->first==27 && skill->second>7);
            auto weights=diabloWeights(input);
            const auto prison=[&](RegionId region,Vec position)->DomainResult<bool> {
                const auto &definition=ports_.areas.at(region).definition;
                const auto first=std::ranges::find_if(definition.nativeMonsterClasses,[](const auto &entry){return entry.second.identity.monster=="boneprison1";});
                if(first==definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
                return ports_.monsters.probeDiabloPrison(region,position,first->first,tick.tick,ports_.spatial.nativeCollisionSample());
            };
            const auto region=targetView.player?targetView.player->area:targetView.monster->area;
            const auto targetProbe=prison(region,targetView.position());if(!targetProbe) return StepStatus::Blocked;
            if(*targetProbe.value) weights[9]=0;
            if(weights[15] && portal) {
                const auto portalProbe=prison(portal->field,portal->fieldPosition);if(!portalProbe) return StepStatus::Blocked;
                if(!*portalProbe.value) weights[15]=0;
            }
            action=diabloWeightedAction(weights,controller.random);
        }
    }
    // Keep the selected action across output backpressure. UseSkill may
    // consume this unit's seed; later circle/wander draws sample that seed.
    const auto result=perform(controller,[this,id,target,at,home,action,tick,portalId=portal?portal->fieldId:EntityId{},buffDone=false]() mutable ->DomainResult<> {
        const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
        const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        auto &ai=state_.controllers.at(id);const auto &area=ports_.areas.at(unit->area);
        const auto idle=[&](int frames){ports_.monsters.neutralMode(id);ai.pursuing=false;ai.nextDecision=tick.tick+uint64_t(frames);};
        const auto cast=[&](size_t slot,UnitTarget victim)->DomainResult<> {
            const auto skill=unit->rule.skillIds.at(slot);const auto program=unit->rule.skillActions.find(skill);
            if(!skill || program==unit->rule.skillActions.end()) {idle(2);return done();}
            const auto casted=ports_.skills.requestCast({id,skill,victim,tick.tick,program->second.nativeMode});
            ai.nextDecision=tick.tick+1;return casted;
        };
        if(!buffDone && action!=5) {
            const auto skill=unit->rule.skillIds[7];
            if(skill && unit->rule.skillActions.contains(skill)) {
                const auto &row=unit->rule.formulas->skills.at(skill);
                if(row.auraState>=0 && !ports_.effects.unitStates(id,tick.tick).contains(row.auraState)) {
                    const auto buff=cast(7,target);if(isDeferred(buff.status)) return buff;
                    if(buff) {ai.phase=0;return buff;}
                }
            }
            buffDone=true;
        }
        ai.phase=action==5?5:0;
        const auto move=[&](Vec point,EntityId victim,int velocityBonus=0,int turn=0,int radius=0,uint8_t bound=5) {
            const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,point},victim,0,75+velocityBonus,false,1,turn,radius,13,bound});
            ai.pursuing=bool(moved);ai.nextDecision=tick.tick+1;return moved;
        };
        const auto randomPoint=[&](int radius)->DomainResult<Vec> {
            const auto expected=unit->combatRandom;auto random=expected;const auto point=monsterWanderPoint(unit->position,radius,random);
            const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return {consumed.status,{}};
            return {DomainStatus::Applied,point};
        };
        switch(action) {
        case 1:return move(at,{},20);
        case 2:case 3:ai.nextDecision=tick.tick+1;return ports_.skills.requestCast({id,0,target,tick.tick,uint8_t(action==2?4:5)});
        case 4:ai.nextDecision=tick.tick+1;return ports_.monsters.aiPose(id,11,false,false,tick.tick);
        case 5:
            if(ports_.effects.unitStates(id,tick.tick).contains(unit->rule.formulas->infernoState)) {
                const auto cleared=ports_.monsters.toggleAiState(id,unit->rule.formulas->infernoState,false,tick.tick);
                if(!cleared) return cleared;ai.phase=0;idle(2);return done();
            }
            if(!unit->rule.skillIds[0]) {ai.phase=0;idle(2);return done();}
            return cast(0,target);
        case 6:return cast(2,target);
        case 7:return cast(1,target);
        case 8:return cast(3,target);
        case 9:return cast(6,target);
        case 10:return cast(4,target);
        case 12: {
            const auto expected=unit->combatRandom;auto random=expected;const int turn=(rollRandom(random)&255)<128?1:-1;
            const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
            return move(at,target.id,0,turn,4,4);
        }
        case 13:return cast(5,target);
        case 14: {
            auto moved=move(home,{},50,0,0,100);if(moved) return moved;
            const int distance=monsterAiDistance(unit->position,0,home);
            const auto fallback=Vec{home.x+((unit->position.x>home.x)-(unit->position.x<home.x))*(distance>>1),
                home.y+((unit->position.y>home.y)-(unit->position.y<home.y))*(distance>>1)};
            moved=move(fallback,{},50,0,0,100);if(!moved) idle(2);return done();
        }
        case 15:
            if(target.type!=0 || !target.id) {idle(3);return done();}
            if(!ports_.travel.nativePortalPosition(portalId)) {idle(2);return done();}
            return cast(6,{portalId,0,2});
        case 16: {
            const auto point=randomPoint(5);if(!point) return {point.status,{}};return move(*point.value,{},20);
        }
        default:idle(unit->rule.difficulty==0?12:unit->rule.difficulty==1?8:4);return done();
        }
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
