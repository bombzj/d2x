#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
namespace d2x::server::quests {
uint64_t System::prisonerPortalDue(EntityId id) const {
    for(const auto &portal:state_.actFive.rescuePortals) if(portal.object==id) return portal.due;
    return 0;
}
std::optional<System::PrisonerPortalView> System::prisonerPortal(EntityId id) const {
    for(const auto &portal:state_.actFive.rescuePortals) if(portal.prisoners.contains(id)) {
        const auto object=ports_.objects.read().objects.find(portal.object);
        if(object!=ports_.objects.read().objects.end())
            return PrisonerPortalView{object->first,object->second.area,object->second.position,resolvedPrisoners(portal)!=0};
    }
    return {};
}
unsigned System::resolvedPrisoners(const ActFiveState::RescuePortal &portal) const {
    unsigned result=0;
    for(const auto prisoner:portal.prisoners) {
        const auto escaped=ports_.npc.read().escapes.find(prisoner);
        if(escaped!=ports_.npc.read().escapes.end() && escaped->second.escaped) {++result;continue;}
        if(state_.actFive.rescueDeaths.contains(prisoner)) {++result;continue;}
        if(const auto *unit=ports_.monsters.find(prisoner);unit && !unit->alive()) {++result;continue;}
        for(const auto &[room,units]:ports_.monsters.read().inactive) {
            (void)room;
            if(std::any_of(units.begin(),units.end(),[&](const auto &unit){return unit.id==prisoner && unit.dead;})) {++result;break;}
        }
    }
    return result;
}
DomainResult<> System::finishPrisonerEscape(EntityId id) {
    return ports_.npc.finishEscape(id);
}
void System::prisonerActivated() {
    const auto quest=QuestId::RescueOnMountArreat;auto &world=state_.actFive;
    if((state_.initialFlags[questDefinition(quest).nativeSlot]&0x8000) || world.rescueLastStatus==5 || world.rescueKilled>=5) return;
    queueStatus(quest,5);world.rescueLastStatus=5;
}
DomainResult<> System::prisonerPortalEvent(EntityId id,uint64_t tick) {
    const auto found=ports_.objects.read().objects.find(id);
    if(found==ports_.objects.read().objects.end()) return {DomainStatus::Stale,{}};
    for(auto &portal:state_.actFive.rescuePortals) if(portal.object==id) {
        const auto resolved=resolvedPrisoners(portal);
        // A5Q2_UpdatePortalMode: the group counter reaches five even when
        // fewer prisoners were successfully allocated. Do not shorten it.
        auto updates=portal.closingUpdates;
        int mode=found->second.mode;
        if(mode==1) mode=2;
        else if(mode==2) {if(resolved>=5) ++updates;if(updates>5) mode=3;}
        else if(mode==3) mode=4;
        if(mode!=found->second.mode) {
            const auto changed=ports_.objects.setNativeMode(id,mode,tick);if(!changed) return changed;
        }
        portal.closingUpdates=updates;portal.due=tick+25;
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
DomainResult<> System::activateNihlathak(uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto quest=QuestId::BetrayalOfHarrogath;const auto &definition=questDefinition(quest);
    if(state_.actFive.nihlathakActivated || (state_.initialFlags[definition.nativeSlot]&0x8000) || state_.objectives[questIndex(quest)]) return done();
    std::vector<EventBatch> batches;
    for(const auto &[player,body]:ports_.players.all()) {
        if(!body.entered) continue;
        const auto &record=body.persistent.player;
        const auto stage=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage;
        if((stage<definition.completedStage && !unavailable(player,record,quest)) || credited(player,quest))
            batches.push_back({0,tick,{}, {AudienceKind::Player,player,body.area},{QuestStatusFact{quest,3,0}}});
    }
    if(!batches.empty()) {const auto accepted=ports_.events.publishGroup(std::move(batches));if(!accepted) return {accepted.status,{}};}
    state_.actFive.nihlathakActivated=true;return done();
}
DomainResult<> System::activateSiegeBoss(EntityId id,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto *unit=ports_.monsters.find(id);
    if(!unit || unit->identity.superUnique!="Siege Boss" || state_.actFive.siegeBossActivated) return done();
    const auto quest=QuestId::SiegeOnHarrogath;const auto &definition=questDefinition(quest);
    if((state_.initialFlags[definition.nativeSlot]&0x8000) || state_.objectives[questIndex(quest)]) return done();
    std::vector<EventBatch> batches;
    for(const auto &[player,body]:ports_.players.all()) {
        if(!body.entered) continue;
        const auto &record=body.persistent.player;
        const auto stage=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage;
        if((stage<definition.completedStage && !unavailable(player,record,quest)) || credited(player,quest))
            batches.push_back({0,tick,{}, {AudienceKind::Player,player,body.area},{QuestStatusFact{quest,2,0}}});
    }
    if(!batches.empty()) {const auto accepted=ports_.events.publishGroup(std::move(batches));if(!accepted) return {accepted.status,{}};}
    state_.actFive.siegeBossActivated=true;return done();
}
std::optional<Dialogue> System::actFiveDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto available=[&](QuestId id){return !unavailable(actor.player,p->persistent.player,id);};
    const auto &book=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    const auto &items=p->rules.character->laterQuests;
    const auto &siege=book[questIndex(QuestId::SiegeOnHarrogath)];
    if(npc.code=="larzuk" && (!siege.stage || siege.stage==3)) return speech(npc,QuestId::SiegeOnHarrogath,siege.stage==3?"Successful":"Init");
    const auto &rescue=book[questIndex(QuestId::RescueOnMountArreat)];
    if(available(QuestId::RescueOnMountArreat) && npc.code=="qual-kehk" && (!rescue.stage || rescue.stage==3)) return speech(npc,QuestId::RescueOnMountArreat,rescue.stage==3?"Successful":"Init");
    const auto &ice=book[questIndex(QuestId::PrisonOfIce)];
    if(available(QuestId::PrisonOfIce) && npc.code=="malah") {
        if(!ice.stage) return speech(npc,QuestId::PrisonOfIce,"Init");
        if(ice.stage>=3 && ice.stage<5 && !detail::actTwoCarried(*p,items.defrostPotion,ports_.settings.difficulty)) return speech(npc,QuestId::PrisonOfIce,"FoundAnya");
        if(ice.stage>=5 && !(ice.flags&iceScrollUsed) && !detail::actTwoCarried(*p,items.resistanceScroll,ports_.settings.difficulty)) return speech(npc,QuestId::PrisonOfIce,"Successful");
    }
    if(npc.code=="drehya" && ice.stage>=5 && !(ice.flags&iceRareGranted)) return speech(npc,QuestId::PrisonOfIce,"Successful");
    const auto &betrayal=book[questIndex(QuestId::BetrayalOfHarrogath)];
    if(npc.code=="drehya" && ice.stage>=5 && (!betrayal.stage || betrayal.stage==3)) return speech(npc,QuestId::BetrayalOfHarrogath,betrayal.stage==3?"Successful":"Init");
    const auto &ancients=book[questIndex(QuestId::RiteOfPassage)];
    if(available(QuestId::RiteOfPassage) && !ancients.stage && siege.stage>=3 && npc.code=="qual-kehk") return speech(npc,QuestId::RiteOfPassage,"Init");
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
    if(result && request.quest==QuestId::BetrayalOfHarrogath && !state_.actFive.anyaPortal) state_.npcStory.anyaPortalPending=true;
    if(result && portal) {
        ports_.travel.commitSpecialPortal(std::move(*portal));
        if(request.quest==QuestId::EveOfDestruction) state_.actFive.exitPortal=true;
    }
    return result;
}
DomainResult<> System::operateActFive(const ActorContext &actor,EntityId source,int,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    if(operation==67 && int(actor.area)==114) {
        if(unavailable(actor.player,record,QuestId::PrisonOfIce)) return {DomainStatus::Conflict,{}};
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
        const auto message=ports_.areas.at(actor.area).definition.questObjectMessages.find(operation);
        if(message==ports_.areas.at(actor.area).definition.questObjectMessages.end()) return {DomainStatus::Unavailable,{}};
        if(!unavailable(actor.player,record,QuestId::RiteOfPassage)) book[questIndex(QuestId::RiteOfPassage)].stage=std::max(book[questIndex(QuestId::RiteOfPassage)].stage,3u);
        auto readers=world.ancientAltarReaders;readers.insert_or_assign(actor.player,ActFiveState::AncientReader{actor.actor,source,actor.areaGeneration});
        transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(record)};
        edit.facts.emplace_back(fact(actor.player,edit.player,actor.area));edit.facts.emplace_back(NpcMessagesFact{source,{{0,message->second}},2});
        const auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(*plan.value);
        if(result) {
            ports_.travel.closePortals(actor.area);
            world.ancientAltar=source;world.ancientAltarReaders.swap(readers);
            world.ancientAltarEnd=actor.tick+std::max(uint64_t{1},ports_.objects.read().objects.at(source).rule.openingTicks-1);
            state_.objectModes[source]=1;
        }
        return result;
    }
    if(operation>=62 && operation<=64 && int(actor.area)==120) {
        if(book[questIndex(QuestId::RiteOfPassage)].stage<4) return {DomainStatus::Conflict,{}};
        book[questIndex(QuestId::RiteOfPassage)].flags|=32;
        if(!unavailable(actor.player,record,QuestId::EveOfDestruction)) book[questIndex(QuestId::EveOfDestruction)].stage=std::max(book[questIndex(QuestId::EveOfDestruction)].stage,2u);
        return commit(actor,std::move(record));
    }
    if(operation==66 || operation==70 || operation==72) return ports_.travel.useSpecial(actor,{travel::Kind::QuestObject,{source,0,2},{}});
    return {DomainStatus::NotImplemented,{}};
}
std::optional<PrisonerDeathPublication> System::preparePrisonerDeath(const monsters::Actor &candidate,
    const std::map<EntityId,monsters::Actor> &staged,std::span<const EntityId> order) const {
    if(candidate.alive() || candidate.owner || int(candidate.area)!=111 ||
        candidate.identity.origin==SpawnOrigin::Debug || candidate.identity.origin==SpawnOrigin::Summoned ||
        (candidate.identity.monster!="prisondoor" && !(candidate.questNpc && candidate.identity.monster=="act5pow")) ||
        (state_.initialFlags[questDefinition(QuestId::RescueOnMountArreat).nativeSlot]&0x8000)) return {};
    const auto *live=ports_.monsters.find(candidate.id);if(!live || !live->alive()) return {};
    PrisonerDeathPublication result;result.unit=candidate.id;result.door=candidate.identity.monster=="prisondoor";
    if(!result.door) {result.deaths.insert(candidate.id);return result;}
    if(state_.actFive.rescueDoors.contains(candidate.id)) return {};
    result.doors.insert(candidate.id);
    const auto &area=ports_.areas.at(candidate.area);const auto *room=area.definition.activation.room(candidate.position);
    if(!room) return result;
    const auto index=size_t(room-area.definition.activation.rooms().data());
    const auto rank=[&](EntityId id) {
        const auto it=std::find(order.begin(),order.end(),id);
        return std::pair{it==order.end(),it==order.end()?id.value:uint64_t(it-order.begin())};
    };
    for(const auto near:area.definition.roomNear.at(index)) for(const auto id:ports_.areas.roomUnitOrder(near)) {
        const auto *unit=ports_.monsters.find(id);if(!unit) continue;
        // Earlier publications are inputs to this door's callback; units later
        // in the native chain still have their live state at this point.
        if(const auto pending=staged.find(id);pending!=staged.end() && rank(id)<rank(candidate.id)) unit=&pending->second;
        if(!unit->alive() || unit->identity.monster!="act5pow" || unit->identity.origin==SpawnOrigin::Debug) continue;
        const auto point=unit->position+ports_.areas.at(unit->area).definition.origin-area.definition.origin;
        if(nativeUnitDistance(point,unit->rule.size,candidate.position,candidate.rule.size)<15) result.freed.insert(id);
    }
    return result;
}
void System::publishPrisonerDeath(PrisonerDeathPublication &plan) noexcept {
    auto &world=state_.actFive;
    if(plan.door) {
        if(world.rescueDoors.contains(plan.unit)) return;
        world.rescueDoors.merge(plan.doors);world.rescueFreed.merge(plan.freed);
    } else world.rescueDeaths.merge(plan.deaths);
    updatePrisonerCounters();
}
void System::updatePrisonerCounters() noexcept {
    auto &world=state_.actFive;
    world.rescued=std::min(unsigned(world.rescueFreed.size()),15u);
    // Death callbacks are cumulative even after the corpse leaves the room.
    // Freed GUIDs contribute to the group's portal counter, not nKilledWussies.
    world.rescueKilled=unsigned(std::count_if(world.rescueDeaths.begin(),world.rescueDeaths.end(),
        [&](EntityId id){return !world.rescueFreed.contains(id);}));
    const auto [spawned,groups]=ports_.objects.prisonerSpawnCounts();
    world.rescueGroupsComplete=groups==3;world.rescueSpawned=spawned;
    world.rescueRemaining=unsigned(std::max(0,int(spawned+(3-groups)*5)-int(world.rescued+world.rescueKilled)));
}
void System::resolvePrisoners(std::optional<PlayerId> rescuedBy) {
    auto &world=state_.actFive;
    if(rescuedBy) world.rescuePlayer=rescuedBy;
    const auto rescue=QuestId::RescueOnMountArreat;
    if(!(state_.initialFlags[questDefinition(rescue).nativeSlot]&0x8000) && !state_.objectives[questIndex(rescue)]) {
        const bool resolved=rescuedBy && world.rescueGroupsComplete && world.rescueSpawned==world.rescued+world.rescueKilled;
        if(world.rescueKilled>=5 || (resolved && world.rescued<=11)) {
            // A5Q2 SetCompletionFlag: this game's unresolved players receive
            // COMPLETEDNOW, without PRIMARYGOALDONE or a reward entitlement.
            captureGoal(rescue,3,std::set<PlayerId>{});queueStatus(rescue,12);world.rescueLastStatus=12;
        } else if(resolved && world.rescued>=12) {
            const auto *rescuer=ports_.players.find(*rescuedBy);
            if(rescuer && rescuer->entered) {
                const auto &record=rescuer->persistent.player;
                const auto eligible=!rewardPending(record,rescue) && !unavailable(*rescuedBy,record,rescue)?
                    eligibleDelivery(rescue,3,*rescuedBy):std::set<PlayerId>{};
                captureGoal(rescue,3,eligible);
                queueStatus(rescue,3);world.rescueLastStatus=3;
            }
        }
    }
}
DomainResult<bool> System::activatePrisoner(EntityId id,PlayerId playerId,uint64_t tick) {
    updatePrisonerCounters();resolvePrisoners({});
    const auto *unit=ports_.monsters.find(id);const auto *player=ports_.players.find(playerId);
    if(!unit || !unit->alive() || !unit->questNpc || unit->identity.monster!="act5pow" || !player || !player->entered)
        return {DomainStatus::Stale,{}};
    const auto &area=ports_.areas.at(unit->area);const auto &playerArea=ports_.areas.at(player->area);
    if(area.definition.act!=playerArea.definition.act) return {DomainStatus::Applied,false};
    const auto *room=playerArea.definition.activation.room(player->position);
    if(!room) return {DomainStatus::Applied,false};
    const auto index=size_t(room-playerArea.definition.activation.rooms().data());
    const monsters::Actor *door=nullptr;bool doorDead=false;
    for(const auto near:playerArea.definition.roomNear.at(index)) for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
        const auto *other=ports_.monsters.find(candidate);
        if(!other || other->identity.monster!="prisondoor" || other->nativeMode!=12) continue;
        doorDead=true;
        const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
        if(nativeUnitDistance(unit->position,unit->rule.size,at,other->rule.size)<15) {door=other;break;}
    }
    // IsPrisonDoorDead sees any DD door; UpdateQuestState separately requires
    // one within 15 and keeps the last matching adjacent room's first door.
    if(!doorDead) return {DomainStatus::Applied,false};
    if(!door) return {DomainStatus::Applied,true};
    const auto npc=std::find_if(area.definition.npcs.begin(),area.definition.npcs.end(),[&](const auto &entry){return entry.id==id;});
    if(npc==area.definition.npcs.end() || !npc->sourceObjectPosition) return {DomainStatus::Applied,true};
    auto &world=state_.actFive;
    if(std::any_of(world.rescuePortals.begin(),world.rescuePortals.end(),[&](const auto &entry){
        return entry.group.x==npc->sourceObjectPosition->x && entry.group.y==npc->sourceObjectPosition->y;
    })) return {DomainStatus::Applied,true};
    auto point=door->position+Vec{2,0};
    auto born=ports_.objects.admit({189,door->area,point,{},{},1,tick});
    if(!born && born.status!=DomainStatus::Capacity) {
        point=door->position;born=ports_.objects.admit({189,door->area,point,{},{},1,tick});
    }
    if(!born && born.status!=DomainStatus::Capacity) {
        const auto &doorArea=ports_.areas.at(door->area);
        if(const auto target=ports_.areas.roomAtPosition(door->area,door->position,
            int(door->position.x+2+doorArea.definition.origin.x),int(door->position.y+doorArea.definition.origin.y))) {
            const auto free=ports_.areas.nativeQuestFreePosition(*target,int(door->position.x+2+doorArea.definition.origin.x),
                int(door->position.y+doorArea.definition.origin.y),2,0x8000,17,ports_.spatial.nativeCollisionSample());
            if(free) {point=free->position;born=ports_.objects.admit({189,free->room.area,point,{},{},1,tick,free->room});}
        }
    }
    if(!born && isDeferred(born.status)) return {born.status,{}};
    ActFiveState::RescuePortal entry;entry.group=*npc->sourceObjectPosition;entry.object=born?*born.value:EntityId{};entry.due=born?tick+25:0;
    for(const auto &member:area.definition.npcs) if(member.rule.code=="act5pow" && member.sourceObjectPosition &&
        member.sourceObjectPosition->x==entry.group.x && member.sourceObjectPosition->y==entry.group.y) {
        entry.prisoners.insert(member.id);
        if(const auto *live=ports_.monsters.find(member.id);live && live->alive())
            ports_.npc.escape(live->area,member.id,point);
    }
    world.rescuePortals.push_back(std::move(entry));
    if(born) {
        const auto scheduled=ports_.objects.setNativeMode(*born.value,1,tick);
        if(!scheduled) return {scheduled.status,{}};
    }
    updatePrisonerCounters();resolvePrisoners(playerId);
    if(!state_.objectives[questIndex(QuestId::RescueOnMountArreat)] && world.rescueKilled<5) {
        queueStatus(QuestId::RescueOnMountArreat,2);world.rescueLastStatus=2;
    }
    return {DomainStatus::Applied,true};
}
StepStatus System::actFiveStep(TickContext tick) {
    if(state_.actFive.anyaCompletion) {
        if(!publishCompletion(*state_.actFive.anyaCompletion,16,{},tick)) return StepStatus::Blocked;
        state_.actFive.anyaCompletion.reset();
    }
    bool blocked=false;auto &world=state_.actFive;
    updatePrisonerCounters();resolvePrisoners({});
    for(const auto &[monsterId,m]:ports_.monsters.read().actors) {
        (void)monsterId;if(m.alive() || m.owner || m.identity.origin==SpawnOrigin::Debug || m.identity.origin==SpawnOrigin::Summoned) continue;
        if(int(m.area)==110 && m.identity.superUnique=="Siege Boss") captureGoal(QuestId::SiegeOnHarrogath,3,m.area,m.position);
        if(int(m.area)==124 && m.identity.superUnique=="Nihlathak Boss" && !world.nihlathakSlain) {
            const auto quest=QuestId::BetrayalOfHarrogath;
            if(!(state_.initialFlags[questDefinition(quest).nativeSlot]&0x8000)) {
                auto eligible=eligibleParty(quest,3,m.area,m.position,{});
                QuestCompletion completion;completion.soundRecipients=eligible;
                for(const auto &[id,player]:ports_.players.all()) if(player.entered) {
                    completion.recipients.emplace(id,player.actor);
                    if(player.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage>=3)
                        completion.soundRecipients.insert(id);
                }
                captureGoal(quest,3,std::move(eligible));
                world.nihlathakCompletion=std::move(completion);
            }
            world.nihlathakSlain=true;
        }
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=4) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));const auto before=book;
        const auto available=[&](QuestId quest){return !unavailable(id,record,quest);};
        const int level=int(p.area);
        if(available(QuestId::SiegeOnHarrogath) && level>=110 && level<=112 && book[questIndex(QuestId::SiegeOnHarrogath)].stage==1) book[questIndex(QuestId::SiegeOnHarrogath)].stage=2;
        if(available(QuestId::RescueOnMountArreat) && level==111) {
            auto &q=book[questIndex(QuestId::RescueOnMountArreat)];if(q.stage<3) {q.stage=2;q.flags=(q.flags&~rescueCountMask)|world.rescued;}
        }
        if(available(QuestId::PrisonOfIce) && level!=109 && book[questIndex(QuestId::PrisonOfIce)].stage==1) book[questIndex(QuestId::PrisonOfIce)].stage=2;
        if(available(QuestId::BetrayalOfHarrogath) && level>=121 && level<=124 && book[questIndex(QuestId::BetrayalOfHarrogath)].stage<2) book[questIndex(QuestId::BetrayalOfHarrogath)].stage=2;
        if(available(QuestId::RiteOfPassage) && level!=109 && book[questIndex(QuestId::RiteOfPassage)].stage==1) book[questIndex(QuestId::RiteOfPassage)].stage=2;
        if(available(QuestId::RiteOfPassage) && level==120 && book[questIndex(QuestId::RiteOfPassage)].stage<3) book[questIndex(QuestId::RiteOfPassage)].stage=3;
        const unsigned baal=level==132?4u:level==131?3u:level>=128 && level<=130?2u:0u;
        if(baal && available(QuestId::EveOfDestruction)) book[questIndex(QuestId::EveOfDestruction)].stage=std::max(book[questIndex(QuestId::EveOfDestruction)].stage,baal);
        if(level==109 && book[questIndex(QuestId::BetrayalOfHarrogath)].stage && !world.anyaPortal &&
            (book[questIndex(QuestId::BetrayalOfHarrogath)].stage<4 || !p.persistent.waypoints.contains(RegionId(123)))) {
            state_.npcStory.anyaPortalPending=true;
        }
        if(level==124) {const auto result=spawnQuestGroup(actor,"Nihlathak Boss");if(!result) blocked=true;}
        if(!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;}) && !commit(actor,std::move(record))) blocked=true;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
