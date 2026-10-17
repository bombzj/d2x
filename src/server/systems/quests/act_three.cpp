#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
namespace d2x::server::quests {
void System::nativeEliteBorn(EntityId id,bool eligible) {
    auto &world=state_.actThree;
    if(!eligible || !world.figurineEnabled || world.figurineSource || world.figurineDropped) return;
    const auto *unit=ports_.monsters.find(id);if(!unit || unit->identity.spawnKey=="quest.gidbinn") return;
    const auto *area=ports_.areas.find(unit->area);if(!area || area->definition.act!=2) return;
    world.figurineSource=id;world.figurineArea=unit->area;world.figurinePosition=unit->position;
}
std::optional<Dialogue> System::speech(const NpcRule &npc,QuestId quest,std::string_view state,uint8_t menu) const {
    const auto found=npc.questSpeeches.find({quest,std::string(state)});
    if(found==npc.questSpeeches.end()) return {};
    return Dialogue{quest,{menu,found->second}};
}
std::optional<Dialogue> System::actThreeDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto available=[&](QuestId id){return !unavailable(actor.player,p->persistent.player,id);};
    const auto &q=p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::GoldenBird));
    const auto &items=p->rules.character->laterQuests;
    if(q.stage==5 && npc.code=="alkor") return speech(npc,QuestId::GoldenBird,"Successful");
    if(q.stage<5 && detail::actTwoCarried(*p,items.bird,ports_.settings.difficulty)) {
        if(npc.code=="alkor") return speech(npc,QuestId::GoldenBird,"AfterInit");
        if(npc.code.starts_with("cain") && q.stage<4) return speech(npc,QuestId::GoldenBird,"Init3");
    }
    if(q.stage<5 && detail::actTwoCarried(*p,items.figurine,ports_.settings.difficulty)) {
        if(npc.code=="meshif2") return speech(npc,QuestId::GoldenBird,"Init2");
        if(npc.code.starts_with("cain") && q.stage<2) return speech(npc,QuestId::GoldenBird,"Init1");
    }
    const auto &blade=p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::BladeOfTheOldReligion));
    if(available(QuestId::BladeOfTheOldReligion) && npc.code=="hratli" && !blade.stage) return speech(npc,QuestId::BladeOfTheOldReligion,"Init");
    if(npc.code=="ormus" && blade.stage<4 && detail::actTwoCarried(*p,items.gidbinn,ports_.settings.difficulty)) return speech(npc,QuestId::BladeOfTheOldReligion,"Successful");
    if(blade.stage==4) {
        if(npc.code=="ormus" && !(blade.flags&gidbinnRingGranted)) return speech(npc,QuestId::BladeOfTheOldReligion,"Reward");
        if(npc.code=="asheara" && !(blade.flags&gidbinnHirelingGranted)) return speech(npc,QuestId::BladeOfTheOldReligion,"Successful");
    }
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto &khalim=book[questIndex(QuestId::KhalimsWill)];
    if(available(QuestId::KhalimsWill) && npc.code.starts_with("cain") && khalim.stage<4 && (q.stage>=5 || khalim.stage || int(actor.area)!=75)) {
        if(detail::actTwoCarried(*p,items.khalimWill,ports_.settings.difficulty) && !(khalim.flags&khalimWillExplained)) return speech(npc,QuestId::KhalimsWill,"Successful");
        constexpr std::array names{"EarlyReturnEye","EarlyReturnBrain","EarlyReturnHeart","EarlyReturnFlail"};
        for(int part=3;part>=0;--part) if(detail::actTwoCarried(*p,items.khalimParts[size_t(part)],ports_.settings.difficulty) && !(khalim.flags&(1u<<part))) return speech(npc,QuestId::KhalimsWill,names[size_t(part)]);
        if(!(khalim.flags&khalimAssigned)) return speech(npc,QuestId::KhalimsWill,"Init");
    }
    const auto &tome=book[questIndex(QuestId::LamEsensTome)];
    if(available(QuestId::LamEsensTome) && npc.code=="alkor" && tome.stage<4) {
        if(detail::actTwoCarried(*p,items.tome,ports_.settings.difficulty)) return speech(npc,QuestId::LamEsensTome,"Successful");
        if(!tome.stage && (book[questIndex(QuestId::BladeOfTheOldReligion)].stage>=4 || book[questIndex(QuestId::BlackenedTemple)].stage)) return speech(npc,QuestId::LamEsensTome,"Init");
    }
    const auto &temple=book[questIndex(QuestId::BlackenedTemple)];
    if(available(QuestId::BlackenedTemple) && !temple.stage && npc.code=="ormus" && (tome.stage>=4 || khalim.stage)) return speech(npc,QuestId::BlackenedTemple,"Init");
    if(temple.stage==3 && npc.code.starts_with("cain")) return speech(npc,QuestId::BlackenedTemple,"Successful");
    const auto &guardian=book[questIndex(QuestId::Guardian)];
    if(guardian.flags&guardianSpeechPending) return speech(npc,QuestId::Guardian,"Successful");
    if(available(QuestId::Guardian) && !guardian.stage && temple.stage>=3 && npc.code=="ormus") return speech(npc,QuestId::Guardian,"Init");
    return {};
}
DomainResult<> System::acknowledgeActThree(const ActorContext &actor,const Request &request,const NpcRule &npc) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(request.quest));
    if(request.quest==QuestId::GoldenBird) {
        if(npc.code=="meshif2") return prepareReward(actor,*request.npc,RewardKind::GoldenBird,true);
        if(npc.code=="alkor") return prepareReward(actor,*request.npc,q.stage==5?RewardKind::LifePotion:RewardKind::DeliverBird,true);
        if(npc.code.starts_with("cain")) q.stage=q.stage<3?2:4;
    }
    if(request.quest==QuestId::BladeOfTheOldReligion) {
        if(npc.code=="hratli" && !q.stage) q.stage=1;
        if(npc.code=="ormus") return prepareReward(actor,*request.npc,q.stage==4?RewardKind::GidbinnRing:RewardKind::ReturnGidbinn,true);
        if(npc.code=="asheara") return prepareReward(actor,*request.npc,RewardKind::IronWolf,true);
    }
    if(request.quest==QuestId::KhalimsWill) {
        const auto &items=p->rules.character->laterQuests;
        q.stage=std::max(q.stage,1u);
        constexpr std::array names{"EarlyReturnEye","EarlyReturnBrain","EarlyReturnHeart","EarlyReturnFlail"};
        for(size_t part=0;part<items.khalimParts.size();++part)
            if(const auto offered=speech(npc,request.quest,names[part]);offered && request.message==offered->message.text) q.flags|=1u<<part;
        if(const auto offered=speech(npc,request.quest,"Successful");offered && request.message==offered->message.text) q.flags|=khalimWillExplained;
        if(const auto offered=speech(npc,request.quest,"Init");offered && request.message==offered->message.text) q.flags|=khalimAssigned;
    }
    if(request.quest==QuestId::LamEsensTome) {
        if(detail::actTwoCarried(*p,p->rules.character->laterQuests.tome,ports_.settings.difficulty)) return prepareReward(actor,*request.npc,RewardKind::ReturnTome,true);
        if(!q.stage) q.stage=1;
    }
    if(request.quest==QuestId::BlackenedTemple) q.stage=q.stage==3?4:std::max(q.stage,1u);
    if(request.quest==QuestId::Guardian) {q.flags&=~guardianSpeechPending;if(!q.stage) q.stage=1;}
    std::set<PlayerId> shared;
    const auto stage=q.stage;
    if(request.quest==QuestId::GoldenBird && npc.code.starts_with("cain")) shared=eligibleDelivery(QuestId::GoldenBird,stage,actor.player);
    const auto result=commit(actor,std::move(record));
    if(result && !shared.empty()) shareProgress(QuestId::GoldenBird,stage,std::move(shared));
    return result;
}
StepStatus System::actThreeStep(TickContext tick) {
    bool blocked=orbStep(tick)==StepStatus::Blocked;auto &world=state_.actThree;
    constexpr std::array council{"Ismail Vilehand","Geleb Flamefinger","Toorc Icefist"};
    std::set<std::string> deadCouncil;
    const monsters::Actor *lastCouncil=nullptr;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        (void)id;if(m.life>0 || m.owner || m.identity.origin==SpawnOrigin::Debug || m.identity.origin==SpawnOrigin::Summoned) continue;
        if(int(m.area)==102 && m.identity.monster=="mephisto") {
            world.mephistoSlain=true;captureGoal(QuestId::Guardian,4,m.area,{},killerPlayer(m.killer));
        }
        if(int(m.area)==83 && m.identity.rank==MonsterRank::SuperUnique && std::find(council.begin(),council.end(),m.identity.superUnique)!=council.end()) {
            deadCouncil.insert(m.identity.superUnique);
            if(!lastCouncil || m.deathTick>lastCouncil->deathTick ||
               (m.deathTick==lastCouncil->deathTick && m.deathOccurrence>lastCouncil->deathOccurrence)) lastCouncil=&m;
        }
    }
    if(deadCouncil.size()==council.size() && lastCouncil) captureGoal(QuestId::BlackenedTemple,world.orbSmashed?4u:3u,lastCouncil->area,lastCouncil->position);
    const auto *source=ports_.monsters.find(world.figurineSource);
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=2) continue;
        const ActorContext actor{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,tick.tick};
        if(world.gidbinnAt && tick.tick>=world.gidbinnAt && !world.gidbinnSpawned) {
            const auto &objects=ports_.areas.at(p.area).definition.objects;
            if(std::any_of(objects.begin(),objects.end(),[&](const auto &o){return o.id==world.gidbinnAltar;})) {
                if(spawnQuestGroup(actor,"gidbinn")) world.gidbinnSpawned=true;else blocked=true;
            }
        }
        if(world.gidbinnSpawned && !world.gidbinnDropped && !state_.pending.contains(id))
            for(const auto &[monsterId,m]:ports_.monsters.read().actors)
                if(m.area==p.area && m.identity.spawnKey=="quest.gidbinn" && m.life<=0)
                    if(!prepareReward(actor,monsterId,RewardKind::Gidbinn,false,m.position)) blocked=true;
        auto record=p.persistent.player;auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::GoldenBird));
        const auto before=record.quests.at(size_t(ports_.settings.difficulty));const auto &items=p.rules.character->laterQuests;
        if(q.stage<3 && detail::actTwoCarried(p,items.figurine,ports_.settings.difficulty)) q.stage=std::max(q.stage,1u);
        if(q.stage<5 && detail::actTwoCarried(p,items.bird,ports_.settings.difficulty)) q.stage=std::max(q.stage,3u);
        auto &book=record.quests.at(size_t(ports_.settings.difficulty));
        const auto available=[&](QuestId quest){return !unavailable(id,record,quest);};
        auto &blade=book[questIndex(QuestId::BladeOfTheOldReligion)];
        if(blade.stage<4 && detail::actTwoCarried(p,items.gidbinn,ports_.settings.difficulty)) blade.stage=3;
        auto &khalim=book[questIndex(QuestId::KhalimsWill)];
        if(available(QuestId::KhalimsWill) && khalim.stage<4) {
            if(detail::actTwoCarried(p,items.khalimWill,ports_.settings.difficulty)) khalim.stage=3;
            else for(const auto &part:items.khalimParts) if(detail::actTwoCarried(p,part,ports_.settings.difficulty)) khalim.stage=std::max(khalim.stage,2u);
        }
        auto &tome=book[questIndex(QuestId::LamEsensTome)];
        if(tome.stage<4 && detail::actTwoCarried(p,items.tome,ports_.settings.difficulty)) tome.stage=3;
        if(available(QuestId::LamEsensTome) && int(p.area)!=75 && tome.stage==1) tome.stage=2;
        auto &temple=book[questIndex(QuestId::BlackenedTemple)];
        if(int(p.area)==83) {
            if(available(QuestId::BlackenedTemple)) temple.stage=std::max(temple.stage,2u);
            for(const auto &[monsterId,m]:ports_.monsters.read().actors) {
                (void)monsterId;
                if(m.area!=p.area || m.owner || m.identity.origin==SpawnOrigin::Debug || !m.identity.monster.starts_with("council")) continue;
                if(m.life<=0 && !state_.pending.contains(id)) {
                    if(!world.flailDropped && khalim.stage<4 && !detail::actTwoCarried(p,items.khalimParts[3],ports_.settings.difficulty) && !detail::actTwoCarried(p,items.khalimWill,ports_.settings.difficulty)) prepareReward(actor,monsterId,RewardKind::KhalimFlail,false,m.position);
                    else if(!world.cubeDropped && !detail::actTwoCarried(p,p.rules.character->actTwo.cube,ports_.settings.difficulty)) prepareReward(actor,monsterId,RewardKind::CouncilCube,false,m.position);
                }
            }
        }
        auto &guardian=book[questIndex(QuestId::Guardian)];
        if(available(QuestId::Guardian) && int(p.area)>=94 && int(p.area)<=102 && guardian.stage<4) guardian.stage=std::max(guardian.stage,int(p.area)==102?3u:int(p.area)>=100?2u:1u);
        for(const auto &[monsterId,m]:ports_.monsters.read().actors) if(int(m.area)==102 && m.identity.monster=="mephisto" && m.identity.origin!=SpawnOrigin::Debug && m.life<=0) {
            if(p.area==m.area && !world.soulstoneDropped && !state_.pending.contains(id)) prepareReward(actor,monsterId,RewardKind::Soulstone,false,m.position);
        }
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;}) && !commit(actor,record)) blocked=true;
        if(source && source->life<=0 && !world.figurineDropped && p.area==source->area && q.stage<5 && p.persistent.player.hp>0) {
            bool preparing=false;for(const auto &[player,pending]:state_.pending) {(void)player;if(pending.kind==RewardKind::Figurine) preparing=true;}
            if(!preparing && !prepareReward(actor,source->id,RewardKind::Figurine,false,source->position)) blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
DomainResult<> System::spawnQuestGroup(const ActorContext &actor,std::string_view key) {
    const auto &area=ports_.areas.at(actor.area);
    if(area.definition.nativeQuestMonsters.contains(key)) return ports_.population.requestQuestBirth(actor.area,key,{actor.tick});
    state_.deferred="Original quest birth is not prepared: "+std::string(key);
    return {DomainStatus::NotImplemented,{}};
}
DomainResult<> System::operateActThree(const ActorContext &actor,EntityId source,int,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    if(operation==31) {
        if((state_.initialFlags[questDefinition(QuestId::BladeOfTheOldReligion).nativeSlot]&0x8000) || unavailable(actor.player,p->persistent.player,QuestId::BladeOfTheOldReligion)) return {DomainStatus::Conflict,{}};
        const auto &q=p->persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::BladeOfTheOldReligion));
        if(q.stage>=4 || state_.actThree.gidbinnAt) return {DomainStatus::Conflict,{}};
        state_.actThree.gidbinnAltar=source;
        state_.actThree.gidbinnAt=(actor.tick/20+7*2)*20;
        state_.objectModes[source]=1;
        return {DomainStatus::Applied,std::monostate{}};
    }
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));const auto &items=p->rules.character->laterQuests;
    if(operation>=57 && operation<=59) {
        const size_t part=operation==57?2:operation==58?0:1;
        if(book[questIndex(QuestId::KhalimsWill)].stage>=4 || detail::actTwoCarried(*p,items.khalimParts[part],ports_.settings.difficulty) || detail::actTwoCarried(*p,items.khalimWill,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        constexpr std::array rewards{RewardKind::KhalimEye,RewardKind::KhalimBrain,RewardKind::KhalimHeart};
        return prepareReward(actor,source,rewards[part],false);
    }
    if(operation==28) {
        if(book[questIndex(QuestId::LamEsensTome)].stage>=4 || detail::actTwoCarried(*p,items.tome,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        return prepareReward(actor,source,RewardKind::LamTome,false);
    }
    if(operation==53 && int(actor.area)==83) {
        if(state_.actThree.orbSmashed || !detail::equippedQuestWeapon(*p,items.khalimWill,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        if(!state_.actThree.orbHits) {state_.actThree.orbHits=1;return {DomainStatus::Applied,std::monostate{}};}
        return prepareReward(actor,source,RewardKind::SmashOrb,false,position);
    }
    if(operation==45) {state_.actThree.sewerAt=actor.tick+30;state_.objectModes[source]=1;return {DomainStatus::Applied,std::monostate{}};}
    if(operation==44 || operation==46) return ports_.travel.useSpecial(actor,{travel::Kind::QuestObject,{source,0,2},{}});
    return {DomainStatus::NotImplemented,{}};
}
std::optional<int> System::objectMode(RegionId region,EntityId id,int definition,int operation,uint64_t tick) const {
    const auto *area=ports_.areas.find(region);if(!area) return {};
    if(id==state_.actOne.cainInvisibleObject && definition==61 && state_.actOne.activatedStones)
        return int(std::min(state_.actOne.activatedStones,5u))+1;
    if(area->definition.act==2) {
        if(operation==31 && id==state_.actThree.gidbinnAltar) return state_.actThree.gidbinnSpawned?2:1;
        if(operation==53) return state_.actThree.orbSmashed?2:0;
        if(operation==54 && int(region)==83) {
            if(state_.actThree.orbSequenceStarted) return {};
            return state_.actThree.orbSmashed?2:0;
        }
        if(operation==44) return state_.actThree.sewerAt && tick>=state_.actThree.sewerAt?2:0;
        if(operation==45 && state_.actThree.sewerAt) return tick>=state_.actThree.sewerAt?2:1;
        if(definition==341 || definition==342 || operation==46) return state_.actThree.mephistoSlain?2:0;
    }
    if(area->definition.act==4) {
        if(operation==67) return state_.actFive.anyaThawed?2:0;
        if(operation>=62 && operation<=64) return state_.actFive.ancientsDefeated?2:state_.actFive.ancientsActive?(tick<state_.actFive.ancientAt?3:4):0;
        if(operation==66) return state_.actFive.ancientsDefeated?2:0;
        if(operation==70) return int(region)==132 || state_.actFive.throneDeparted?2:0;
    }
    if(const auto mode=state_.objectModes.find(id);mode!=state_.objectModes.end()) return mode->second;
    return {};
}
}
