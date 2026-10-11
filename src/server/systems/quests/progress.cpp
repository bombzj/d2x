#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/social/system.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>

namespace d2x::server::quests {
void System::initialize(const CharacterRecord &creator) {
    // QUESTS_SequenceCycler selects the first player's completed set once.
    // Horadric Staff has bNoSetState and remains available in completed games.
    for(const auto &q:questDefinitions) if(q.id!=QuestId::HoradricStaff &&
        creator.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(q.id)).stage>=q.completedStage)
        state_.initialFlags[q.nativeSlot]=0x8000;
    const auto &book=creator.quests.at(size_t(ports_.settings.difficulty));
    state_.actOne.cainOffer=(state_.initialFlags[2]&0x8000)!=0;
    state_.actOne.burialOffer=(state_.initialFlags[1]&0x8000)!=0;
    state_.actOne.toolsOffer=(state_.initialFlags[4]&0x8000)!=0;
    state_.actThree.orbSmashed=book[questIndex(QuestId::KhalimsWill)].stage>=4;
    state_.actThree.mephistoSlain=book[questIndex(QuestId::Guardian)].stage>=4;
    state_.actFive.anyaThawed=book[questIndex(QuestId::PrisonOfIce)].stage>=5;
    state_.actFive.ancientsDefeated=book[questIndex(QuestId::RiteOfPassage)].stage>=4;
    if(book[questIndex(QuestId::SearchForCain)].stage>=8) {
        state_.actOne.cainRescued=true;state_.actOne.restoreCairnStones=true;state_.actOne.activatedStones=5;
    }
    const auto sun=book[questIndex(QuestId::TaintedSun)].stage;
    state_.actTwo.dark=sun==1 || sun==2;state_.actTwo.sunScheduled=state_.actTwo.dark;
}
std::array<uint16_t,48> System::gameFlags() const {
    auto flags=state_.initialFlags;
    for(const auto &q:questDefinitions) if(state_.objectives[questIndex(q.id)]) flags[q.nativeSlot]|=0x2000;
    return flags;
}
std::set<PlayerId> System::eligibleParty(QuestId quest,uint32_t stage,RegionId area,std::optional<Vec> source) const {
    std::set<PlayerId> direct,result;
    const auto &definition=questDefinition(quest);
    if(state_.initialFlags[definition.nativeSlot]&0x8000) return result;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || (quest==QuestId::DenOfEvil?!state_.actOne.contributors.contains(id):p.area!=area) || p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest)).stage>=stage) continue;
        if(source) {
            const auto &layout=ports_.areas.at(area).definition.activation;
            const auto *room=layout.room(p.position),*target=layout.room(*source);
            if(!room || !target) continue;
            const auto near=layout.nearRooms(*room);
            if(std::find(near.begin(),near.end(),target)==near.end()) continue;
        }
        direct.insert(id);
    }
    result=direct;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=definition.act ||
            p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest)).stage>=stage) continue;
        if(std::any_of(direct.begin(),direct.end(),[&](PlayerId member){return ports_.social.sameParty(id,member);})) result.insert(id);
    }
    return result;
}
void System::captureGoal(QuestId quest,uint32_t stage,RegionId area,std::optional<Vec> source) {
    if(state_.objectives.at(questIndex(quest))) return;
    Goal goal{stage,eligibleParty(quest,stage,area,source)};
    for(const auto id:goal.eligible) state_.credited[id].insert(quest);
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
