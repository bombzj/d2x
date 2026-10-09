#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
namespace d2x::server::quests {
std::optional<Dialogue> System::actFiveDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto &items=p->rules.character->laterQuests;
    const auto &siege=book[questIndex(QuestId::SiegeOnHarrogath)];
    if(npc.code=="larzuk" && (!siege.stage || siege.stage==3)) return speech(npc,QuestId::SiegeOnHarrogath,siege.stage==3?"Successful":"Init");
    const auto &rescue=book[questIndex(QuestId::RescueOnMountArreat)];
    if(npc.code=="qual-kehk" && (!rescue.stage || rescue.stage==3)) return speech(npc,QuestId::RescueOnMountArreat,rescue.stage==3?"Successful":"Init");
    const auto &ice=book[questIndex(QuestId::PrisonOfIce)];
    if(npc.code=="malah") {
        if(!ice.stage) return speech(npc,QuestId::PrisonOfIce,"Init");
        if(ice.stage>=3 && ice.stage<5 && !detail::actTwoCarried(*p,items.defrostPotion,ports_.settings.difficulty)) return speech(npc,QuestId::PrisonOfIce,"FoundAnya");
        if(ice.stage>=5 && !(ice.flags&iceScrollUsed) && !detail::actTwoCarried(*p,items.resistanceScroll,ports_.settings.difficulty)) return speech(npc,QuestId::PrisonOfIce,"Successful");
    }
    if(npc.code=="drehya" && ice.stage>=5 && !(ice.flags&iceRareGranted)) return speech(npc,QuestId::PrisonOfIce,"Successful");
    const auto &betrayal=book[questIndex(QuestId::BetrayalOfHarrogath)];
    if(npc.code=="drehya" && ice.stage>=5 && (!betrayal.stage || betrayal.stage==3)) return speech(npc,QuestId::BetrayalOfHarrogath,betrayal.stage==3?"Successful":"Init");
    const auto &ancients=book[questIndex(QuestId::RiteOfPassage)];
    if(!ancients.stage && siege.stage>=3 && npc.code=="qual-kehk") return speech(npc,QuestId::RiteOfPassage,"Init");
    if(ancients.stage==4 && (ancientNpcAcknowledgement(npc.code)&~ancients.flags)) return speech(npc,QuestId::RiteOfPassage,"Successful");
    const auto &baal=book[questIndex(QuestId::EveOfDestruction)];
    if(baal.stage==5 && (baalNpcAcknowledgement(npc.code)&~baal.flags)) return speech(npc,QuestId::EveOfDestruction,"Successful");
    return {};
}
DomainResult<> System::acknowledgeActFive(const ActorContext &actor,const Request &request,const NpcRule &npc) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(request.quest));
    std::optional<travel::SpecialPortalPlan> portal;
    switch(request.quest) {
    case QuestId::SiegeOnHarrogath: q.stage=q.stage==3?4:1;break;
    case QuestId::RescueOnMountArreat:
        if(q.stage==3) return prepareReward(actor,*request.npc,RewardKind::RescueRunes,true);
        q.stage=1;break;
    case QuestId::PrisonOfIce:
        if(npc.code=="drehya") return prepareReward(actor,*request.npc,RewardKind::AnyaRare,true);
        if(q.stage>=5) return prepareReward(actor,*request.npc,RewardKind::ResistanceScroll,true);
        if(q.stage>=3) return prepareReward(actor,*request.npc,RewardKind::DefrostPotion,true);
        q.stage=1;break;
    case QuestId::BetrayalOfHarrogath:
        if(q.stage==3) q.stage=4;
        else {
            if(!state_.actFive.anyaPortal) {
                const auto prepared=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::AnyaTemple,0,p->position+Vec{10,5});
                if(!prepared) return {prepared.status,{}};
                portal=*prepared.value;
            }
            q.stage=1;
        }
        break;
    case QuestId::RiteOfPassage:
        if(q.stage==4) q.flags|=ancientNpcAcknowledgement(npc.code);else q.stage=1;break;
    case QuestId::EveOfDestruction:
        if(q.stage!=5) return {DomainStatus::Stale,{}};
        if(npc.code=="tyrael3" && !state_.actFive.exitPortal) {
            const auto prepared=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::BaalExit,0,p->position+Vec{5,0});
            if(!prepared) return {prepared.status,{}};
            portal=*prepared.value;
        }
        q.flags|=baalNpcAcknowledgement(npc.code);break;
    default: return {DomainStatus::InvalidRequest,{}};
    }
    const auto result=commit(actor,std::move(record));
    if(result && portal) {
        ports_.travel.commitSpecialPortal(std::move(*portal));
        if(request.quest==QuestId::BetrayalOfHarrogath) state_.actFive.anyaPortal=true;
        if(request.quest==QuestId::EveOfDestruction) state_.actFive.exitPortal=true;
    }
    return result;
}
DomainResult<> System::operateActFive(const ActorContext &actor,EntityId source,int,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    if(operation==67 && int(actor.area)==114) {
        auto &q=book[questIndex(QuestId::PrisonOfIce)];
        if(q.stage>=5 || state_.actFive.anyaThawed) return {DomainStatus::Conflict,{}};
        if(detail::actTwoCarried(*p,p->rules.character->laterQuests.defrostPotion,ports_.settings.difficulty)) return prepareReward(actor,source,RewardKind::ThawAnya,false,position);
        const auto message=ports_.areas.at(actor.area).definition.questObjectMessages.find(operation);
        if(message==ports_.areas.at(actor.area).definition.questObjectMessages.end()) return {DomainStatus::Unavailable,{}};
        q.stage=std::max(q.stage,3u);
        transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(record)};
        edit.facts.emplace_back(fact(actor.player,edit.player,actor.area));edit.facts.emplace_back(NpcMessagesFact{source,{{0,message->second}},2});
        const auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        return ports_.transactions.commit(*plan.value);
    }
    if(operation==65 && int(actor.area)==120) {
        auto &world=state_.actFive;
        if(world.ancientsActive || world.ancientsDefeated) return {DomainStatus::Conflict,{}};
        const auto &groups=ports_.areas.at(actor.area).definition.questGroups;
        for(const auto name:ancientIdentities) if(!groups.contains(name)) return {DomainStatus::Unavailable,{}};
        book[questIndex(QuestId::RiteOfPassage)].stage=std::max(book[questIndex(QuestId::RiteOfPassage)].stage,3u);
        const auto result=commit(actor,std::move(record));
        if(result) {
            ports_.travel.closePortals(actor.area);
            world.ancientsActive=true;world.ancientAt=actor.tick+20;
            constexpr std::array minimumLevel{20,40,60};
            for(const auto &[id,player]:ports_.players.all()) if(player.entered && player.area==actor.area && player.persistent.player.hp>0 && player.persistent.player.level>=minimumLevel.at(size_t(ports_.settings.difficulty)) && player.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::RiteOfPassage)].stage<4) world.ancientEligible.insert(id);
            state_.objectModes[source]=2;
        }
        return result;
    }
    if(operation>=62 && operation<=64 && int(actor.area)==120) {
        if(book[questIndex(QuestId::RiteOfPassage)].stage<4) return {DomainStatus::Conflict,{}};
        book[questIndex(QuestId::RiteOfPassage)].flags|=32;
        book[questIndex(QuestId::EveOfDestruction)].stage=std::max(book[questIndex(QuestId::EveOfDestruction)].stage,2u);
        return commit(actor,std::move(record));
    }
    if(operation==66 || operation==70 || operation==72) return ports_.travel.useSpecial(actor,{travel::Kind::QuestObject,{source,0,2},{}});
    return {DomainStatus::NotImplemented,{}};
}
StepStatus System::actFiveStep(TickContext tick) {
    bool blocked=false;auto &world=state_.actFive;
    for(const auto &[monsterId,m]:ports_.monsters.read().actors) {
        (void)monsterId;if(m.life>0 || m.owner || m.identity.origin==SpawnOrigin::Debug || m.identity.origin==SpawnOrigin::Summoned) continue;
        if(int(m.area)==110 && m.identity.superUnique=="Siege Boss") captureGoal(QuestId::SiegeOnHarrogath,3,m.area);
        if(int(m.area)==124 && m.identity.superUnique=="Nihlathak Boss") captureGoal(QuestId::BetrayalOfHarrogath,3,m.area);
        if(int(m.area)==111 && m.identity.monster=="prisondoor") {
            const auto &area=ports_.areas.at(m.area);
            for(const auto &npc:area.definition.npcs) if(npc.rule.code=="act5pow" && !npc.hidden && (npc.position-m.position).length()<15)
                ports_.npc.escape(m.area,npc.id,m.position+Vec{2,0});
        }
    }
    world.rescued=0;for(const auto &[id,escape]:ports_.npc.read().escapes) {(void)id;if(int(escape.area)==111 && escape.escaped) ++world.rescued;}
    world.rescued=std::min(world.rescued,15u);
    if(world.rescued==15) captureGoal(QuestId::RescueOnMountArreat,3,RegionId(111));
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=4) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));const auto before=book;
        const int level=int(p.area);
        if(level>=110 && level<=112 && book[questIndex(QuestId::SiegeOnHarrogath)].stage==1) book[questIndex(QuestId::SiegeOnHarrogath)].stage=2;
        if(level==111) {
            auto &q=book[questIndex(QuestId::RescueOnMountArreat)];if(q.stage<3) {q.stage=2;q.flags=(q.flags&~rescueCountMask)|world.rescued;}
        }
        if(level!=109 && book[questIndex(QuestId::PrisonOfIce)].stage==1) book[questIndex(QuestId::PrisonOfIce)].stage=2;
        if(level>=121 && level<=124 && book[questIndex(QuestId::BetrayalOfHarrogath)].stage<2) book[questIndex(QuestId::BetrayalOfHarrogath)].stage=2;
        if(level!=109 && book[questIndex(QuestId::RiteOfPassage)].stage==1) book[questIndex(QuestId::RiteOfPassage)].stage=2;
        if(level==120 && book[questIndex(QuestId::RiteOfPassage)].stage<3) book[questIndex(QuestId::RiteOfPassage)].stage=3;
        const unsigned baal=level==132?4u:level==131?3u:level>=128 && level<=130?2u:0u;
        if(baal) book[questIndex(QuestId::EveOfDestruction)].stage=std::max(book[questIndex(QuestId::EveOfDestruction)].stage,baal);
        if(level==109 && (world.anyaThawed || book[questIndex(QuestId::PrisonOfIce)].stage>=5) && !world.anyaSpawned) {
            for(const auto &object:area.definition.objects) if(object.type==459) {
                if(ports_.world.admitQuestNpc(p.area,"drehya",object.position)) world.anyaSpawned=true;else blocked=true;
            }
        }
        if(level==109 && book[questIndex(QuestId::BetrayalOfHarrogath)].stage && !world.anyaPortal) {
            const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::AnyaTemple,0);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);world.anyaPortal=true;}else blocked=true;
        }
        if(level==124) {const auto result=spawnQuestGroup(actor,"Nihlathak Boss");if(!result) blocked=true;}
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;}) && !commit(actor,std::move(record))) blocked=true;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
