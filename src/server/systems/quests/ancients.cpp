#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
namespace d2x::server::quests {
void System::resetAncients() {
    auto &world=state_.actFive;
    if(!world.ancientsActive || world.ancientsDefeated) return;
    for(const auto name:ancientIdentities) ports_.monsters.withdrawQuestGroup(RegionId(120),name);
    world.ancientsActive=false;world.ancientAt=0;world.ancientEligible.clear();
    if(const auto *area=ports_.areas.find(RegionId(120))) for(const auto &object:area->definition.objects)
        if(object.rule.operation>=62 && object.rule.operation<=65) state_.objectModes[object.id]=0;
}
StepStatus System::ancientsStep(TickContext tick) {
    auto &world=state_.actFive;if(!world.ancientsActive && !world.ancientsDefeated) return StepStatus::Complete;
    bool blocked=false,alivePlayer=false;
    for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && int(p.area)==120 && p.persistent.player.hp>0) alivePlayer=true;}
    if(!alivePlayer && !world.ancientsDefeated) {resetAncients();return StepStatus::Complete;}
    bool cleared=true;
    for(const auto name:ancientIdentities) {
        bool spawned=false,dead=false;
        for(const auto &[id,m]:ports_.monsters.read().actors) {(void)id;if(int(m.area)==120 && m.identity.spawnKey=="quest."+std::string(name)) {spawned=true;dead=m.life<=0;}}
        if(!spawned && tick.tick>=world.ancientAt && !world.ancientsDefeated) {
            for(const auto &[id,p]:ports_.players.all()) if(p.entered && int(p.area)==120 && p.persistent.player.hp>0) {
                const ActorContext actor{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick};
                if(!spawnQuestGroup(actor,name)) blocked=true;
                break;
            }
        }
        cleared=cleared && spawned && dead;
    }
    if(cleared) {world.ancientsDefeated=true;world.ancientsActive=false;}
    if(!world.ancientsDefeated) return blocked?StepStatus::Blocked:StepStatus::Complete;
    constexpr std::array minimumLevel{20,40,60};
    constexpr std::array<uint64_t,3> rewards{1400000,20000000,40000000};
    for(auto member=world.ancientEligible.begin();member!=world.ancientEligible.end();) {
        const auto *p=ports_.players.find(*member);
        if(!p || !p->entered || int(p->area)!=120 || p->persistent.player.hp<=0 || p->persistent.player.level<minimumLevel.at(size_t(ports_.settings.difficulty))) {member=world.ancientEligible.erase(member);continue;}
        auto record=p->persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
        if(book[questIndex(QuestId::RiteOfPassage)].stage>=4) {member=world.ancientEligible.erase(member);continue;}
        const auto &thresholds=p->rules.character->experience;const auto level=size_t(record.level);
        if(level+1<thresholds.size() && record.experience<thresholds.back()) {
            const auto amount=std::min(rewards.at(size_t(ports_.settings.difficulty)),thresholds[level+1]-thresholds[level]);
            const auto planned=progression::addExperience(record,p->definition,*p->rules.character,amount);
            if(!planned) {blocked=true;++member;continue;}record=*planned.value;
        }
        record.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::RiteOfPassage)]={4,0};
        auto &baal=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::EveOfDestruction)];if(!baal.stage) baal.stage=1;
        if(commit({p->player,p->actor,p->area,ports_.areas.at(p->area).generation,0,tick.tick},record,record.level>p->persistent.player.level)) member=world.ancientEligible.erase(member);
        else {blocked=true;++member;}
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
