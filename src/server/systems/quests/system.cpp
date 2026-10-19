#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "detail.hpp"
#include "gameplay/character/respec.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include "core/random.hpp"
namespace d2x::server::quests {
void System::initializeControl() {
    if(state_.controlRandom) throw std::logic_error("Quest control is already initialized");
    state_.controlRandom=childRandom(ports_.random);
}
void System::rebindObject(EntityId previous,EntityId current) {
    if(previous==current) return;
    if(auto mode=state_.objectModes.extract(previous);!mode.empty()) {mode.key()=current;state_.objectModes.insert(std::move(mode));}
    if(state_.actThree.gidbinnAltar==previous) state_.actThree.gidbinnAltar=current;
    if(state_.actFive.ancientAltar==previous) state_.actFive.ancientAltar=current;
    for(auto &[player,reader]:state_.actFive.ancientAltarReaders) {(void)player;if(reader.altar==previous) reader.altar=current;}
    for(auto &ancient:state_.actFive.ancients) if(ancient.statue==previous) ancient.statue=current;
    if(state_.actOne.cairnPending && state_.actOne.cairnPending->owner==previous) state_.actOne.cairnPending->owner=current;
    if(state_.actOne.cainInvisibleObject==previous) state_.actOne.cainInvisibleObject=current;
    if(state_.actOne.cainStartObject==previous) state_.actOne.cainStartObject=current;
    for(auto &id:state_.actOne.monoliths) if(id==previous) id=current;
    for(auto &[player,id]:state_.actTwo.orifices) {(void)player;if(id==previous) id=current;}
    for(auto &[player,id]:state_.actTwo.journals) {(void)player;if(id==previous) id=current;}
    if(auto entry=state_.actFour.sealAfterglows.extract(previous);!entry.empty()) {entry.key()=current;state_.actFour.sealAfterglows.insert(std::move(entry));}
}
std::optional<Dialogue> System::dialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *area=ports_.areas.find(actor.area);
    if(!area) return {};
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    std::optional<Dialogue> offered;
    if(area->definition.act==0) offered=actOneDialogue(actor,npc);
    if(area->definition.act==1) offered=actTwoDialogue(actor,npc);
    if(area->definition.act==2) offered=actThreeDialogue(actor,npc);
    if(area->definition.act==3) offered=actFourDialogue(actor,npc);
    if(area->definition.act==4) offered=actFiveDialogue(actor,npc);
    // Jerhyn's arrival speech is an independent A2Q0 prelude.
    if(offered && offered->message.text!=253 && unavailable(actor.player,p->persistent.player,offered->quest)) return {};
    return offered;
}
std::optional<Dialogue> System::actOneDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player); if(!p) return {};
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto stage=[&](QuestId id){return book.at(questIndex(id)).stage;};
    const auto emit=[&](QuestId id,uint16_t text,uint8_t menu=1)->std::optional<Dialogue> {
        if(!npc.questMessages.contains(text)) return {};
        return Dialogue{id,{menu,text}};
    };
    if(npc.code=="akara") {
        const auto cain=stage(QuestId::SearchForCain);
        if(!unavailable(actor.player,p->persistent.player,QuestId::SearchForCain) && cain==3 && detail::carried(*p,"bks",ports_.settings.difficulty)) return emit(QuestId::SearchForCain,112,0);
        if(cain==7) return emit(QuestId::SearchForCain,118);
        const auto den=stage(QuestId::DenOfEvil);
        if((den==0 || den==3) && !unavailable(actor.player,p->persistent.player,QuestId::DenOfEvil)) return emit(QuestId::DenOfEvil,den==0?64:76,0);
        // A1Q2_SeqCallback activates Cain's offer after Blood Raven's
        // reward, or immediately when the creator already completed A1Q2.
        if(cain==0 && state_.actOne.cainOffer && !unavailable(actor.player,p->persistent.player,QuestId::SearchForCain)) return emit(QuestId::SearchForCain,97);
        if(den==1 || den==2) return emit(QuestId::DenOfEvil,den==1?65:71,2);
    }
    if(npc.code=="kashya") {
        const auto burial=stage(QuestId::SistersBurialGrounds);
        // A1Q1 message76 advances A1Q2's sequence to its Kashya offer.
        // Killing Blood Raven without accepting the offer still qualifies.
        if(burial==0 && !state_.actOne.burialOffer) return {};
        if(burial==0 || burial==3) return emit(QuestId::SistersBurialGrounds,burial==0?81:92,burial==3?0:1);
        if(burial==1 || burial==2) return emit(QuestId::SistersBurialGrounds,burial==1?82:87,2);
    }
    if(npc.code=="charsi") {
        const auto tools=stage(QuestId::ToolsOfTheTrade);
        if(tools<5 && p->persistent.player.level>=8 && detail::actTwoCarried(*p,"hdm",ports_.settings.difficulty)) return emit(QuestId::ToolsOfTheTrade,163,0);
        if(tools==0 && state_.actOne.toolsOffer && p->persistent.player.level>=8) return emit(QuestId::ToolsOfTheTrade,146);
        if(tools>0 && tools<5) return emit(QuestId::ToolsOfTheTrade,150,2);
    }
    if(npc.code.starts_with("cain") && stage(QuestId::SistersToTheSlaughter)==0) return emit(QuestId::SistersToTheSlaughter,166);
    if(npc.code=="warriv1" && stage(QuestId::SistersToTheSlaughter)==3) return emit(QuestId::SistersToTheSlaughter,183,0);
    return {};
}
DomainResult<> System::commit(const ActorContext &actor,CharacterRecord record,bool levelUp,std::optional<QuestId> primaryGoal) {
    const auto *player=ports_.players.find(actor.player); if(!player) return {DomainStatus::InvalidActor,{}};
    auto triggers=levelUp?ports_.itemTriggers.prepare(actor,{ItemSkillEvent::LevelUp},{},player->position):DomainResult<reactions::ItemEventPlan>{DomainStatus::Applied,reactions::ItemEventPlan{}};
    if(!triggers) return {triggers.status,{}};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
    if(levelUp) edit.resources=transactions::ResourceRefresh::LevelUp;
    auto questFact=fact(actor.player,edit.player,actor.area);
    if(primaryGoal) {
        const auto index=questIndex(*primaryGoal);
        questFact.credited[index]=true;questFact.entitled[index]=true;questFact.unavailable[index]=false;
    }
    edit.facts.emplace_back(std::move(questFact));
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result && levelUp) ports_.itemTriggers.commit(std::move(*triggers.value),true);
    return result;
}
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(request.action==Action::ViewLog) {
        if(!request.message || *request.message>=nativeQuestStatusCount) return {DomainStatus::InvalidRequest,{}};
        const auto difficulty=size_t(ports_.settings.difficulty),slot=size_t(*request.message);
        if(p->persistent.player.questLogViewed[difficulty][slot]) return {DomainStatus::Applied,std::monostate{}};
        auto record=p->persistent.player;
        record.questLogViewed[difficulty][slot]=true;
        return commit(actor,std::move(record));
    }
    if(request.action==Action::Refresh) {
        orderStones();
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{projection(actor.player,p->persistent.player,actor.area)}});
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
    if(request.action==Action::StaffUpdate) {
        if(!request.npc || !request.message) return {DomainStatus::InvalidRequest,{}};
        return submitStaff(actor,*request.npc,request.item.value_or(EntityId{}),*request.message);
    }
    if(request.action==Action::ReadJournal) {
        if(!request.npc || request.message!=396) return {DomainStatus::InvalidRequest,{}};
        const auto source=state_.actTwo.journals.find(actor.player);
        if(source==state_.actTwo.journals.end() || source->second!=*request.npc) return {DomainStatus::Stale,{}};
        const auto result=operateActTwo(actor,*request.npc,0,42,p->position);
        if(isDeferred(result.status)) {
            state_.actTwo.journalReads.insert_or_assign(actor.player,actor);
            return {DomainStatus::Applied,std::monostate{}};
        }
        if(result) state_.actTwo.journals.erase(source);
        return result;
    }
    if(request.action==Action::ReadAncientAltar) {
        if(!request.npc || request.message!=20002 || int(actor.area)!=120 || p->persistent.player.hp<=0) return {DomainStatus::InvalidRequest,{}};
        const auto reader=state_.actFive.ancientAltarReaders.find(actor.player);
        if(reader==state_.actFive.ancientAltarReaders.end() || reader->second.altar!=*request.npc ||
           reader->second.actor!=actor.actor || reader->second.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        const auto result=readAncientAltar(actor,*request.npc);
        if(result) state_.actFive.ancientAltarReaders.erase(actor.player);
        return result;
    }
    if(!request.npc || !request.message) return {DomainStatus::InvalidRequest,{}};
    const auto *npc=ports_.npc.find(actor,*request.npc,true);if(!npc) return {DomainStatus::InvalidRequest,{}};
    const auto expected=dialogue(actor,npc->rule);
    if(!expected || expected->quest!=request.quest || expected->message.text!=*request.message) return {DomainStatus::Stale,{}};
    if(area->definition.act==1) return acknowledgeActTwo(actor,request,npc->rule);
    if(area->definition.act==2) return acknowledgeActThree(actor,request,npc->rule);
    if(area->definition.act==3) return acknowledgeActFour(actor,request,npc->rule);
    if(area->definition.act==4) return acknowledgeActFive(actor,request,npc->rule);
    auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(request.quest));
    switch(*request.message) {
    case 64: if(!denAdvanceOnTalk(q)) return {DomainStatus::Stale,{}};break;
    case 76: if(!denClaimReward(q) || record.unspentSkills==INT32_MAX) return {DomainStatus::Conflict,{}}; ++record.unspentSkills;break;
    case 81: if(!burialAdvanceOnTalk(q,state_.actOne.burialOffer)) return {DomainStatus::Stale,{}};break;
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
    const auto result=commit(actor,std::move(record));
    if(result && *request.message==76 && credited(actor.player,QuestId::DenOfEvil)) state_.actOne.burialOffer=true;
    return result;
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    std::erase_if(state_.credited,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered || state_.creditedActors.at(entry.first)!=p->actor;});
    std::erase_if(state_.creditedActors,[&](const auto &entry){return !state_.credited.contains(entry.first);});
    const auto den=denStep(tick),act=actOneStep(tick),two=actTwoStep(tick),three=actThreeStep(tick),four=actFourStep(tick),five=actFiveStep(tick),ancients=ancientsStep(tick),baal=baalStep(tick),goals=flushGoals(tick);
    bool publicationBlocked=false;
    const auto flags=gameFlags();
    if(flags!=state_.publishedFlags) {
        for(const auto &[id,p]:ports_.players.all()) if(p.entered &&
            !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,id,p.area},{projection(id,p.persistent.player,p.area)}})) publicationBlocked=true;
        if(!publicationBlocked) state_.publishedFlags=flags;
    }
    std::erase_if(state_.pending,[&](const auto &entry){
        const auto *p=ports_.players.find(entry.first);const auto &s=entry.second;
        const auto *area=ports_.areas.find(s.actor.area);
        const auto *lease=ports_.npc.conversation(entry.first);
        const bool stale=!p || !p->entered || p->actor!=s.actor.actor || p->area!=s.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=s.actor.areaGeneration || (s.conversation && (!lease || lease->revision!=s.conversation));
        if(stale && !s.conversation && !s.dropPosition) state_.completed.insert_or_assign(s.source,false);
        return stale;
    });
    return publicationBlocked || den==StepStatus::Blocked || act==StepStatus::Blocked || two==StepStatus::Blocked || three==StepStatus::Blocked || four==StepStatus::Blocked || five==StepStatus::Blocked || ancients==StepStatus::Blocked || baal==StepStatus::Blocked || goals==StepStatus::Blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::denStep(TickContext tick) {
    const auto *area=ports_.areas.find(RegionId(8));
    if(area) {
        size_t alive=0,killed=0;
        for(const auto &[id,m]:ports_.monsters.read().actors) {
            (void)id;if(m.area!=RegionId(8) || m.owner || m.questNpc || m.uncounted || m.rule.environmentController || m.identity.origin==SpawnOrigin::Debug) continue;
            if(m.alive()) {++alive;continue;}++killed;
            if(const auto *killer=ports_.players.findActor(m.killer)) state_.actOne.contributors[killer->player]=killer->actor;
            else if(const auto pet=ports_.monsters.read().actors.find(m.killer);pet!=ports_.monsters.read().actors.end() && pet->second.owner)
                if(const auto *owner=ports_.players.find(*pet->second.owner)) state_.actOne.contributors[owner->player]=owner->actor;
        }
        for(const auto &[room,units]:ports_.monsters.read().inactive) {
            if(room.first!=RegionId(8)) continue;
            for(const auto &unit:units) {
                if(ports_.monsters.find(unit.id) || unit.identity.origin==SpawnOrigin::Debug || !unit.hostile || unit.uncounted ||
                    unit.admissionRule->environmentController) continue;
                if(!unit.dead) ++alive;
                else ++killed;
            }
        }
        const bool generated=ports_.population.coverage(RegionId(8)).complete();
        state_.actOne.denRemaining=unsigned(std::min<size_t>(alive,UINT16_MAX));
        state_.actOne.denHasKills|=killed!=0;
        if(!state_.actOne.denCleared && state_.actOne.denHasKills && !alive && generated) {
            captureGoal(QuestId::DenOfEvil,uint32_t(DenStage::Cleared),RegionId(8));
            state_.actOne.denCleared=true;
        }
    }
    bool blocked=false;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        if(unavailable(id,p.persistent.player,QuestId::DenOfEvil)) continue;
        auto record=p.persistent.player; auto &den=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::DenOfEvil));
        bool changed=false;
        if(p.area==RegionId(8)) changed=denAdvanceOnEntry(den);
        if(!changed && (p.area!=RegionId(8) || (state_.actOne.observed.contains(id) && state_.actOne.observed.at(id)==state_.actOne.denRemaining))) continue;
        const auto &at=ports_.areas.at(p.area);
        const auto result=commit({id,p.actor,p.area,at.generation,0,tick.tick},std::move(record));
        if(result) state_.actOne.observed[id]=state_.actOne.denRemaining; else blocked=true;
    }
    std::erase_if(state_.actOne.observed,[&](const auto &entry){return !ports_.players.find(entry.first);});
    std::erase_if(state_.actOne.contributors,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || p->actor!=entry.second;});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
