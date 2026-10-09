#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::quests {
void System::orderStones() {
    if(state_.actOne.stonesOrdered) return;
    auto random=ports_.random;
    // ACT1Q4_SetMonolithOrder: assign each original class to a random empty
    // position, retaining rejection draws. It is per game, not map-seed UI data.
    for(int stone=17;stone<=21;++stone) {
        size_t slot;
        do {slot=limitedRandom(random,5);} while(state_.actOne.stones[slot]);
        state_.actOne.stones[slot]=stone;
    }
    ports_.random=random;state_.actOne.stonesOrdered=true;
}
bool System::npcVisible(const CharacterRecord &record,std::string_view code,RegionId area,int initFunction) const {
    if(code=="hratli" && int(area)==75 && (initFunction==49 || initFunction==50)) {
        bool welcomed=false;
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.persistent.player.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(QuestPreludeId::KurastArrival))) welcomed=true;}
        return initFunction==(welcomed?50:49);
    }
    if(code=="nihlathak" && int(area)==109) return !state_.actFive.anyaThawed;
    if(code=="drehya" && int(area)==109) return state_.actFive.anyaThawed;
    if(code=="baalthrone" && int(area)==131) return !state_.actFive.throneDeparted;
    if(code=="jerhyn" && int(area)==40 && (initFunction==18 || initFunction==19)) {
        bool palace=false;
        for(const auto &[id,p]:ports_.players.all()) {
            (void)id;const auto difficulty=size_t(ports_.settings.difficulty);
            const auto &book=p.persistent.player.quests.at(difficulty);
            if(p.entered && (p.persistent.player.questPreludes.at(difficulty).at(size_t(QuestPreludeId::LutGholeinArrival)) || book.at(questIndex(QuestId::RadamentsLair)).stage>=uint32_t(RadamentStage::Slain) || book.at(questIndex(QuestId::ArcaneSanctuary)).stage>0 || book.at(questIndex(QuestId::SevenTombs)).stage>0)) palace=true;
        }
        return initFunction==(palace?19:18);
    }
    if(code=="tyrael1" && int(area)==73) return state_.actTwo.durielSlain || record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SevenTombs)).stage>=2;
    if(!code.starts_with("cain")) return true;
    if(int(area)==38) return !state_.actOne.cainRescued;
    if(int(area)!=1) return true;
    return state_.actOne.cainRescued || record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::Rescued);
}
DomainResult<> System::operate(const ActorContext &actor,EntityId source,int definition,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    if(ports_.areas.at(actor.area).definition.act==1) return operateActTwo(actor,source,definition,operation,position);
    if(ports_.areas.at(actor.area).definition.act==2) return operateActThree(actor,source,definition,operation,position);
    if(ports_.areas.at(actor.area).definition.act==3) return operateActFour(actor,source,definition,operation,position);
    if(ports_.areas.at(actor.area).definition.act==4) return operateActFive(actor,source,definition,operation,position);
    auto record=p->persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    if(operation==6 && definition==8) {
        if(!towerAdvance(book.at(questIndex(QuestId::ForgottenTower)),TowerStage::TomeRead)) return {DomainStatus::Applied,std::monostate{}};
        return commit(actor,std::move(record));
    }
    auto &cain=book.at(questIndex(QuestId::SearchForCain));
    if(operation==12 && int(actor.area)==5) {
        if(cain.stage>=uint32_t(CainStage::Rescued) || detail::carried(*p,"bks",ports_.settings.difficulty) || detail::carried(*p,"bkd",ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        // A new game can replace a lost scroll. Do not permanently lock the
        // tree from a saved journal stage without its actual original item.
        for(const auto &[id,item]:ports_.items.read().world.items) {
            (void)id;if(item.definition=="bks" && item.nativeQuestDifficulty==unsigned(ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        }
        return prepareReward(actor,source,RewardKind::Bark,false);
    }
    if(operation==21 && definition==108 && int(actor.area)==28) {
        auto &tools=book.at(questIndex(QuestId::ToolsOfTheTrade));
        if(p->persistent.player.level<8 || tools.stage>=uint32_t(ToolsStage::RewardReady) || detail::carried(*p,"hdm",ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        for(const auto &[id,item]:ports_.items.read().world.items) { (void)id;if(item.definition=="hdm") return {DomainStatus::Conflict,{}}; }
        return prepareReward(actor,source,RewardKind::Malus,false);
    }
    if(operation==9 && int(actor.area)==4 && definition>=17 && definition<=21) {
        orderStones();
        if(!detail::carried(*p,"bkd",ports_.settings.difficulty) && cain.stage<uint32_t(CainStage::ScrollTranslated)) return {DomainStatus::InvalidRequest,{}};
        if(state_.actOne.activatedStones>=5) return {DomainStatus::Applied,std::monostate{}};
        if(state_.actOne.stones[state_.actOne.activatedStones]!=definition) return {DomainStatus::Conflict,{}};
        std::optional<travel::SpecialPortalPlan> portal;
        if(state_.actOne.activatedStones==4) {
            const auto *area=ports_.areas.find(actor.area);
            const auto alpha=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[](const auto &o){return o.type==17;});
            if(alpha==area->definition.objects.end()) return {DomainStatus::Unavailable,{}};
            const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::Tristram,0,alpha->position+Vec{4,4});
            if(!result) return {result.status,{}};
            portal=*result.value;
        }
        cain.stage=std::max(cain.stage,uint32_t(CainStage::ScrollTranslated));
        cain.flags=(cain.flags&~cainStoneCountMask)|(state_.actOne.activatedStones+1);
        if(state_.actOne.activatedStones==4) cain.stage=std::max(cain.stage,uint32_t(CainStage::PortalOpened));
        const auto result=commit(actor,std::move(record));
        if(result) {++state_.actOne.activatedStones;if(portal) {ports_.travel.commitSpecialPortal(std::move(*portal));state_.actOne.cainPortalOpened=true;}}
        return result;
    }
    if(operation==10 && int(actor.area)==38) {
        if(state_.actOne.cainRescued) return {DomainStatus::Applied,std::monostate{}};
        const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::CainRescue,0,position+Vec{3,3});
        if(!portal) return {portal.status,{}};
        // Capture only present eligible characters. The personal pending reward
        // persists; late entrants never acquire another player's ring.
        std::set<PlayerId> eligible;
        for(const auto &[id,player]:ports_.players.all()) if(player.entered && int(player.area)==38 &&
            player.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SearchForCain)).stage<uint32_t(CainStage::Rescued)) eligible.insert(id);
        state_.goals.insert_or_assign(QuestId::SearchForCain,Goal{uint32_t(CainStage::Rescued),std::move(eligible)});state_.actOne.cainRescued=true;
        ports_.travel.commitSpecialPortal(*portal.value);
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::NotImplemented,{}};
}
StepStatus System::actOneStep(TickContext tick) {
    bool blocked=false;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        (void)id;if(m.life>0 || m.owner || m.identity.origin==SpawnOrigin::Debug) continue;
        std::optional<QuestId> goal;
        if(int(m.area)==17 && m.identity.monster=="bloodraven") goal=QuestId::SistersBurialGrounds;
        if(int(m.area)==25 && m.identity.superUnique=="The Countess") goal=QuestId::ForgottenTower;
        if(int(m.area)==37 && m.identity.monster=="andariel") goal=QuestId::SistersToTheSlaughter;
        if(!goal || state_.objectives[questIndex(*goal)]) continue;
        std::set<PlayerId> eligible;
        for(const auto &[playerId,p]:ports_.players.all()) if(p.entered && p.area==m.area) eligible.insert(playerId);
        const uint32_t stage=*goal==QuestId::ForgottenTower?uint32_t(TowerStage::CountessSlain):*goal==QuestId::SistersBurialGrounds?uint32_t(BurialStage::BloodRavenSlain):uint32_t(SlaughterStage::AndarielSlain); state_.goals.insert_or_assign(*goal,Goal{stage,std::move(eligible)});state_.objectives[questIndex(*goal)]=true;
        // Game.cpp calls QuestUpdater every 20 frames; timeout 1 uses a
        // strict '<', hence each invocation consumes two updater ticks.
        if(*goal==QuestId::SistersToTheSlaughter) state_.actOne.andarielPortalAt=(tick.tick/20+9*2)*20;
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
        const auto before=book;const int area=int(p.area);
        // A1Q4_PlayerStartedGame restores opened stones; InitFunction06 on
        // STONEALPHA schedules the real red portal when that area is loaded.
        if(state_.actOne.restoreCairnStones && !state_.actOne.cainPortalOpened && area==4 && p.persistent.player.hp>0) {
            const auto &source=ports_.areas.at(p.area);
            const auto alpha=std::find_if(source.definition.objects.begin(),source.definition.objects.end(),[](const auto &object){return object.type==17;});
            if(alpha!=source.definition.objects.end()) {
                const auto portal=ports_.travel.prepareSpecialPortal({id,p.actor,p.area,source.generation,0,tick.tick},travel::SpecialPortalKind::Tristram,0,alpha->position+Vec{4,4});
                if(portal) {ports_.travel.commitSpecialPortal(*portal.value);state_.actOne.cainPortalOpened=true;} else blocked=true;
            }
        }
        if(area==17) burialAdvanceOnEntry(book[questIndex(QuestId::SistersBurialGrounds)]);
        if(area>=20 && area<=25) towerAdvance(book[questIndex(QuestId::ForgottenTower)],area==20?TowerStage::TowerEntered:TowerStage::CellarEntered);
        if(area==28) toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],ToolsStage::BarracksEntered);
        if(area==37) slaughterAdvance(book[questIndex(QuestId::SistersToTheSlaughter)],SlaughterStage::CatacombsEntered);
        auto &cain=book[questIndex(QuestId::SearchForCain)];
        if(record.completedActs.at(size_t(ports_.settings.difficulty)).at(0) && cain.stage<uint32_t(CainStage::Rescued)) {
            cain.stage=uint32_t(CainStage::Rewarded);cain.flags=cainRescuedByRogues;
        }
        if(!state_.actOne.andarielPortal && state_.actOne.andarielPortalAt && tick.tick>=state_.actOne.andarielPortalAt && area==37 && p.persistent.player.hp>0) {
            const auto portal=ports_.travel.prepareSpecialPortal({id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick},travel::SpecialPortalKind::Andariel,0,p.position);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);state_.actOne.andarielPortal=true;} else blocked=true;
        }
        if(area==38) cainAdvance(cain,CainStage::TristramEntered);
        if(detail::carried(p,"bks",ports_.settings.difficulty)) cainAdvance(cain,CainStage::BarkAcquired);
        if(detail::carried(p,"bkd",ports_.settings.difficulty)) cainAdvance(cain,CainStage::ScrollTranslated);
        if(detail::carried(p,"hdm",ports_.settings.difficulty)) toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],ToolsStage::MalusAcquired);
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;})) {
            if(!commit({id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick},std::move(record))) blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
