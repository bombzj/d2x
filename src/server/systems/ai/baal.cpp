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
#include "gameplay/monsters/baal_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::ai {
StepStatus System::baalFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const bool clone=unit.rule.ai.kind==MonsterAiKind::BaalClone;
    if(clone) {
        const auto *minionOwner=ports_.monsters.find(ports_.monsters.minionOwner(id));
        const auto *storedOwner=ports_.monsters.find(unit.nativeOwner);
        if((minionOwner && !minionOwner->alive()) || !storedOwner || !storedOwner->alive()) {
            const auto killed=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
            return isDeferred(killed.status)?StepStatus::Blocked:StepStatus::Complete;
        }
    }
    const auto &catalog=*unit.rule.formulas;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto position=[&](combat::ParticipantView target) {
        return target.position()+ports_.areas.at(target.player?target.player->area:target.monster->area).definition.origin-area.definition.origin;
    };
    const auto selected=[&](combat::ParticipantView target,bool right)->std::optional<std::pair<int,int>> {
        if(target.player) {
            const auto &player=*target.player;const auto &record=player.persistent.player;
            const int index=record.weaponSet*2+(right?1:0),skill=record.selectedSkills.at(index);
            if(skill<0 || !catalog.skills.contains(skill)) return {};
            const auto owner=record.selectedSkillOwners.at(index);
            const NativeSkillSlot slot{skill,EntityId{owner==UINT32_MAX?0:owner}};
            if(std::find(player.totals.skillOrder.begin(),player.totals.skillOrder.end(),slot)==player.totals.skillOrder.end()) return {};
            return std::pair{skill,nativeSkillSlotLevel(slot,player.totals.skillOrder,player.totals.skillRanks,
                player.totals.chargedSkills,catalog.maximumSkillRank)};
        }
        if(!target.monster || !right || !target.monster->rightSkill) return {};
        const auto stats=ports_.effects.unitStatValues(target.id(),tick.tick);if(!stats) return {};
        const auto ranks=nativeMonsterSkillRanks(target.monster->rule.baseSkillRanks,*stats,*target.monster->rule.stats,*target.monster->rule.formulas);
        const auto skill=target.monster->rightSkill->skill;const auto rank=ranks.find(skill);
        return std::pair{skill,rank==ranks.end()?0:rank->second};
    };
    const auto score=[&](EntityId id) {
        const auto target=participants.find(id);const auto stats=ports_.effects.unitStatValues(id,tick.tick);
        if(!stats) throw std::logic_error("Missing Baal target stats");
        const auto &rules=target.player?*target.player->rules.character->stats:*target.monster->rule.stats;
        const auto stat=[&](std::string_view name){return nativeStatValue(*stats,rules,name);};
        BaalTargetScore input;
        input.damageResist=stat("damageresist");input.magicResist=stat("magicresist");input.fireResist=stat("fireresist");
        input.lightResist=stat("lightresist");input.coldResist=stat("coldresist");input.maximumDamage=stat("maxdamage");
        input.fireDamage=stat("firemaxdam");input.lightDamage=stat("lightmaxdam");input.magicDamage=stat("magicmaxdam");
        input.coldDamage=stat("coldmaxdam");input.poisonDamage=stat("poisonmaxdam");
        for(const bool right:{false,true}) if(const auto skill=selected(target,right)) {
            const auto rank=uint8_t(catalog.skills.at(skill->first).number("attackrank"));
            if(right) {input.rightRank=skill->second;input.rightAttackRank=rank;}
            else {input.leftRank=skill->second;input.leftAttackRank=rank;}
        }
        input.lifePercentage=playerLifePercentage(stat("hitpoints"),stat("maxhp"));input.threat=target.monster?target.monster->rule.threat:14;
        const auto at=position(target);
        input.melee=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,at,target.size(),unit.rule.meleeRange);
        input.clearRay=area.definition.collision.missileSegment(unit.position,at,{4,1});
        input.cold=ports_.effects.unitStates(id,tick.tick).contains(unit.rule.coldState);
        return baalTargetScore(input);
    };
    const auto query=acquireNativeBossTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},unit,score);
    if(!controller.homeCommand) {
        const auto at=unit.position+area.definition.origin;controller.homeCommand=Vec{std::floor(at.x),std::floor(at.y)};
    }
    const auto home=*controller.homeCommand-area.definition.origin;
    const auto target=query.target?query.target->unit:UnitTarget{};
    const auto targetView=participants.find(target.id);const auto at=query.target?query.target->position:Vec{};
    int action=controller.phase;
    if(!action) {
        if(!query.target) {
            const auto global=unit.position+area.definition.origin;
            const auto room=ports_.areas.roomAtPosition(unit.area,unit.position,int(std::floor(global.x)),int(std::floor(global.y)));
            const bool missile=room && ports_.areas.nativeCollisionMask(*room,int(std::floor(global.x)),int(std::floor(global.y)),2,0x40,ports_.spatial.nativeCollisionSample());
            action=missile?4:8*(limitedRandom(controller.random,100)<8)+1;
        } else {
            const auto stats=ports_.effects.unitStatValues(target.id,tick.tick);if(!stats) return StepStatus::Blocked;
            const auto &rules=targetView.player?*targetView.player->rules.character->stats:*targetView.monster->rule.stats;
            const auto stat=[&](std::string_view name){return nativeStatValue(*stats,rules,name);};
            BaalWeights input;input.targets=query.count;input.score=query.score;input.clones=controller.loop;
            input.distance=monsterAiDistance(unit.position,unit.rule.size,at);input.difficulty=unit.rule.difficulty;
            input.lifePercentage=playerLifePercentage(unit.life,unit.maximumLife);input.targetLifePercentage=playerLifePercentage(stat("hitpoints"),stat("maxhp"));
            input.melee=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,at,targetView.size(),unit.rule.meleeRange);
            input.blocked=!area.definition.collision.missileSegment(unit.position,at,{4,1});
            input.cold=ports_.effects.unitStates(target.id,tick.tick).contains(unit.rule.coldState);
            const auto distance=monsterAiDistance(at,(targetView.size()>>1)+1,home);input.far=distance>75;input.further=distance>100;
            if(targetView.player) {
                const auto portal=ports_.travel.read().portals.find(targetView.player->player);
                if(portal!=ports_.travel.read().portals.end()) input.portal=uint16_t(portal->second.field)==132 &&
                    monsterAiDistance(portal->second.fieldPosition+ports_.areas.at(portal->second.field).definition.origin,0,*controller.homeCommand)<75;
            }
            for(const bool right:{false,true}) if(const auto skill=selected(targetView,right)) {
                const auto matches=[&](std::string_view name,int minimum) {
                    const auto found=catalog.skillIds.find(std::string(name));return found!=catalog.skillIds.end() && found->second==skill->first && skill->second>minimum;
                };
                input.specialSkill|=matches("blizzard",-1) || matches("meteor",-1) || matches("fire wall",3) || matches("immolation arrow",7);
            }
            action=baalWeightedAction(baalWeights(input),controller.random);
        }
    }
    if(clone && (action==7 || action==9 || action==14 || action==15)) action=2;
    const auto result=perform(controller,[this,id,target,at,home,action,clone,tick,
        point=std::optional<Vec>{},turn=std::optional<int>{},novaAssigned=false,teleportPrepared=false]() mutable ->DomainResult<> {
        const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
        const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        auto &ai=state_.controllers.at(id);const auto &area=ports_.areas.at(unit->area);
        ai.nextDecision=tick.tick+25;
        const auto idle=[&] {ports_.monsters.neutralMode(id);ai.pursuing=false;return done();};
        if(clone && !target.id) {ai.nextDecision=tick.tick+15;return idle();}
        const auto cast=[&](size_t slot,UnitTarget victim,std::optional<Vec> destination={})->DomainResult<> {
            const auto skill=unit->rule.skillIds.at(slot);const auto found=unit->rule.skillActions.find(skill);
            if(!skill || found==unit->rule.skillActions.end()) return idle();
            actions::MonsterCastRequest request{id,skill,victim,tick.tick,found->second.nativeMode};request.position=destination;
            const auto casted=ports_.skills.requestCast(request);
            if(!casted && !isDeferred(casted.status)) idle();
            return casted;
        };
        const auto move=[&](Vec destination,EntityId victim={},int circleTurn=0,int circleDistance=0)->DomainResult<> {
            const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,destination},victim,0,75,false,1,circleTurn,circleDistance,13,uint8_t(circleDistance?circleDistance:5)});
            ai.pursuing=bool(moved);return moved;
        };
        switch(action) {
        case 2:case 6:
            if(!target.id) return idle();
            return move(monsterRadiusApproachPoint(unit->position,unit->rule.size,at,12));
        case 3: {
            if(!target.id) return idle();
            if(!turn) {
                const auto expected=unit->combatRandom;auto random=expected;const int direction=(rollRandom(random)&255)<128?1:-1;
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;turn=direction;
            }
            return move(at,target.id,*turn,6);
        }
        case 4: {
            if(!point) {
                const auto expected=unit->combatRandom;auto random=expected;const auto destination=monsterWanderPoint(unit->position,16,random);
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;point=destination;
            }
            return move(*point);
        }
        case 5:return move(home);
        case 8: {
            if(!target.id) return idle();
            const auto stats=ports_.effects.unitStatValues(target.id,tick.tick);if(!stats) return {DomainStatus::Unavailable,{}};
            const auto *player=ports_.players.findActor(target.id);const auto *monster=ports_.monsters.find(target.id);
            if(!player && !monster) return idle();
            const auto &rules=player?*player->rules.character->stats:*monster->rule.stats;
            const bool defense=!player || nativeStatValue(*stats,rules,"maxhp")>nativeStatValue(*stats,rules,"maxmana");
            return cast(defense?5:6,target);
        }
        case 9:return cast(2,target);
        case 10:return ports_.skills.requestCast({id,0,target,tick.tick,5});
        case 11:
            if(!novaAssigned) {
                const auto assigned=ports_.monsters.assignBaalNova(id,tick.tick);if(!assigned) return assigned;novaAssigned=true;
            }
            return cast(0,target);
        case 12:return cast(1,target);
        case 13:return cast(3,target);
        case 14: {
            if(!target.id) return idle();
            if(!teleportPrepared) {
                const auto position=ports_.monsters.findBaalTeleportPosition(id,tick.tick,ports_.spatial.nativeCollisionSample());
                if(!position) return {position.status,{}};point=*position.value;teleportPrepared=true;
            }
            return point?cast(4,{},*point):idle();
        }
        case 15: {
            if(!point) {
                for(const auto child:unit->nativeMinions) if(child!=id) if(const auto *minion=ports_.monsters.find(child);minion && minion->alive()) return idle();
                if(const auto *owner=ports_.monsters.find(unit->nativeOwner);owner && owner->alive()) return idle();
                const auto expected=unit->combatRandom;auto random=expected;
                // GetMinionSpawnInfo's clone branch draws two unused offsets.
                (void)limitedRandom(random,24);(void)limitedRandom(random,24);
                const auto x=int(rollRandom(random)%24)-12,y=int(rollRandom(random)%24)-12;
                const auto victim=ports_.monsters.nativeTargetPosition(unit->movementTarget,unit->area);
                const auto global=(victim?victim->first:unit->position)+area.definition.origin;
                const Vec destination{float(std::floor(global.x))+float(x)-area.definition.origin.x,
                    float(std::floor(global.y))+float(y)-area.definition.origin.y};
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;point=destination;
            }
            const auto born=ports_.monsters.spawnMinionChild(id,*point,0,{unit->rule.spawnFallbackClass,1,0},tick.tick,
                ports_.spatial.nativeCollisionSample(),MinionBirthProcedure::BaalClone);
            if(isDeferred(born.status)) return {born.status,{}};
            if(born && *born.value) ai.loop=nativeAdd(ai.loop,1);
            return idle();
        }
        default:return idle();
        }
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
