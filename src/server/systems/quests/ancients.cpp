#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
namespace d2x::server::quests {
void System::resetAncients() {
    auto &world=state_.actFive;
    if(!world.ancientsActive || world.ancientsDefeated) return;
    for(const auto name:ancientIdentities) ports_.monsters.withdrawQuestGroup(RegionId(120),name);
    world.ancientsActive=false;world.ancientAt=0;
    if(const auto *area=ports_.areas.find(RegionId(120))) for(const auto &object:area->definition.objects)
        if(object.rule.operation>=62 && object.rule.operation<=65) state_.objectModes[object.id]=0;
}
StepStatus System::ancientsStep(TickContext tick) {
    auto &world=state_.actFive;if(!world.ancientsActive && !world.ancientsDefeated) return StepStatus::Complete;
    bool blocked=false,alivePlayer=false;
    for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && int(p.area)==120 && p.persistent.player.hp>0) alivePlayer=true;}
    bool cleared=true;const monsters::Actor *last=nullptr;
    for(const auto name:ancientIdentities) {
        bool spawned=false,dead=false;
        for(const auto &[id,m]:ports_.monsters.read().actors) {
            (void)id;if(int(m.area)!=120 || m.identity.spawnKey!="quest."+std::string(name)) continue;
            spawned=true;dead=m.life<=0;
            if(dead && (!last || m.deathTick>last->deathTick || (m.deathTick==last->deathTick && m.deathOccurrence>last->deathOccurrence))) last=&m;
        }
        if(!spawned && tick.tick>=world.ancientAt && !world.ancientsDefeated) {
            for(const auto &[id,p]:ports_.players.all()) if(p.entered && int(p.area)==120 && p.persistent.player.hp>0) {
                const ActorContext actor{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick};
                if(!spawnQuestGroup(actor,name)) blocked=true;
                break;
            }
        }
        cleared=cleared && spawned && dead;
    }
    if(cleared && !world.ancientsDefeated) {
        // A5Q5 awards at the last Ancient's death, including same-act party
        // members. Eligibility is not frozen when the altar is activated.
        captureGoal(QuestId::RiteOfPassage,4,RegionId(120),{},last?killerPlayer(last->killer):std::nullopt);
        world.ancientsDefeated=true;world.ancientsActive=false;
    }
    if(!alivePlayer && !world.ancientsDefeated) resetAncients();
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
