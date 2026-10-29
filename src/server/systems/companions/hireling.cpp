#include "gameplay/quest/catalog.hpp"
#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/effects/queries.hpp"
#include "server/systems/npc/system.hpp"
#include "hireling_equipment.hpp"
#include "hireling_birth.hpp"
#include "server/systems/inventory/planning.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::companions {
DomainResult<> System::grantHireling(const ActorContext &actor,HirelingRecord record) {
    const auto result=replaceHireling(actor,std::move(record),0);
    if(result.status==DomainStatus::PendingContent) return {DomainStatus::Applied,std::monostate{}};
    if(!result) return {result.status,{}};
    return *result.value?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Unavailable,{}};
}
DomainResult<bool> System::replaceHireling(const ActorContext &actor,HirelingRecord record,unsigned price,
    std::optional<Request> request,std::optional<Vec> seller) {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
        ports_.areas.at(actor.area).generation!=actor.areaGeneration || player->persistent.player.hp<=0 ||
        record.sourceRow<0 || record.level<1 || record.level>player->persistent.player.level) return {DomainStatus::InvalidActor,{}};
    if(player->inventoryRevision==UINT64_MAX || player->characterRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(uint64_t(player->persistent.player.gold)+player->persistent.player.bankGold<price) return {DomainStatus::Unavailable,{}};
    auto candidate=player->persistent;candidate.player.hireling=record;
    std::vector<ItemChange> changes;
    std::set<EntityId> removed;
    for(auto it=candidate.inventory.items.begin();it!=candidate.inventory.items.end();) {
        const auto *at=std::get_if<ContainerLocation>(&it->second.location);
        if(!at || at->container!=candidate.containers.hirelingEquipment) {++it;continue;}
        if(it->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        removed.insert(it->first);changes.push_back({it->first,it->second.revision+1,ItemChangeKind::Removed,it->second.location,{},0});
        it=candidate.inventory.items.erase(it);
    }
    std::erase_if(candidate.inventory.itemOrder,[&](EntityId id){return removed.contains(id);});
    if(auto container=candidate.inventory.containers.find(candidate.containers.hirelingEquipment);container!=candidate.inventory.containers.end())
        container->second.registeredWeapon={};
    auto pending=hirelingReplacements_.find(actor.player);
    if(pending==hirelingReplacements_.end() || !sameHirelingIdentity(record,pending->second.source.record) ||
        pending->second.source.inventoryRevision!=player->inventoryRevision || pending->second.characterRevision!=player->characterRevision) {
        if(nextHirelingReplacement_==UINT64_MAX) return {DomainStatus::Capacity,{}};
        HirelingPreparation source{actor,record,candidate.difficulty,player->inventoryRevision,candidate,player->rules.items,player->rules.equipment,0,nextHirelingReplacement_++};
        hirelingReplacements_.insert_or_assign(actor.player,HirelingReplacement{std::move(source),player->characterRevision,std::move(request),{}});
        return {DomainStatus::PendingContent,{}};
    }
    if(!pending->second.prepared) return {DomainStatus::PendingContent,{}};
    const auto &prepared=*pending->second.prepared;
    if(!prepared.deferred.empty()) return {DomainStatus::NotImplemented,{}};
    const auto birth=prepareHirelingBirth(actor,candidate,prepared,seller,true);
    if(!birth) return {birth.status,{}};
    auto &publication=**birth.value;
    if(!publication.units) {
        std::optional<EventBatch> failure;
        if(request) failure=EventBatch{0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
            {MerchantFact{request->npc,EntityId{UINT32_MAX},0,15,player->persistent.player.gold}}};
        const auto consumed=ports_.transactions.consumeRoomRandom(publication.rooms,std::move(failure));
        return consumed?DomainResult<bool>{DomainStatus::Applied,false}:DomainResult<bool>{consumed.status,{}};
    }
    candidate.player.hireling.hp=Fixed8::fromRaw(nativeChecked(publication.units->actors->actors.begin()->second.life));
    const auto wallet=std::min(candidate.player.gold,price);candidate.player.gold-=wallet;candidate.player.bankGold-=price-wallet;
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(candidate.inventory),std::move(changes),candidate.player.weaponSet};
    edit.character=candidate.player;
    if(request) {
        HirelingListFact remaining{request->npc,{}};
        const auto &list=hirelingLists_.at(actor.player);
        for(const auto &offer:list.offers)
            if(offer.name!=*request->offer && ports_.npc.hirelingAvailable(list.source.seller,list.source.rosterRevision,offer.name,offer.record.seed))
                remaining.offers.emplace_back(offer.name,offer.record.seed);
        edit.facts.emplace_back(std::move(remaining));
        edit.facts.emplace_back(MerchantFact{request->npc,{},0,5,candidate.player.gold});
    }
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication.units);
    if(!plan) return {plan.status,{}};
    plan.value->gameRandom=transactions::UnitRandomEdit{publication.expectedRandom,publication.nextRandom};
    plan.value->roomRandomLoans=publication.rooms;
    auto nextRules=hirelingRules_;auto installed=prepared;
    installed.source.inventoryRevision=player->inventoryRevision+1;
    installed.source.record=candidate.player.hireling;installed.source.token=0;
    nextRules.insert_or_assign(actor.player,std::move(installed));
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return {result.status,{}};
    ports_.monsters.publishBirthComponents(publication.components);state_.companions.swap(publication.companions);
    hirelingRules_.swap(nextRules);
    return {DomainStatus::Applied,true};
}
std::vector<HirelingListPreparation> System::pendingHirelingLists() const {
    std::vector<HirelingListPreparation> result;
    for (const auto &[id, request] : pendingLists_) { (void)id; result.push_back(request); }
    return result;
}
DomainResult<> System::install(PreparedHirelingList prepared) {
    const auto pending = pendingLists_.find(prepared.source.actor.player);
    if (pending == pendingLists_.end() || pending->second.token != prepared.source.token) return {DomainStatus::Stale,{}};
    const auto source=pending->second;
    prepared.source=source;
    const auto *player = ports_.players.find(source.actor.player);
    const auto access = ports_.npc.service(source.actor, source.npc);
    if (!player || !access || access->conversation->revision != source.conversation ||
        player->persistent.player.level != source.level || player->persistent.difficulty != source.difficulty) {
        pendingLists_.erase(pending); return {DomainStatus::Stale,{}};
    }
    for(const auto &entry:source.entries) if(!ports_.npc.hirelingAvailable(source.seller,source.rosterRevision,entry.name,entry.seed)) {
        pendingLists_.erase(pending);return {DomainStatus::Stale,{}};
    }
    if(prepared.deferred.empty()) {
        if(prepared.offers.size()!=source.entries.size()) return {DomainStatus::InvalidRequest,{}};
        for(size_t index=0;index<source.entries.size();++index) {
            const auto &entry=source.entries[index];const auto &offer=prepared.offers[index];
            if(offer.name!=entry.name || offer.record.seed!=entry.seed) return {DomainStatus::InvalidRequest,{}};
        }
    } else prepared.offers.clear();
    HirelingListFact fact{source.npc,{}};
    if (prepared.deferred.empty()) for (const auto &offer : prepared.offers) fact.offers.emplace_back(offer.name,offer.record.seed);
    auto next = hirelingLists_;
    next.insert_or_assign(source.actor.player,std::move(prepared));
    const auto sent = ports_.events.publish({0,source.actor.tick,{}, {AudienceKind::Player,source.actor.player,source.actor.area},{std::move(fact)}});
    if (!sent) return {sent.status,{}};
    hirelingLists_.swap(next); pendingLists_.erase(pending);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if(request.action==Action::CommandInteract || request.action==Action::CommandMove) {
        const auto *player=ports_.players.find(actor.player);const auto companion=state_.companions.find(request.npc);
        const auto *body=ports_.monsters.find(request.npc);
        if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 || !body || !body->alive() ||
            companion==state_.companions.end() || companion->second.owner!=actor.player || companion->second.kind!=Kind::Hireling)
            return {DomainStatus::InvalidActor,{}};
        const auto *area=ports_.areas.find(actor.area);if(!area || area->generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        if(request.action==Action::CommandMove) {
            if(!request.destination || std::abs(int(request.destination->x)-int(player->position.x))>50 ||
                std::abs(int(request.destination->y)-int(player->position.y))>50) return {DomainStatus::InvalidRequest,{}};
            return ports_.ai.replacePetCommand(body->id,13,{},*request.destination+area->definition.origin,actor.tick);
        }
        bool ownedItem=request.target.type==4 && player->persistent.inventory.items.contains(request.target.id);
        const auto at=ports_.spatial.locate(request.target.id,request.target.type);
        if(!at) return {DomainStatus::Stale,{}};
        if(!ownedItem) {
            const auto *other=ports_.areas.find(at->first);if(!other || other->definition.act!=area->definition.act) return {DomainStatus::InvalidRequest,{}};
            const auto delta=at->second+other->definition.origin-player->position-area->definition.origin;
            if(std::abs(int(delta.x))>50 || std::abs(int(delta.y))>50) return {DomainStatus::InvalidRequest,{}};
        }
        return ports_.ai.replacePetCommand(body->id,12,request.target,{},actor.tick);
    }
    if(request.action==Action::Potion) return request.item?drink(actor,*request.item,request.belt):DomainResult<>{DomainStatus::InvalidRequest,{}};
    if(request.action==Action::Equipment) return request.offer?equipHireling(actor,*request.offer):DomainResult<>{DomainStatus::InvalidRequest,{}};
    const auto *player = ports_.players.find(actor.player);
    const auto access = ports_.npc.service(actor,request.npc);
    if (!player || !access || (access->npc->rule.code!="kashya" && access->npc->rule.code!="greiz" && access->npc->rule.code!="asheara" && access->npc->rule.code!="tyrael" && access->npc->rule.code!="qual-kehk")) return {DomainStatus::InvalidRequest,{}};
    if(access->npc->rule.code=="tyrael" && request.action!=Action::Resurrect) return {DomainStatus::InvalidRequest,{}};
    const auto reject = [&](uint8_t result) -> DomainResult<> {
        const auto sent = ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
            {MerchantFact{request.npc,EntityId{UINT32_MAX},0,result,player->persistent.player.gold}}});
        return sent ? DomainResult<>{DomainStatus::Applied,std::monostate{}} : DomainResult<>{sent.status,{}};
    };
    if (request.action == Action::List) {
        if (pendingLists_.contains(actor.player)) return {DomainStatus::Conflict,{}};
        if (pendingLists_.size() >= 8 || nextList_ == UINT64_MAX) return {DomainStatus::Capacity,{}};
        const auto roster=ports_.npc.hirelingRoster(access->npc->rule);
        if(!roster) return {roster.status,{}};
        const auto existing = hirelingLists_.find(actor.player);
        if (existing != hirelingLists_.end() && existing->second.source.conversation == access->conversation->revision &&
            existing->second.source.level == player->persistent.player.level && existing->second.source.rosterRevision==roster.value->revision &&
            std::all_of(existing->second.offers.begin(),existing->second.offers.end(),[&](const auto &offer) {
                return ports_.npc.hirelingAvailable(existing->second.source.seller,existing->second.source.rosterRevision,offer.name,offer.record.seed);
            })) {
            HirelingListFact fact{request.npc,{}};
            for (const auto &offer : existing->second.offers) fact.offers.emplace_back(offer.name,offer.record.seed);
            const auto sent = ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{std::move(fact)}});
            return sent ? DomainResult<>{DomainStatus::Applied,std::monostate{}} : DomainResult<>{sent.status,{}};
        }
        HirelingListPreparation source{actor,request.npc,access->conversation->revision,nextList_,roster.value->revision,
            access->npc->rule.nativeClass,player->persistent.difficulty,player->persistent.player.level};
        for(size_t index=0;index<roster.value->slots.size();++index) {
            const auto &slot=roster.value->slots[index];
            if(slot.available && !slot.hired) source.entries.push_back({uint32_t(index+1),slot.name,slot.seed});
        }
        pendingLists_.emplace(actor.player,std::move(source));
        ++nextList_;
        return {DomainStatus::Applied,std::monostate{}};
    }
    auto record = player->persistent.player;
    std::shared_ptr<effects::AuraUnitPublication> revivedUnits;
    std::optional<EntityId> revivedActor;
    std::vector<EntityId> relocatedPets;
    std::vector<EntityId> retiredPets;
    unsigned price = 0;
    if (request.action == Action::Hire) {
        const auto &quest = record.quests.at(size_t(player->persistent.difficulty)).at(questIndex(QuestId::SistersBurialGrounds));
        if (access->npc->rule.code=="kashya" && record.level < 8 && quest.stage < questCompletionStage(QuestId::SistersBurialGrounds)) return reject(11);
        if(access->npc->rule.code=="qual-kehk" && record.quests.at(size_t(player->persistent.difficulty)).at(questIndex(QuestId::RescueOnMountArreat)).stage<questCompletionStage(QuestId::RescueOnMountArreat)) return reject(11);
        const auto list = hirelingLists_.find(actor.player);
        if (!request.offer || list == hirelingLists_.end() || list->second.source.conversation != access->conversation->revision ||
            list->second.source.level != record.level) return reject(9);
        const auto candidate = std::find_if(list->second.offers.begin(),list->second.offers.end(),[&](const auto &entry){return entry.name == *request.offer;});
        if (candidate == list->second.offers.end()) return reject(9);
        if(!ports_.npc.hirelingAvailable(list->second.source.seller,list->second.source.rosterRevision,candidate->name,candidate->record.seed)) return reject(9);
        if(uint64_t(record.gold)+record.bankGold<candidate->price) return reject(12);
        auto roster=ports_.npc.prepareHirelingHired(list->second.source.seller,list->second.source.rosterRevision,candidate->name,candidate->record.seed);
        if(!roster) return {roster.status,{}};
        const auto *seller=ports_.monsters.find(request.npc);
        if(!seller || seller->area!=actor.area) return {DomainStatus::Unavailable,{}};
        const auto result=replaceHireling(actor,candidate->record,candidate->price,request,seller->position);
        if(result.status==DomainStatus::PendingContent) return {DomainStatus::Applied,std::monostate{}};
        if(!result) {
            if(!isDeferred(result.status)) hirelingReplacements_.erase(actor.player);
            return result.status==DomainStatus::Unavailable?reject(15):DomainResult<>{result.status,{}};
        }
        if(!*result.value) {
            hirelingReplacements_.erase(actor.player);return {DomainStatus::Applied,std::monostate{}};
        }
        ports_.npc.publishVendorControl(std::move(*roster.value));
        hirelingReplacements_.erase(actor.player);
        auto &offers=list->second;
        std::erase_if(offers.offers,[&](const auto &offer){return !ports_.npc.hirelingAvailable(offers.source.seller,offers.source.rosterRevision,offer.name,offer.record.seed);});
        std::erase_if(offers.source.entries,[&](const auto &entry){return !ports_.npc.hirelingAvailable(offers.source.seller,offers.source.rosterRevision,entry.name,entry.seed);});
        if(offers.offers.empty()) hirelingLists_.erase(actor.player);
        return {DomainStatus::Applied,std::monostate{}};
    } else if (request.action == Action::Resurrect) {
        const auto prepared=hirelingRules_.find(actor.player);
        if (record.hireling.sourceRow < 0 || record.hireling.hp > 0 || prepared==hirelingRules_.end() ||
            !sameHirelingIdentity(record.hireling,prepared->second.source.record) || !prepared->second.deferred.empty()) return reject(9);
        price=unsigned(std::min<int64_t>(50000,15LL*record.hireling.level*record.hireling.level/2));
        if(!player->rules.items || !player->rules.equipment) return reject(9);
        record.hireling.hp=calculateHirelingEquipment(player->persistent,*player->rules.items,*player->rules.equipment,prepared->second).life;
        for(const auto &[id,pet]:state_.companions) {
            if(pet.owner!=actor.player || pet.kind!=Kind::Hireling) continue;
            const auto *live=ports_.monsters.find(id);
            if(!live || live->alive() || live->revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
            auto candidate=*live;
            const Vec restored{std::floor(player->position.x)+.5f,std::floor(player->position.y)+.5f};
            if(!ports_.areas.at(actor.area).definition.activation.room(restored)) return {DomainStatus::Unavailable,{}};
            candidate.area=actor.area;candidate.position=candidate.home=restored;candidate.roomless=false;
            candidate.revivalPosition=PointTarget{actor.area,actor.areaGeneration,restored};
            candidate.life=candidate.maximumLife;candidate.inactivePet=candidate.petUnregistered=false;
            candidate.aiPose=candidate.nativeMode=1;candidate.holdPose=false;candidate.modeAlive=false;candidate.deathMode=false;
            candidate.busyUntil=candidate.poseUntil=candidate.riseUntil=candidate.reactionUntil=0;
            candidate.neutralAt.reset();candidate.delayedDeathSource.reset();candidate.corpse={};
            candidate.frozenUntil=0;candidate.shatterActive=false;
            candidate.route.clear();candidate.pathStep={};candidate.moving=candidate.running=false;candidate.movementTarget={};
            candidate.skillMotion.reset();candidate.nativeActionUntil=0;candidate.modeFrame.reset();
            candidate.killer={};candidate.knockedUntil=0;candidate.knockbackGoal.reset();++candidate.revision;
            effects::UnitDamageStates states;states.source=id;
            states.clearStateFlags={candidate.rule.frozenState.id,candidate.rule.shatterState.id,prepared->second.recycledState};
            revivedUnits=std::make_shared<effects::AuraUnitPublication>(ports_.effects.prepareDamageStates(std::move(candidate),states,actor.tick));
            auto &life=revivedUnits->life.at(id);life.life=life.maximum;life.changed=true;
            record.hireling.hp=life.life;
            revivedActor=id;break;
        }
        if(!revivedActor) return {DomainStatus::PendingContent,{}};
        if(!player->rules.skills) return {DomainStatus::Unavailable,{}};
        // SUnitNpc::sub_6FCC84D0 -> PlayerPets::sub_6FC7E310 also
        // relocates the owner's living warp-enabled pets to this coordinate.
        auto actors=revivedUnits->actors->actors;
        std::vector<EntityId> order{*revivedActor};
        for(const auto id:ports_.monsters.pets(actor.player,true)) {
            if(id==*revivedActor) continue;
            const auto *live=ports_.monsters.find(id);
            if(!live || live->rule.petType<=0) continue;
            if(!player->rules.skills->petTypeBaseLimits.contains(live->rule.petType)) return {DomainStatus::Unavailable,{}};
            const bool warp=player->rules.skills->warpPetTypes.contains(live->rule.petType);
            const bool range=player->rules.skills->rangePetTypes.contains(live->rule.petType);
            // Native flags gate only the warp/range branches. A type with
            // neither flag loses its entire list, including inactive corpses.
            if((live->inactivePet || !live->alive()) && (warp || range)) continue;
            if(!warp && !range) {
                if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
                auto candidate=*live;candidate.petUnregistered=true;++candidate.revision;
                actors.emplace(id,std::move(candidate));retiredPets.push_back(id);order.push_back(id);continue;
            }
            if(!warp) {
                const auto from=live->position+ports_.areas.at(live->area).definition.origin;
                const auto to=player->position+ports_.areas.at(actor.area).definition.origin;
                const int64_t x=int64_t(std::floor(from.x))-int64_t(std::floor(to.x));
                const int64_t y=int64_t(std::floor(from.y))-int64_t(std::floor(to.y));
                // PlayerPets: range-only types survive within squared 1600;
                // types with neither flag leave the active list unconditionally.
                if(x*x+y*y>1600) {retiredPets.push_back(id);order.push_back(id);}
                continue;
            }
            if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            auto candidate=*live;
            candidate.area=actor.area;candidate.position={std::floor(player->position.x)+.5f,std::floor(player->position.y)+.5f};
            candidate.revivalPosition=PointTarget{actor.area,actor.areaGeneration,candidate.position};
            candidate.route.clear();candidate.pathStep={};candidate.moving=candidate.running=false;++candidate.revision;
            actors.emplace(id,std::move(candidate));
            relocatedPets.push_back(id);order.push_back(id);
        }
        revivedUnits->actors=std::make_shared<monsters::ActorPublication>(ports_.monsters.prepareActorPublication(std::move(actors),0));
        const auto retired=ports_.monsters.stageRetirements(*revivedUnits,retiredPets,actor.tick);if(!retired) return retired;
        for(const auto id:retiredPets) revivedUnits->actors->actors.at(id).petUnregistered=true;
        revivedUnits->actors->publicationOrder=std::move(order);
    } else return {DomainStatus::NotImplemented,{}};
    if (uint64_t(record.gold)+record.bankGold < price) return reject(12);
    const auto wallet=std::min(record.gold,price);record.gold-=wallet;record.bankGold-=price-wallet;
    DomainResult<transactions::Plan> plan;
    {
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
        edit.facts.emplace_back(MerchantFact{request.npc,revivedActor.value_or(EntityId{}),0,5,edit.player.gold});
        if(revivedUnits) {
            for(auto &fact:revivedUnits->facts) edit.publicFacts.push_back(std::move(fact));
            revivedUnits->facts.clear();
            const auto &body=revivedUnits->actors->actors.at(*revivedActor);
            edit.publicFacts.emplace_back(HitFact{body.id,1,body.area,128,false,body.position});
        }
        plan=ports_.transactions.prepare(std::move(edit),nullptr,{},revivedUnits);
    }
    if(!plan) return {plan.status,{}};
    const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    for(const auto id:relocatedPets) {
        const auto found=state_.companions.find(id);if(found==state_.companions.end()) continue;
        auto &pet=found->second;pet.ownerTeleport=player->teleportRevision;pet.nativePetMoving=false;
    }
    for(const auto id:retiredPets) {
        const auto found=state_.companions.find(id);const auto *body=ports_.monsters.find(id);
        if(found==state_.companions.end() || !body) continue;
        auto &pet=found->second;
        pet.removeAt=actor.tick+uint64_t(body->rule.deathTicks);pet.release=0;
        pet.pendingAction.reset();pet.pendingAura.reset();
    }
    if(revivedActor) {
        auto &pet=state_.companions.at(*revivedActor);
        pet.pendingAction.reset();pet.pendingAura.reset();pet.target={};pet.release=pet.removeAt=0;
        pet.pendingHirelingUseSkill=pet.cancelAi=false;
        pet.nextDecision=actor.tick+1;pet.ownerTeleport=player->teleportRevision;
        return committed;
    }
    return committed;
}
DomainResult<> System::equipHireling(const ActorContext &actor, uint32_t bodySlot) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const auto prepared=hirelingRules_.find(actor.player);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || prepared==hirelingRules_.end() || !prepared->second.deferred.empty() ||
        !player->rules.items || !player->rules.equipment || ports_.skills.busy(actor.actor,actor.tick)) return {DomainStatus::InvalidActor,{}};
    const monsters::Actor *merc=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==actor.player && pet.kind==Kind::Hireling) {merc=ports_.monsters.find(id);break;}
    if(!merc || !merc->alive() || merc->life<=0 || merc->area!=actor.area || !area->definition.activation.nearby(player->position,merc->position)) return {DomainStatus::Unavailable,{}};
    auto candidate=player->persistent;
    const auto &containers=candidate.containers;
    auto cursor=candidate.inventory.items.end();
    for(auto item=candidate.inventory.items.begin();item!=candidate.inventory.items.end();++item)
        if(const auto *at=std::get_if<ContainerLocation>(&item->second.location);at && at->container==containers.cursor) {cursor=item;break;}
    EquipmentSlot slot;
    if(cursor!=candidate.inventory.items.end()) {
        const auto *definition=player->rules.items->find(cursor->second.definition);
        if(player->rules.potions && player->rules.potions->contains(cursor->second.definition)) return drink(actor,cursor->second.handle(),false);
        if(!definition || !cursor->second.identified || definition->equipment.isType("ques") ||
            (player->rules.equipment->at(cursor->first,candidate.player.hireling.level).maximumDurability && !cursor->second.durability)) return {DomainStatus::InvalidRequest,{}};
        if(definition->equipment.isType("helm") && definition->equipment.fits(EquipmentSlot::Head)) slot=EquipmentSlot::Head;
        else if(definition->equipment.isType("tors") && definition->equipment.fits(EquipmentSlot::Torso)) slot=EquipmentSlot::Torso;
        else if(prepared->second.act==3 && definition->equipment.isType("shld") && definition->equipment.fits(EquipmentSlot::LeftHand)) slot=EquipmentSlot::LeftHand;
        else if((definition->equipment.isType(prepared->second.weaponType) || (!prepared->second.weaponType2.empty() && definition->equipment.isType(prepared->second.weaponType2))) &&
            definition->equipment.fits(EquipmentSlot::RightHand) && (prepared->second.act!=3 || !definition->equipment.twoHanded)) slot=EquipmentSlot::RightHand;
        else if(definition->beltRows) slot=EquipmentSlot::Belt;
        else if(definition->equipment.isType("boot") && definition->equipment.fits(EquipmentSlot::Feet)) slot=EquipmentSlot::Feet;
        else if(definition->equipment.isType("glov") && definition->equipment.fits(EquipmentSlot::Gloves)) slot=EquipmentSlot::Gloves;
        else return {DomainStatus::InvalidRequest,{}};
    } else {
        if(bodySlot<1 || bodySlot>10 || (bodySlot==5 && prepared->second.act!=3)) return {DomainStatus::InvalidRequest,{}};
        slot=EquipmentSlot(bodySlot-1);
    }
    if(!(prepared->second.equipmentSlots & (1u<<unsigned(slot)))) return {DomainStatus::InvalidRequest,{}};
    auto equipped=candidate.inventory.items.end();
    const ContainerLocation destination{containers.hirelingEquipment,{int(slot),0}};
    for(auto item=candidate.inventory.items.begin();item!=candidate.inventory.items.end();++item)
        if(item->second.location==ItemLocation{destination}) {equipped=item;break;}
    if(cursor==candidate.inventory.items.end() && equipped==candidate.inventory.items.end()) return {DomainStatus::InvalidRequest,{}};
    if(cursor!=candidate.inventory.items.end()) {
        const auto stats=calculateHirelingEquipment(candidate,*player->rules.items,*player->rules.equipment,prepared->second,
            equipped==candidate.inventory.items.end()?EntityId{}:equipped->first);
        const auto loadout=hirelingLoadout(candidate,*player->rules.items,*player->rules.equipment);
        if(loadout.requirements({&cursor->second,player->rules.items->find(cursor->second.definition)},stats.actor)!=InventoryError::None)
            return {DomainStatus::InvalidRequest,{}};
    }
    std::vector<ItemChange> changes;
    const auto move=[&](auto item,ContainerLocation location) {
        if(item->second.revision==UINT64_MAX) return false;
        const auto before=item->second.location;item->second.location=location;++item->second.revision;
        changes.push_back({item->first,item->second.revision,ItemChangeKind::Moved,before,item->second.location,item->second.quantity});return true;
    };
    if(cursor!=candidate.inventory.items.end() && !move(cursor,destination)) return {DomainStatus::Capacity,{}};
    if(equipped!=candidate.inventory.items.end() && !move(equipped,{containers.cursor,{}})) return {DomainStatus::Capacity,{}};
    if(player->inventoryRevision==UINT64_MAX || merc->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto states=ports_.effects.unitStates(merc->id,actor.tick);
    auto sources=ports_.effects.unitStatSources(merc->id,actor.tick);
    auto body=prepareHirelingBody(*merc,candidate,prepared->second,*player->rules.items,*player->rules.equipment,
        player->inventoryRevision+1,player->characterRevision+1,states,sources,actor.tick);
    std::map<EntityId,monsters::Actor> bodies;bodies.emplace(body.id,std::move(body));
    auto auraUnits=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*auraUnits,std::move(bodies),0);
    ports_.effects.stageAuraSource(*auraUnits,merc->id,
        [live=*merc,owner=candidate,prepared=prepared->second,items=player->rules.items,equipment=player->rules.equipment,
         inventory=player->inventoryRevision+1,character=player->characterRevision+1,tick=actor.tick]
        (monsters::Actor &body,const CombatEffectSet &states,const effects::AuraUnitPublication::SourceMutation &mutation) {
            auto sources=combatEffectStatSources(states.entries(),tick,true);
            body=prepareHirelingBody(live,owner,prepared,*items,*equipment,inventory,character,states.stateIds(tick,true),sources,tick,false,mutation);
        });
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(candidate.inventory),std::move(changes),candidate.player.weaponSet};
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},auraUnits);if(!plan) return {plan.status,{}};
    return ports_.transactions.commit(std::move(*plan.value));
}
DomainResult<> System::drink(const ActorContext &actor,ItemHandle item,bool belt) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || !player->rules.potions || !player->rules.items || !player->rules.equipment || !player->rules.character)
        return {DomainStatus::InvalidActor,{}};
    const monsters::Actor *merc=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==actor.player && pet.kind==Kind::Hireling) {merc=ports_.monsters.find(id);break;}
    inventory::detail::Draft draft(*player,*player->rules.items,*player->rules.equipment,*player->rules.character);
    const auto *source=draft.resolve(item);if(!source || !source->quantity || source->revision==UINT64_MAX) return {DomainStatus::Stale,{}};
    const auto *at=std::get_if<ContainerLocation>(&source->location);
    if(!at || at->container!=(belt?draft.containers().belt:draft.containers().cursor)) return {DomainStatus::InvalidRequest,{}};
    if(belt && draft.at({draft.containers().cursor,{}})) return {DomainStatus::Conflict,{}};
    const auto definition=player->rules.potions->find(source->definition);
    if(definition==player->rules.potions->end()) return {DomainStatus::NotImplemented,{}};
    const auto &potion=definition->second;
    if(potion.kind!=PotionKind::Healing && potion.kind!=PotionKind::Rejuvenation && potion.kind!=PotionKind::Remedy) return {DomainStatus::InvalidRequest,{}};
    if ((!merc || !merc->alive() || merc->life<=0) && belt)
        return ports_.inventory.execute(actor,{UseItem{item},inventory::Source::Belt});
    if(!merc || !merc->alive() || merc->life<=0 || (!belt && (merc->area!=actor.area || !area->definition.activation.nearby(player->position,merc->position)))) return {DomainStatus::Unavailable,{}};
    if(merc->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto recipient=*merc;++recipient.revision;
    effects::UnitDamageStates statuses;statuses.source=merc->id;
    auto random=merc->combatRandom;
    if(potion.kind==PotionKind::Healing) {
        const auto stats=ports_.effects.unitStatValues(merc->id,actor.tick);
        if(!stats || !merc->rule.stats) return {DomainStatus::Unavailable,{}};
        const auto amount=rollPotionRestoration(potion,{},logicalStatValue(*stats,*merc->rule.stats,"vitality"),random);
        if(amount<=0 || potion.state.id<0 || !potion.durationFrames) return {DomainStatus::Unavailable,{}};
        statuses.potionRestoration=nativeChecked(amount);
    } else if(potion.kind==PotionKind::Rejuvenation) {
        if(potion.amount<=0 || potion.amount>100) return {DomainStatus::Unavailable,{}};
        recipient.life=std::min(merc->maximumLife,int64_t(nativeAdd(nativeChecked(merc->life),nativePercentage(nativeChecked(merc->maximumLife),potion.amount,100))));
    }
    recipient.combatRandom=random;
    if(potion.curesPoison) recipient.poison.reset();
    if(potion.curesCold) {recipient.chilledUntil=recipient.frozenUntil=0;recipient.shatterActive=false;}
    statuses.curePoison=potion.curesPoison;statuses.cureCold=potion.curesCold;
    if(potion.state.id>=0) statuses.potion=potion;
    const auto location=*at;
    auto &consumed=draft.edit.inventory.items.at(item.id);--consumed.quantity;++consumed.revision;
    const bool removed=!consumed.quantity;
    draft.edit.changes.push_back({item.id,consumed.revision,removed?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,
        consumed.location,removed?std::nullopt:std::optional{consumed.location},consumed.quantity});
    if(removed) draft.edit.inventory.items.erase(item.id);
    if(removed && belt) {
        const auto result=draft.compactBeltColumn(location.cell.x);
        if(result!=DomainStatus::Applied) return {result,{}};
    }
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),player->persistent.player.weaponSet};
    auto units=std::make_shared<effects::AuraUnitPublication>(ports_.effects.prepareDamageStates(std::move(recipient),statuses,actor.tick));
    for(auto &fact:units->facts) edit.publicFacts.push_back(std::move(fact));
    units->facts.clear();
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},std::move(units));if(!plan) return {plan.status,{}};
    return ports_.transactions.commit(std::move(*plan.value));
}
}
