#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::quests {
void System::registerCainObject(EntityId id,int definition,int initialization,RegionId area,Vec position) {
    if(initialization==13 && definition==61) state_.actOne.cainInvisibleObject=id;
    if(initialization==54) {state_.actOne.cainStartObject=id;state_.actOne.cainStart=ActOneState::CainStart{area,position};}
}
bool System::cainTownRequested() const {
    if(state_.actOne.cainTownActor) return false;
    if(state_.actOne.cainRescued) return state_.npcStory.cainDeparted;
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;
        if(player.entered && player.persistent.player.quests.at(size_t(ports_.settings.difficulty))
            .at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::Rescued)) return true;
    }
    return false;
}
void System::registerTownCain(EntityId id) {
    const auto *actor=ports_.monsters.find(id);
    if(!actor || !actor->questNpc || actor->identity.monster!="cain5")
        throw std::invalid_argument("Invalid town Cain registration");
    if(state_.actOne.cainTownActor && state_.actOne.cainTownActor!=id)
        throw std::invalid_argument("Town Cain already registered");
    state_.actOne.cainTownActor=id;
}
std::optional<int> System::initialObjectMode(RegionId region,EntityId id,int definition,int operation,int initialization,uint64_t tick) const {
    if(int(region)==40 && initialization==30) return state_.npcStory.palaceOpen?2:0;
    if(int(region)==75 && initialization==39) return state_.npcStory.ormusActivated?2:0;
    const auto completed=[&](QuestId quest) {return (state_.initialFlags[questDefinition(quest).nativeSlot]&0x8000)!=0;};
    switch(initialization) {
    case 4:if(completed(QuestId::ForgottenTower)) return 5;break;
    case 6:if(completed(QuestId::SearchForCain) || state_.actOne.restoreCairnStones) return 2;break;
    case 7:return state_.actOne.cainRescued?5:0;
    case 9:return completed(QuestId::SearchForCain) || state_.actOne.cainRescued?1:0;
    case 15:return completed(QuestId::ToolsOfTheTrade)?2:0;
    default:break;
    }
    return objectMode(region,id,definition,operation,tick);
}
DomainResult<> System::cairnFx(RegionId area,Vec position,int destination,uint64_t tick) {
    const auto portal=ports_.travel.prepareNativePermanentPortal(area,position,destination,tick);
    if(isDeferred(portal.status)) return {portal.status,{}};
    if(portal) {
        ports_.travel.commitSpecialPortal(*portal.value);
        if(destination==38) state_.actOne.cainPortalOpened=true;
    }
    ports_.world.retainControllerRoom(area,position,false);
    return {DomainStatus::Applied,std::monostate{}};
}
void System::orderStones() {
    if(state_.actOne.stonesOrdered) return;
    auto random=state_.controlRandom.value();
    // ACT1Q4_SetMonolithOrder: assign each original class to a random empty
    // position, retaining rejection draws. It is per game, not map-seed UI data.
    for(int stone=17;stone<=21;++stone) {
        size_t slot;
        do {slot=limitedRandom(random,5);} while(state_.actOne.stones[slot]);
        state_.actOne.stones[slot]=stone;
    }
    state_.controlRandom=random;state_.actOne.stonesOrdered=true;
}
bool System::npcVisible(const CharacterRecord &record,std::string_view code,RegionId area,int initFunction) const {
    if(code=="hratli" && int(area)==75 && (initFunction==49 || initFunction==50)) {
        if(initFunction==50) return state_.npcStory.hratliDeparted;
        for(const auto &[id,unit]:ports_.monsters.read().actors)
            if(unit.area==area && unit.identity.monster==code && !state_.npcStory.retired.contains(id))
                for(const auto &npc:ports_.areas.at(area).definition.npcs)
                    if(npc.id==id && npc.rule.questInitFunction==49) return true;
        return !state_.npcStory.hratliDeparted;
    }
    if(code=="nihlathak" && int(area)==109) return !state_.actFive.anyaThawed;
    if(code=="drehya" && int(area)==109) return state_.npcStory.anyaDeparted;
    if(code=="natalya" && int(area)==75) return !state_.actThree.mephistoSlain;
    if(code=="baalthrone" && int(area)==131) return !state_.actFive.throneDeparted;
    if(code=="jerhyn" && int(area)==40 && (initFunction==18 || initFunction==19)) {
        return initFunction==(state_.npcStory.palaceRequested?19:18);
    }
    if(code=="tyrael1" && int(area)==73) return state_.actTwo.durielSlain || record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::SevenTombs)).stage>=2;
    if(!code.starts_with("cain")) return true;
    if(int(area)==38) return !state_.npcStory.cainDeparted;
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
        if(unavailable(actor.player,record,QuestId::ForgottenTower)) return {DomainStatus::Conflict,{}};
        if(!towerAdvance(book.at(questIndex(QuestId::ForgottenTower)),TowerStage::TomeRead)) return {DomainStatus::Applied,std::monostate{}};
        return commit(actor,std::move(record));
    }
    auto &cain=book.at(questIndex(QuestId::SearchForCain));
    if(operation==12 && int(actor.area)==5) {
        if(unavailable(actor.player,record,QuestId::SearchForCain)) return {DomainStatus::Conflict,{}};
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
        if(p->persistent.player.level<8 || tools.stage>=uint32_t(ToolsStage::RewardReady) || detail::actTwoCarried(*p,"hdm",ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        for(const auto &[id,item]:ports_.items.read().world.items) { (void)id;if(item.definition=="hdm") return {DomainStatus::Conflict,{}}; }
        return prepareReward(actor,source,RewardKind::Malus,false);
    }
    if(operation==9 && int(actor.area)==4 && definition>=17 && definition<=21) {
        if(unavailable(actor.player,record,QuestId::SearchForCain)) return {DomainStatus::Conflict,{}};
        orderStones();
        if(!detail::carried(*p,"bkd",ports_.settings.difficulty) && cain.stage<uint32_t(CainStage::ScrollTranslated)) return {DomainStatus::InvalidRequest,{}};
        if(state_.actOne.activatedStones>=5) return {DomainStatus::Applied,std::monostate{}};
        if(state_.actOne.stones[state_.actOne.activatedStones]!=definition) return {DomainStatus::Conflict,{}};
        std::optional<ActOneState::CairnControl> control;
        if(state_.actOne.activatedStones==4) {
            const auto *area=ports_.areas.find(actor.area);
            const auto &activeObjects=ports_.objects.read().objects;
            const objects::Object *lambda=nullptr;
            for(size_t index=0;index<state_.actOne.stones.size();++index) if(state_.actOne.stones[index]==21) {
                const auto found=activeObjects.find(index==state_.actOne.activatedStones?source:state_.actOne.monoliths[index]);
                if(found!=activeObjects.end() && found->second.definition==21) lambda=&found->second;
            }
            if(!lambda) if(const auto *room=area->definition.activation.room(position)) {
                const auto index=size_t(room-area->definition.activation.rooms().data());
                for(const auto &near:area->definition.roomNear.at(index)) {
                    for(const auto id:ports_.areas.roomUnitOrder(near)) {
                        const auto found=activeObjects.find(id);
                        if(found!=activeObjects.end() && found->second.definition==21) {lambda=&found->second;break;}
                    }
                    if(lambda) break;
                }
            }
            if(lambda && int(lambda->position.x+ports_.areas.at(lambda->area).definition.origin.x))
                control=ActOneState::CairnControl{actor.actor,lambda->area,lambda->position+Vec{6,-3}};
        }
        cain.stage=std::max(cain.stage,uint32_t(CainStage::ScrollTranslated));
        cain.flags=(cain.flags&~cainStoneCountMask)|(state_.actOne.activatedStones+1);
        if(state_.actOne.activatedStones==4) cain.stage=std::max(cain.stage,uint32_t(CainStage::PortalOpened));
        const auto result=commit(actor,std::move(record));
        if(result) {
            state_.actOne.monoliths.at(state_.actOne.activatedStones)=source;
            ++state_.actOne.activatedStones;if(control) state_.actOne.cairnPending=*control;
        }
        return result;
    }
    if(operation==10 && int(actor.area)==38) {
        if(state_.actOne.cainRescued) return {DomainStatus::Applied,std::monostate{}};
        if(unavailable(actor.player,record,QuestId::SearchForCain)) return {DomainStatus::Conflict,{}};
        state_.actOne.cainRescued=true;
        // Operate10 schedules MONUMOD +17; that callback births Cain at
        // the cage's +3,+3 coordinate. Fn031 owns the subsequent departure.
        state_.npcStory.cainBirth=position+ports_.areas.at(actor.area).definition.origin+Vec{3,3};
        state_.npcStory.cainCage=source;
        state_.npcStory.cainBirthAt=actor.tick+17;
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::NotImplemented,{}};
}
StepStatus System::actOneStep(TickContext tick) {
    bool blocked=false;
    if(state_.actOne.cairnPending) {
        const auto control=*state_.actOne.cairnPending;
        const auto definition=ports_.missiles.nativeDefinition("cairnstones");
        if(!definition) throw std::logic_error("Missing original Cairn Stones missile");
        const auto *area=ports_.areas.find(control.area);
        const auto result=area?ports_.missiles.spawnNative({control.owner,control.area,area->generation,tick.tick,
            control.position,control.position,*definition,0,1}):DomainResult<EntityId>{DomainStatus::Unavailable,{}};
        if(isDeferred(result.status)) blocked=true;
        else {
            if(result) ports_.world.retainControllerRoom(control.area,control.position,true);
            state_.actOne.cairnPending.reset();
        }
    }
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        (void)id;if(m.alive() || m.owner || m.identity.origin==SpawnOrigin::Debug) continue;
        std::optional<QuestId> goal;
        if(int(m.area)==17 && m.identity.monster=="bloodraven") goal=QuestId::SistersBurialGrounds;
        if(int(m.area)==25 && m.identity.superUnique=="The Countess") goal=QuestId::ForgottenTower;
        if(int(m.area)==37 && m.identity.monster=="andariel") goal=QuestId::SistersToTheSlaughter;
        if(!goal || state_.objectives[questIndex(*goal)]) continue;
        const uint32_t stage=*goal==QuestId::ForgottenTower?uint32_t(TowerStage::CountessSlain):*goal==QuestId::SistersBurialGrounds?uint32_t(BurialStage::BloodRavenSlain):uint32_t(SlaughterStage::AndarielSlain);
        captureGoal(*goal,stage,m.area,*goal==QuestId::SistersBurialGrounds?std::optional<Vec>{m.position}:std::nullopt,
            *goal==QuestId::SistersBurialGrounds?std::nullopt:killerPlayer(m.killer));
        if(*goal==QuestId::ForgottenTower && !(state_.initialFlags[questDefinition(*goal).nativeSlot]&0x8000))
            ports_.objects.countessKilled(m.position,tick.tick);
        // Game.cpp calls QuestUpdater every 20 frames; timeout 1 uses a
        // strict '<', hence each invocation consumes two updater ticks.
        if(*goal==QuestId::SistersToTheSlaughter) state_.actOne.andarielPortalAt=(tick.tick/20+9*2)*20;
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
        const auto available=[&](QuestId quest){return !unavailable(id,record,quest);};
        const auto before=book;const int area=int(p.area);
        const auto burial=book[questIndex(QuestId::SistersBurialGrounds)].stage;
        if(burial>0 && burial<uint32_t(BurialStage::BloodRavenSlain)) state_.actOne.burialOffer=true;
        const auto tools=book[questIndex(QuestId::ToolsOfTheTrade)].stage;
        if(tools>0 && tools<uint32_t(ToolsStage::RewardReady)) state_.actOne.toolsOffer=true;
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
        if(area==17 && available(QuestId::SistersBurialGrounds)) burialAdvanceOnEntry(book[questIndex(QuestId::SistersBurialGrounds)]);
        if(area>=20 && area<=25 && available(QuestId::ForgottenTower)) towerAdvance(book[questIndex(QuestId::ForgottenTower)],area==20?TowerStage::TowerEntered:TowerStage::CellarEntered);
        if(area==28 && available(QuestId::ToolsOfTheTrade)) toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],ToolsStage::BarracksEntered);
        if(area==37 && available(QuestId::SistersToTheSlaughter)) slaughterAdvance(book[questIndex(QuestId::SistersToTheSlaughter)],SlaughterStage::CatacombsEntered);
        auto &cain=book[questIndex(QuestId::SearchForCain)];
        if(record.completedActs.at(size_t(ports_.settings.difficulty)).at(0) && cain.stage<uint32_t(CainStage::Rescued)) {
            cain.stage=uint32_t(CainStage::Rewarded);cain.flags=cainRescuedByRogues;
        }
        if(!state_.actOne.andarielPortal && state_.actOne.andarielPortalAt && tick.tick>=state_.actOne.andarielPortalAt && area==37 && p.persistent.player.hp>0) {
            const auto portal=ports_.travel.prepareSpecialPortal({id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick},travel::SpecialPortalKind::Andariel,0,p.position);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);state_.actOne.andarielPortal=true;} else blocked=true;
        }
        if(available(QuestId::SearchForCain)) {
            if(area==38) cainAdvance(cain,CainStage::TristramEntered);
            if(detail::carried(p,"bks",ports_.settings.difficulty)) cainAdvance(cain,CainStage::BarkAcquired);
            if(detail::carried(p,"bkd",ports_.settings.difficulty)) cainAdvance(cain,CainStage::ScrollTranslated);
        }
        if(detail::actTwoCarried(p,"hdm",ports_.settings.difficulty)) toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],ToolsStage::MalusAcquired);
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;})) {
            if(!commit({id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick},std::move(record))) blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
