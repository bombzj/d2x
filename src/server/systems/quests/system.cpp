#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include <algorithm>
namespace d2x::server::quests {
std::optional<NpcMessage> System::dialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player); if(!p || npc.code!="akara") return {};
    const auto stage=DenStage(p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil)).stage);
    const uint16_t text=stage==DenStage::Unstarted?64:stage==DenStage::Assigned?65:stage==DenStage::Entered?71:stage==DenStage::Cleared?76:0;
    if(!npc.denMessages.contains(text)) return {};
    return NpcMessage{uint8_t(stage==DenStage::Unstarted || stage==DenStage::Cleared?0:2),text};
}
DomainResult<> System::commit(const ActorContext &actor,CharacterRecord record) {
    const auto &player=*ports_.players.find(actor.player);
    transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(record)};
    edit.facts.emplace_back(QuestFact{actor.player,edit.player,ports_.settings.difficulty,state_.denRemaining});
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    return ports_.transactions.commit(std::move(*plan.value));
}
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    const auto *p=ports_.players.find(actor.player); if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area) return {DomainStatus::InvalidActor,{}};
    if(request.action==Action::Refresh) {
        auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{QuestFact{actor.player,p->persistent.player,ports_.settings.difficulty,state_.denRemaining}}});
        return {sent.status,sent?std::optional<std::monostate>{std::monostate{}}:std::nullopt};
    }
    if(request.quest!=QuestId::DenOfEvil || !request.npc || !request.message) return {};
    const auto *npc=ports_.npc.find(actor,*request.npc,true); if(!npc || npc->rule.code!="akara") return {DomainStatus::InvalidRequest,{}};
    const auto expected=dialogue(actor,npc->rule); if(!expected || expected->text!=*request.message) return {DomainStatus::Stale,{}};
    auto record=p->persistent.player; auto &den=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil));
    if(*request.message==64) { if(!denAdvanceOnTalk(den)) return {DomainStatus::Stale,{}}; }
    else if(*request.message==76) { if(!denClaimReward(den) || record.unspentSkills==INT32_MAX) return {DomainStatus::Conflict,{}}; ++record.unspentSkills; }
    else return {DomainStatus::Applied,std::monostate{}};
    return commit(actor,std::move(record));
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    const auto *area=ports_.areas.find(RegionId(8));
    if(area) {
        size_t alive=area->definition.populationMissing, killed=0, admitted=0;
        for(const auto &[id,m]:ports_.monsters.read().actors) { (void)id; if(m.area!=RegionId(8) || m.owner || m.identity.origin==SpawnOrigin::Debug) continue; ++admitted; if(m.life>0) ++alive; else ++killed; }
        alive+=area->definition.population.size()>admitted?area->definition.population.size()-admitted:0;
        state_.denRemaining=unsigned(std::min<size_t>(alive,UINT16_MAX));
        const auto population=ports_.population.read().areas.find(RegionId(8));
        if(!state_.denCleared && killed && !alive && population!=ports_.population.read().areas.end() && population->second.generation==area->generation) {
            for(const auto &[id,p]:ports_.players.all()) if(p.entered && p.area==RegionId(8)) state_.eligible.insert(id);
            state_.denCleared=true;
        }
    }
    bool blocked=false;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        auto record=p.persistent.player; auto &den=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil));
        bool changed=false;
        if(p.area==RegionId(8)) changed=denAdvanceOnEntry(den);
        if(state_.eligible.contains(id)) changed=denAdvanceOnClear(den,true,false) || changed;
        if(!changed && (p.area!=RegionId(8) || (state_.observed.contains(id) && state_.observed.at(id)==state_.denRemaining))) continue;
        const auto &at=ports_.areas.at(p.area);
        const auto result=commit({id,p.actor,p.area,at.generation,0,tick.tick},std::move(record));
        if(result) state_.observed[id]=state_.denRemaining; else blocked=true;
    }
    std::erase_if(state_.observed,[&](const auto &entry){return !ports_.players.find(entry.first);});
    std::erase_if(state_.eligible,[&](PlayerId id){return !ports_.players.find(id);});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
