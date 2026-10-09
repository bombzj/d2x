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
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::quests {
void System::orderStones() {
    if(state_.stonesOrdered) return;
    auto random=ports_.random;
    // ACT1Q4_SetMonolithOrder: assign each original class to a random empty
    // position, retaining rejection draws. It is per game, not map-seed UI data.
    for(int stone=17;stone<=21;++stone) {
        size_t slot;
        do {slot=limitedRandom(random,5);} while(state_.stones[slot]);
        state_.stones[slot]=stone;
    }
    ports_.random=random;state_.stonesOrdered=true;
}
bool System::npcVisible(const CharacterRecord &record,std::string_view code,RegionId area) const {
    if(!code.starts_with("cain")) return true;
    if(int(area)==38) return !state_.cainRescued;
    if(int(area)!=1) return true;
    return state_.cainRescued || record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::Rescued);
}
DomainResult<> System::operate(const ActorContext &actor,EntityId source,int definition,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
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
        if(state_.activatedStones>=5) return {DomainStatus::Applied,std::monostate{}};
        if(state_.stones[state_.activatedStones]!=definition) return {DomainStatus::Conflict,{}};
        std::optional<travel::SpecialPortalPlan> portal;
        if(state_.activatedStones==4) {
            const auto *area=ports_.areas.find(actor.area);
            const auto alpha=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[](const auto &o){return o.type==17;});
            if(alpha==area->definition.objects.end()) return {DomainStatus::Unavailable,{}};
            const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::Tristram,0,alpha->position+Vec{4,4});
            if(!result) return {result.status,{}};
            portal=*result.value;
        }
        cain.stage=std::max(cain.stage,uint32_t(CainStage::ScrollTranslated));
        cain.flags=(cain.flags&~cainStoneCountMask)|(state_.activatedStones+1);
        if(state_.activatedStones==4) cain.stage=std::max(cain.stage,uint32_t(CainStage::PortalOpened));
        const auto result=commit(actor,std::move(record));
        if(result) {++state_.activatedStones;if(portal) {ports_.travel.commitSpecialPortal(std::move(*portal));state_.cainPortalOpened=true;}}
        return result;
    }
    if(operation==10 && int(actor.area)==38) {
        if(state_.cainRescued) return {DomainStatus::Applied,std::monostate{}};
        const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::CainRescue,0,position+Vec{3,3});
        if(!portal) return {portal.status,{}};
        // Capture only present eligible characters. The personal pending reward
        // persists; late entrants never acquire another player's ring.
        std::set<PlayerId> eligible;
        for(const auto &[id,player]:ports_.players.all()) if(player.entered && int(player.area)==38 &&
            player.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SearchForCain)).stage<uint32_t(CainStage::Rescued)) eligible.insert(id);
        state_.goals.insert_or_assign(QuestId::SearchForCain,std::move(eligible));state_.cainRescued=true;
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
        state_.goals.insert_or_assign(*goal,std::move(eligible));state_.objectives[questIndex(*goal)]=true;
        // Game.cpp calls QuestUpdater every 20 frames; timeout 1 uses a
        // strict '<', hence each invocation consumes two updater ticks.
        if(*goal==QuestId::SistersToTheSlaughter) state_.andarielPortalAt=(tick.tick/20+9*2)*20;
    }
    for(auto it=state_.goals.begin();it!=state_.goals.end();) {
        for(auto player=it->second.begin();player!=it->second.end();) {
            const auto *p=ports_.players.find(*player);
            if(!p || !p->entered) {player=it->second.erase(player);continue;}
            auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(it->first));
            const uint32_t stage=it->first==QuestId::SearchForCain?uint32_t(CainStage::Rescued):it->first==QuestId::ForgottenTower?uint32_t(TowerStage::CountessSlain):it->first==QuestId::SistersBurialGrounds?uint32_t(BurialStage::BloodRavenSlain):uint32_t(SlaughterStage::AndarielSlain);
            if(q.stage>=stage) {player=it->second.erase(player);continue;}
            q.stage=stage;
            const auto &area=ports_.areas.at(p->area);
            const auto result=commit({p->player,p->actor,p->area,area.generation,0,tick.tick},std::move(record));
            if(result) player=it->second.erase(player);else {blocked=true;++player;}
        }
        if(it->second.empty()) it=state_.goals.erase(it);else ++it;
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
        const auto before=book;const int area=int(p.area);
        // A1Q4_PlayerStartedGame restores opened stones; InitFunction06 on
        // STONEALPHA schedules the real red portal when that area is loaded.
        if(state_.restoreCairnStones && !state_.cainPortalOpened && area==4 && p.persistent.player.hp>0) {
            const auto &source=ports_.areas.at(p.area);
            const auto alpha=std::find_if(source.definition.objects.begin(),source.definition.objects.end(),[](const auto &object){return object.type==17;});
            if(alpha!=source.definition.objects.end()) {
                const auto portal=ports_.travel.prepareSpecialPortal({id,p.actor,p.area,source.generation,0,tick.tick},travel::SpecialPortalKind::Tristram,0,alpha->position+Vec{4,4});
                if(portal) {ports_.travel.commitSpecialPortal(*portal.value);state_.cainPortalOpened=true;} else blocked=true;
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
        if(!state_.andarielPortal && state_.andarielPortalAt && tick.tick>=state_.andarielPortalAt && area==37 && p.persistent.player.hp>0) {
            const auto portal=ports_.travel.prepareSpecialPortal({id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick},travel::SpecialPortalKind::Andariel,0,p.position);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);state_.andarielPortal=true;} else blocked=true;
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
