#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"

namespace d2x::server::quests {
void System::captureGoal(QuestId quest,uint32_t stage,RegionId area) {
    if(state_.objectives.at(questIndex(quest))) return;
    Goal goal{stage,{}};
    for(const auto &[id,p]:ports_.players.all())
        if(p.entered && p.area==area && p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest)).stage<stage)
            goal.eligible.insert(id);
    state_.goals.insert_or_assign(quest,std::move(goal));
    state_.objectives.at(questIndex(quest))=true;
}
StepStatus System::flushGoals(TickContext tick) {
    bool blocked=false;
    for(auto it=state_.goals.begin();it!=state_.goals.end();) {
        for(auto member=it->second.eligible.begin();member!=it->second.eligible.end();) {
            const auto *p=ports_.players.find(*member);
            if(!p || !p->entered) {member=it->second.eligible.erase(member);continue;}
            auto record=p->persistent.player;
            auto &quest=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(it->first));
            if(quest.stage>=it->second.stage) {member=it->second.eligible.erase(member);continue;}
            quest.stage=it->second.stage;
            if(it->first==QuestId::RadamentsLair && !(quest.flags&radamentBookUsed)) quest.flags|=radamentBookPending;
            if(it->first==QuestId::ArcaneSanctuary) quest.flags|=arcaneCommentPending;
            if(it->first==QuestId::Guardian) quest.flags|=guardianSpeechPending;
            if(it->first==QuestId::TerrorsEnd) quest.flags|=terrorTyraelPending|terrorCainPending;
            if(it->first==QuestId::RescueOnMountArreat) quest.flags=(quest.flags&~rescueCountMask)|state_.actFive.rescued;
            const auto &area=ports_.areas.at(p->area);
            if(commit({p->player,p->actor,p->area,area.generation,0,tick.tick},std::move(record))) member=it->second.eligible.erase(member);
            else {blocked=true;++member;}
        }
        if(it->second.eligible.empty()) it=state_.goals.erase(it);else ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
