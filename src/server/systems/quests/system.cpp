#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "detail.hpp"
#include "gameplay/character/respec.hpp"
#include <algorithm>
namespace d2x::server::quests {
std::optional<Dialogue> System::dialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player); if(!p) return {};
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto stage=[&](QuestId id){return book.at(questIndex(id)).stage;};
    const auto emit=[&](QuestId id,uint16_t text,uint8_t menu=1)->std::optional<Dialogue> {
        if(!npc.questMessages.contains(text)) return {};
        return Dialogue{id,{menu,text}};
    };
    if(npc.code=="akara") {
        const auto cain=stage(QuestId::SearchForCain);
        if(cain==3 && detail::carried(*p,"bks",ports_.settings.difficulty)) return emit(QuestId::SearchForCain,112,0);
        if(cain==7) return emit(QuestId::SearchForCain,118);
        const auto den=stage(QuestId::DenOfEvil);
        if(den==0 || den==3) return emit(QuestId::DenOfEvil,den==0?64:76,0);
        if(cain==0) return emit(QuestId::SearchForCain,97);
        if(den==1 || den==2) return emit(QuestId::DenOfEvil,den==1?65:71,2);
    }
    if(npc.code=="kashya") {
        const auto burial=stage(QuestId::SistersBurialGrounds);
        // A1Q1 message76 advances A1Q2's sequence to its Kashya offer.
        // Killing Blood Raven without accepting the offer still qualifies.
        if(burial==0 && !state_.objectives[questIndex(QuestId::DenOfEvil)]) return {};
        if(burial==0 || burial==3) return emit(QuestId::SistersBurialGrounds,burial==0?81:92,burial==3?0:1);
        if(burial==1 || burial==2) return emit(QuestId::SistersBurialGrounds,burial==1?82:87,2);
    }
    if(npc.code=="charsi") {
        const auto tools=stage(QuestId::ToolsOfTheTrade);
        if(tools<5 && p->persistent.player.level>=8 && detail::carried(*p,"hdm",ports_.settings.difficulty)) return emit(QuestId::ToolsOfTheTrade,163,0);
        if(tools==0 && p->persistent.player.level>=8) return emit(QuestId::ToolsOfTheTrade,146);
        if(tools>0 && tools<5) return emit(QuestId::ToolsOfTheTrade,150,2);
    }
    if(npc.code.starts_with("cain") && stage(QuestId::SistersToTheSlaughter)==0) return emit(QuestId::SistersToTheSlaughter,166);
    if(npc.code=="warriv1" && stage(QuestId::SistersToTheSlaughter)==3) return emit(QuestId::SistersToTheSlaughter,183,0);
    return {};
}
DomainResult<> System::commit(const ActorContext &actor,CharacterRecord record) {
    const auto *player=ports_.players.find(actor.player); if(!player) return {DomainStatus::InvalidActor,{}};
    const bool denRewarded=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil)).stage>=uint32_t(DenStage::Rewarded);
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
    edit.facts.emplace_back(QuestFact{actor.player,edit.player,ports_.settings.difficulty,state_.denRemaining,state_.stones});
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result && denRewarded) state_.objectives[questIndex(QuestId::DenOfEvil)]=true;
    return result;
}
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(request.action==Action::Refresh) {
        orderStones();
        if(p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil)).stage>=uint32_t(DenStage::Rewarded)) state_.objectives[questIndex(QuestId::DenOfEvil)]=true;
        // ACT1Q4_Callback13_PlayerStartedGame restores the completed game's
        // Cain/gibbet state from RewardGranted or CompletedBefore.
        if(p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::Rewarded)) {
            state_.cainRescued=true;
            state_.restoreCairnStones=true;
            state_.activatedStones=5;
        }
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{QuestFact{actor.player,p->persistent.player,ports_.settings.difficulty,state_.denRemaining,state_.stones}}});
        return {sent.status,sent?std::optional<std::monostate>{std::monostate{}}:std::nullopt};
    }
    if(request.action==Action::ClaimRespec) {
        if(!request.npc) return {DomainStatus::InvalidRequest,{}};
        const auto *npc=ports_.npc.find(actor,*request.npc,true);
        if(!npc || npc->rule.code!="akara" || p->persistent.player.hp<=0) return {DomainStatus::InvalidRequest,{}};
        auto record=p->persistent.player;
        if(!denClaimRespec(record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil)))) return {DomainStatus::Conflict,{}};
        if(!refundCharacterPoints(record)) return {DomainStatus::Capacity,{}};
        return commit(actor,std::move(record));
    }
    if(!request.npc || !request.message) return {DomainStatus::InvalidRequest,{}};
    const auto *npc=ports_.npc.find(actor,*request.npc,true);if(!npc) return {DomainStatus::InvalidRequest,{}};
    const auto expected=dialogue(actor,npc->rule);
    if(!expected || expected->quest!=request.quest || expected->message.text!=*request.message) return {DomainStatus::Stale,{}};
    auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(request.quest));
    switch(*request.message) {
    case 64: if(!denAdvanceOnTalk(q)) return {DomainStatus::Stale,{}};break;
    case 76: if(!denClaimReward(q) || record.unspentSkills==INT32_MAX) return {DomainStatus::Conflict,{}}; ++record.unspentSkills;break;
    case 81: if(!burialAdvanceOnTalk(q,state_.objectives[questIndex(QuestId::DenOfEvil)])) return {DomainStatus::Stale,{}};break;
    case 92: return prepareReward(actor,*request.npc,RewardKind::Rogue,true);
    case 97: cainAdvance(q,CainStage::Assigned);break;
    case 112: return prepareReward(actor,*request.npc,RewardKind::TranslateScroll,true);
    case 118: return prepareReward(actor,*request.npc,RewardKind::CainRing,true);
    case 146: toolsAdvance(q,ToolsStage::Assigned);break;
    case 163: return prepareReward(actor,*request.npc,RewardKind::Malus,true);
    case 166: slaughterAdvance(q,SlaughterStage::Assigned);break;
    case 183: slaughterAdvance(q,SlaughterStage::PassageReady);break;
    default: return {DomainStatus::Applied,std::monostate{}};
    }
    return commit(actor,std::move(record));
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    const auto den=denStep(tick),act=actOneStep(tick);
    std::erase_if(state_.pending,[&](const auto &entry){
        const auto *p=ports_.players.find(entry.first);const auto &s=entry.second;
        const auto *area=ports_.areas.find(s.actor.area);
        const auto *lease=ports_.npc.conversation(entry.first);
        const bool stale=!p || !p->entered || p->actor!=s.actor.actor || p->area!=s.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=s.actor.areaGeneration || (s.conversation && (!lease || lease->revision!=s.conversation));
        if(stale && !s.conversation) state_.completed.insert_or_assign(s.source,false);
        return stale;
    });
    return den==StepStatus::Blocked || act==StepStatus::Blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::denStep(TickContext tick) {
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
