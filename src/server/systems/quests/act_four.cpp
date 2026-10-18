#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/population/system.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include <algorithm>
namespace d2x::server::quests {
DomainResult<> System::activateIzual(uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto quest=QuestId::FallenAngel;const auto &definition=questDefinition(quest);
    if(state_.actFour.izualActivated || (state_.initialFlags[definition.nativeSlot]&0x8000) || state_.objectives[questIndex(quest)]) return done();
    std::vector<EventBatch> batches;
    for(const auto &[player,body]:ports_.players.all()) {
        if(!body.entered) continue;
        const auto &record=body.persistent.player;
        const auto stage=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage;
        if((stage<definition.completedStage && !unavailable(player,record,quest)) || credited(player,quest))
            batches.push_back({0,tick,{}, {AudienceKind::Player,player,body.area},{QuestStatusFact{quest,2,0}}});
    }
    if(!batches.empty()) {const auto accepted=ports_.events.publishGroup(std::move(batches));if(!accepted) return {accepted.status,{}};}
    state_.actFour.izualActivated=true;return done();
}
std::optional<Dialogue> System::actFourDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto available=[&](QuestId id){return !unavailable(actor.player,p->persistent.player,id);};
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto &izual=book[questIndex(QuestId::FallenAngel)];
    if(available(QuestId::FallenAngel) && npc.code=="tyrael2" && (izual.stage==0 || izual.stage==3)) return speech(npc,QuestId::FallenAngel,izual.stage==3?"Successful":"Init");
    if(npc.code=="izualghost" && izual.stage==3 && !(izual.flags&izualGhostSpoken)) return speech(npc,QuestId::FallenAngel,"Successful");
    const auto &forge=book[questIndex(QuestId::HellsForge)];
    if(available(QuestId::HellsForge) && npc.code.starts_with("cain") && forge.stage<5 && book[questIndex(QuestId::Guardian)].stage>=4) {
        if(forge.stage==4) return speech(npc,QuestId::HellsForge,"Successful");
        if(!forge.stage || (forge.stage<3 && !detail::actTwoCarried(*p,p->rules.character->laterQuests.soulstone,ports_.settings.difficulty))) return speech(npc,QuestId::HellsForge,detail::actTwoCarried(*p,p->rules.character->laterQuests.soulstone,ports_.settings.difficulty)?"InitHasStone":"InitNoStone");
    }
    const auto &terror=book[questIndex(QuestId::TerrorsEnd)];
    if(available(QuestId::TerrorsEnd) && npc.code=="tyrael2" && ((!terror.stage && (izual.stage>=3 || forge.stage>=4)) || (terror.stage==4 && !(terror.flags&terrorPortalOpened)))) {
        const auto result=speech(npc,QuestId::TerrorsEnd,terror.stage==4?"ExpansionSuccess":"Init");
        return result?result:speech(npc,QuestId::TerrorsEnd,"Successful");
    }
    if(npc.code.starts_with("cain") && terror.stage==4 && terror.flags&terrorCainPending) return speech(npc,QuestId::TerrorsEnd,"Successful");
    return {};
}
DomainResult<> System::acknowledgeActFour(const ActorContext &actor,const Request &request,const NpcRule &npc) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(request.quest));
    std::optional<travel::SpecialPortalPlan> portal;
    if(request.quest==QuestId::FallenAngel) {
        if(npc.code=="tyrael2" && !q.stage) q.stage=1;
        else if(npc.code=="tyrael2" && q.stage==3) {
            if(record.unspentSkills>INT32_MAX-izualSkillReward) return {DomainStatus::Capacity,{}};
            record.unspentSkills+=izualSkillReward;q={4,0};
        } else if(npc.code=="izualghost" && q.stage==3) q.flags|=izualGhostSpoken;
    }
    if(request.quest==QuestId::HellsForge) {
        if(q.stage==4) q.stage=5;
        else if(q.stage<3) {
            if(!detail::actTwoCarried(*p,p->rules.character->laterQuests.soulstone,ports_.settings.difficulty)) return prepareReward(actor,*request.npc,RewardKind::Soulstone,true);
            q.stage=std::max(q.stage,1u);
        }
    }
    if(request.quest==QuestId::TerrorsEnd) {
        if(!q.stage && npc.code=="tyrael2") q.stage=1;
        else if(q.stage==4 && npc.code=="tyrael2") {
            const auto prepared=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::ActFive,0,p->position+Vec{5,0});
            if(!prepared) return {prepared.status,{}};
            portal=*prepared.value;
            q.flags=(q.flags&~terrorTyraelPending)|terrorPortalOpened;
        } else if(q.stage==4 && npc.code.starts_with("cain")) q.flags&=~terrorCainPending;
    }
    const auto result=commit(actor,std::move(record));
    if(result && portal) {ports_.travel.commitSpecialPortal(std::move(*portal));state_.actFour.portalOpened=true;}
    return result;
}
DomainResult<> System::operateActFour(const ActorContext &actor,EntityId source,int definition,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto &world=state_.actFour;
    if(operation==49 && int(actor.area)==107) {
        const auto &q=p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::HellsForge));
        if(q.stage>=4 || world.forgeSmashed) return {DomainStatus::Conflict,{}};
        const auto &items=p->rules.character->laterQuests;
        if(!world.forgePlaced) {
            if(!detail::actTwoCarried(*p,items.soulstone,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
            return prepareReward(actor,source,RewardKind::PlaceSoulstone,false,position);
        }
        if(!detail::equippedQuestWeapon(*p,items.hammer,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        if(world.forgeHits<2) {++world.forgeHits;return {DomainStatus::Applied,std::monostate{}};}
        return prepareReward(actor,source,RewardKind::SmashSoulstone,false,position);
    }
    if(int(actor.area)==108 && definition>=392 && definition<=396) {
        if(world.seals.contains(definition)) return {DomainStatus::Conflict,{}};
        if(operation>=54 && operation<=56) {
            constexpr std::array names{"Infector of Souls","Lord De Seis","Grand Vizier of Chaos"};
            constexpr std::array offsets{Vec{-12,-52},Vec{-39,33},Vec{32,16}};
            const auto index=size_t(operation-54);
            const auto requested=position+offsets[index];
            const auto &area=ports_.areas.at(actor.area).definition;
            const auto global=requested+area.origin;
            const auto room=ports_.areas.roomAtPosition(actor.area,position,int(global.x),int(global.y));
            if(!room || room->area!=actor.area) return {DomainStatus::Unavailable,{}};
            const auto found=ports_.world.questFreePosition(actor.area,requested,requested,3,13+int(index),0x3f11);
            if(!found || found->area!=actor.area) return {DomainStatus::Unavailable,{}};
            const auto spawned=ports_.objects.admit({131,found->area,found->position,{},{},0,actor.tick});
            if(!spawned) return {spawned.status,{}};
            world.sealAfterglows.emplace(*spawned.value,names[index]);
            ports_.world.retainControllerRoom(found->area,found->position,true);
        }
        world.seals.insert(definition);
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::NotImplemented,{}};
}
DomainResult<> System::sealAfterglow(RegionId region,EntityId source,Vec position,uint64_t tick) {
    const auto found=state_.actFour.sealAfterglows.find(source);
    if(found==state_.actFour.sealAfterglows.end()) return {DomainStatus::Applied,std::monostate{}};
    return ports_.population.requestQuestBirth(region,found->second,{tick},position);
}
StepStatus System::actFourStep(TickContext tick) {
    bool blocked=false;auto &world=state_.actFour;
    for(const auto &[monsterId,m]:ports_.monsters.read().actors) {
        (void)monsterId;
        if(m.life>0 || m.owner || m.identity.origin==SpawnOrigin::Debug || m.identity.origin==SpawnOrigin::Summoned) continue;
        if(int(m.area)==105 && m.identity.monster=="izual") {
            captureGoal(QuestId::FallenAngel,3,m.area,m.position,killerPlayer(m.killer));world.izualSlain=true;world.ghostPosition=m.position;
        }
        if(int(m.area)==108 && m.identity.monster=="diablo") {captureGoal(QuestId::TerrorsEnd,4,m.area,m.position);world.diabloSlain=true;}
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=3) continue;
        const ActorContext actor{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick};
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));const auto before=book;
        if(int(p.area)!=103) for(const auto quest:{QuestId::FallenAngel,QuestId::HellsForge,QuestId::TerrorsEnd}) if(!unavailable(id,record,quest) && book[questIndex(quest)].stage==1) book[questIndex(quest)].stage=2;
        if(int(p.area)==108) {
            auto &q=book[questIndex(QuestId::TerrorsEnd)];if(!unavailable(id,record,QuestId::TerrorsEnd) && q.stage<3) q.stage=3;
            bool cleared=world.seals.size()==5;
            for(const auto *name:{"Infector of Souls","Lord De Seis","Grand Vizier of Chaos"}) {
                bool killed=false;
                for(const auto &[monsterId,m]:ports_.monsters.read().actors) {(void)monsterId;if(m.area==p.area && m.identity.superUnique==name && m.identity.origin!=SpawnOrigin::Debug && m.life<=0) killed=true;}
                cleared=cleared && killed;
            }
            if(cleared && !world.diabloAt) {
                world.sanctumCleared=true;
                // ACT4Q2_KillAllMonstersInCS: remaining original hostiles die
                // before Diablo's spawn. Keep normal death/event ownership.
                bool retired=true;
                for(const auto &[monsterId,m]:ports_.monsters.read().actors)
                    if(m.area==p.area && m.life>0 && !m.owner && m.identity.monster!="diablo" && m.identity.origin!=SpawnOrigin::Debug && m.identity.origin!=SpawnOrigin::Summoned)
                        if(!ports_.monsters.damage(monsterId,p.actor,m.life,tick.tick)) retired=false;
                if(retired) world.diabloAt=tick.tick+10;else blocked=true;
            }
            if(world.diabloAt && tick.tick>=world.diabloAt && !world.diabloSpawned) {
                if(spawnQuestGroup(actor,"diablo")) world.diabloSpawned=true;else blocked=true;
            }
        }
        if(int(p.area)==105 && world.izualSlain && !world.ghostSpawned) {
            if(ports_.world.admitQuestNpc(p.area,"izualghost",world.ghostPosition)) world.ghostSpawned=true;else blocked=true;
        }
        if(int(p.area)==107 && !world.hammerDropped && !state_.pending.contains(id)) {
            for(const auto &[monsterId,m]:ports_.monsters.read().actors)
                if(m.area==p.area && m.identity.superUnique=="The Feature Creep" && m.life<=0 && m.identity.origin!=SpawnOrigin::Debug)
                    if(!prepareReward(actor,monsterId,RewardKind::ForgeHammer,false,m.position)) blocked=true;
        }
        if(int(p.area)==103 && !world.portalOpened && (book[questIndex(QuestId::TerrorsEnd)].flags&terrorPortalOpened)) {
            const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::ActFive,0);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);world.portalOpened=true;}else blocked=true;
        }
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;}) && !commit(actor,std::move(record))) blocked=true;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
