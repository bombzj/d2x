#include "system.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
namespace d2x::server::loot {
std::optional<Preparation> System::beginPreparation(EntityId source) {
    const auto found=state_.pending.find(source);
    if(found==state_.pending.end()) return {};
    auto &pending=found->second;
    if(!pending.prepared) {
        pending.uniques=state_.uniques;
        if(pending.monsterSeed) if(const auto *monster=ports_.monsters.find(source)) {
            pending.seed=monster->combatRandom;pending.monsterSeed=pending.seed;
        }
        if(pending.objectSeed) {
            const auto object=ports_.objects.read().objects.find(source);
            if(object!=ports_.objects.read().objects.end()) {pending.seed=object->second.random;pending.objectSeed=pending.seed;}
        }
        if(pending.request.object && (pending.request.object->operation==19 || pending.request.object->operation==20)) {
            const auto &request=pending.request;
            const auto *area=ports_.areas.find(request.source.region);
            if(!area) return {};
            const auto position=request.position+area->definition.origin;
            const auto room=ports_.areas.roomAtPosition(request.source.region,request.position,int(position.x),int(position.y));
            if(!room) return {};
            const auto loan=ports_.areas.roomRandom(*room);
            if(!loan) return {};
            pending.roomSeed=*loan.value;pending.seed=loan.value->next;
        }
    }
    return pending;
}
std::optional<ItemSeeds> System::reserveSeeds(EntityId source,uint64_t occurrence,size_t index) {
    const auto found=state_.pending.find(source);
    if(found==state_.pending.end() || found->second.request.occurrence!=occurrence) return {};
    auto &seeds=found->second.itemSeeds;
    if(index<seeds.size()) return seeds[index];
    if(index!=seeds.size() || index>=64) return {};
    auto random=ports_.random;
    const ItemSeeds next{rollRandom(random),rollRandom(random)};
    seeds.push_back(next);
    ports_.random=random;
    return next;
}
std::set<size_t> System::prepareUniques(const std::set<size_t> &additions) const {
    auto next=state_.uniques; next.insert(additions.begin(),additions.end()); return next;
}
DomainResult<> System::queue(const Request &request,std::optional<transactions::CharacterEdit> payment) {
    if (state_.pending.contains(request.source.source)) return {DomainStatus::Applied, std::monostate{}};
    if (state_.pending.size() >= 256 || state_.nextOrder==UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto *player = ports_.players.find(request.beneficiary);
    const bool ownerless=request.environment && !request.beneficiary.value && (request.object || request.towerGold);
    if (!player && !ownerless) return {DomainStatus::InvalidActor, {}};
    if(ownerless && (payment || request.questClaimant || (request.object && (request.object->key.id || request.object->ingredient.id))))
        return {DomainStatus::InvalidRequest,{}};
    Preparation pending{request, player?player->persistent:PersistentCharacter{}, player?player->definition.code:std::string{}, 0,
        player?player->totals.character.combat.magicFind:0, player?player->totals.character.combat.goldFind:0, state_.uniques};
    pending.magicFind=int(std::clamp(int64_t(pending.magicFind)+request.additionalMagicFind,int64_t(INT32_MIN),int64_t(INT32_MAX)));
    pending.goldFind=int(std::clamp(int64_t(pending.goldFind)+request.additionalGoldFind,int64_t(INT32_MIN),int64_t(INT32_MAX)));
    if(request.environment) pending.magicFind=pending.goldFind=0;
    if(request.questClaimant) pending.character.player=*request.questClaimant;
    if(request.source.sourceSeed) {
        const auto *monster=ports_.monsters.find(request.source.source);
        if(!monster) return {DomainStatus::Stale,{}};
        pending.seed=request.source.random.value_or(monster->combatRandom);
        pending.monsterSeed=pending.seed;
    } else if(const auto object=ports_.objects.read().objects.find(request.source.source);object!=ports_.objects.read().objects.end()) {
        pending.seed=object->second.random;
        pending.objectSeed=pending.seed;
    } else pending.seed=request.source.random.value_or(0);
    // PLAYER_GetPlayerCount includes entered dead players; only the same-level
    // party bonus uses living members (Items::ITEMS_DropItemByMonster).
    const unsigned count=std::clamp(std::max(ports_.settings.virtualPlayers,unsigned(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[](const auto &entry){return entry.second.entered;}))),1u,8u);
    unsigned members=1;
    if(player && ports_.social.partyId(player->player)!=UINT16_MAX)
        members=std::max(1u,unsigned(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[&](const auto &entry){
            return entry.second.entered && entry.second.area==request.source.region && entry.second.persistent.player.hp>0 && ports_.social.sameParty(player->player,entry.first);
        })));
    pending.effectivePlayers=std::min(std::clamp((count-members)/2+members,1u,8u),request.source.monsterPlayerCount);
    pending.character.inventory.items.clear();
    pending.order=state_.nextOrder;
    std::map<EntityId,Preparation> candidate;candidate.emplace(request.source.source,std::move(pending));
    if(payment) {
        auto plan=ports_.transactions.prepare(std::move(*payment));if(!plan) return {plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    }
    state_.pending.merge(candidate);
    ++state_.nextOrder;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::install(EntityId source, items::PreparedBatch batch, std::string deferred,
    std::optional<uint64_t> sourceRandom,std::optional<uint64_t> objectRandom) {
    const auto found = state_.pending.find(source);
    if (found == state_.pending.end()) return {DomainStatus::Stale, {}};
    std::set<size_t> uniques = state_.uniques;
    uniques.insert(batch.limitedUniques.begin(), batch.limitedUniques.end());
    const auto &request = found->second.request;
    if (!deferred.empty()) {
        if(found->second.prepared) {
            state_.deferred=std::move(deferred);
            return {DomainStatus::Conflict,{}};
        }
        if(request.object && request.object->reportCompletion) state_.completed.insert_or_assign(source, false);
        state_.deferred = std::move(deferred); state_.pending.erase(found);
        return {DomainStatus::Applied, std::monostate{}};
    }
    if(!found->second.prepared) {
        if(found->second.roomSeed && !ports_.areas.validateRoomRandom(*found->second.roomSeed)) return {DomainStatus::Stale,{}};
        if(found->second.monsterSeed) if(const auto *monster=ports_.monsters.find(source))
            if(monster->combatRandom!=*found->second.monsterSeed) return {DomainStatus::Conflict,{}};
        if(!found->second.roomSeed && found->second.objectSeed) {
            const auto object=ports_.objects.read().objects.find(source);
            if(object!=ports_.objects.read().objects.end() && object->second.random!=*found->second.objectSeed) return {DomainStatus::Conflict,{}};
        }
        found->second.prepared=std::move(batch);
        state_.uniques.swap(uniques);
        if(found->second.roomSeed && sourceRandom) {
            auto loan=*found->second.roomSeed;loan.next=*sourceRandom;ports_.areas.publishRoomRandom(loan);
        }
        if(sourceRandom && found->second.monsterSeed) {
            const auto *monster=ports_.monsters.find(source);
            if(monster && monster->combatRandom==*found->second.monsterSeed)
                ports_.monsters.commitCombatRandom(source,*sourceRandom);
        }
        if(!found->second.roomSeed && found->second.objectSeed && (objectRandom || sourceRandom))
            ports_.objects.commitLootRandom(source,*found->second.objectSeed,objectRandom?*objectRandom:*sourceRandom);
    }
    batch=*found->second.prepared;
    const bool ownerless=request.environment && !request.beneficiary.value;
    if(ownerless && request.object) {
        const auto *area=ports_.areas.find(request.source.region);
        if(!area || !batch.equipment) return {DomainStatus::InvalidRequest,{}};
        const auto global=request.position+area->definition.origin;
        const auto room=ports_.areas.roomAtPosition(request.source.region,request.position,int(global.x),int(global.y));
        if(!room) return {DomainStatus::Stale,{}};
        const auto sample=ports_.spatial.nativeCollisionSample();
        std::vector<std::pair<items::PreparedBatch,GroundLocation>> placed;
        const auto collision=[&](RoomView owner,int32_t column,int32_t row) {
            auto mask=sample(owner,column,row);
            for(const auto &entry:placed) {
                const auto point=entry.second.position+ports_.areas.at(entry.second.region).definition.origin;
                if(owner.area==entry.second.region && column==int(point.x) && row==int(point.y)) mask|=0x200;
            }
            return mask;
        };
        for(auto &item:batch.items) {
            const auto position=ports_.areas.nativeFreeSpace(*room,int(global.x),int(global.y),1,collision);
            if(!position) continue;
            items::PreparedBatch single;single.equipment=batch.equipment;
            single.items.push_back(std::move(item));
            placed.emplace_back(std::move(single),GroundLocation{position->room.area,position->position});
        }
        auto completions=state_.completed;
        if(request.object->reportCompletion) completions.insert_or_assign(source,true);
        const auto installed=ports_.items.installNativeGroundBatch(placed,request.occurrence);
        if(!installed) return installed;
        state_.completed.swap(completions);state_.pending.erase(found);state_.deferred.clear();
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(request.object && !ownerless) {
        const auto *player=ports_.players.find(request.beneficiary);
        if(!player || !player->entered || player->area!=request.source.region || player->persistent.player.hp<=0) {
            if(request.object->reportCompletion) state_.completed.insert_or_assign(source,false);
            state_.pending.erase(found);return {DomainStatus::Applied,std::monostate{}};
        }
    }
    const size_t facts=batch.items.size()+(request.object?2:0);
    // Native NoDrop is a completed empty result, not an outbox reservation.
    if (facts && !ports_.events.hasCapacity(facts, request.object ? 2 : 1))
        return {DomainStatus::Capacity, {}};
    // Allocate follow-up bookkeeping before any authority is committed.
    auto completions = state_.completed;
    if (request.object && request.object->reportCompletion) completions.insert_or_assign(source, true);
    auto next = ports_.items.prepare(std::move(batch), {request.source.region, request.position});
    if (!next) return {next.status, {}};
    std::vector<DomainFact> drops;
    for(const auto &[id,item]:next.value->world.items)
        if(!ports_.items.read().world.items.contains(id)) drops.emplace_back(GroundDropFact{item});
    if (request.object && !ownerless) {
        const auto *player = ports_.players.find(request.beneficiary);
        const auto fail = [&]() { if(request.object->reportCompletion) state_.completed.insert_or_assign(source, false); state_.pending.erase(found); return DomainResult<>{DomainStatus::Applied, std::monostate{}}; };
        if (!player || !player->entered || player->area != request.source.region || player->persistent.player.hp <= 0) return fail();
        inventory::detail::Draft draft(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
        const auto key = request.object->key;
        if (key.id) {
            const auto *item = draft.resolve(key);
            const auto *at = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
            if (!at || at->container != player->persistent.containers.backpack || !item->quantity || item->revision == UINT64_MAX) return fail();
            auto &value = draft.edit.inventory.items.at(key.id); ++value.revision;
            if (--value.quantity) draft.edit.changes.push_back({key.id, value.revision, ItemChangeKind::QuantityChanged, value.location, value.location, value.quantity});
            else { draft.edit.changes.push_back({key.id, value.revision, ItemChangeKind::Removed, value.location, {}, 0}); draft.edit.inventory.items.erase(key.id); }
        }
        const auto ingredient=request.object->ingredient;
        if(ingredient.id) {
            const auto *item=draft.resolve(ingredient);const auto *at=item?std::get_if<ContainerLocation>(&item->location):nullptr;
            if(!at || at->container!=player->persistent.containers.backpack || item->revision==UINT64_MAX) return fail();
            const auto *definition=player->rules.items->find(item->definition);
            if(!definition || definition->betterGem!=request.object->dropCode) return fail();
            draft.edit.changes.push_back({item->id,item->revision+1,ItemChangeKind::Removed,item->location,{},0});draft.edit.inventory.items.erase(item->id);
        }
        const ActorContext actor{player->player, player->actor, player->area, 0, 0, 0};
        transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision, std::move(draft.edit.inventory), std::move(draft.edit.changes), player->persistent.player.weaponSet};
        edit.world = transactions::WorldEdit{ports_.items.read().revision, std::move(*next.value)};
        edit.publicFacts=std::move(drops);
        auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return {plan.status, {}};
        const auto committed = ports_.transactions.commit(std::move(*plan.value)); if (!committed) return committed;
        state_.completed.swap(completions);
    } else {
        if(!drops.empty()) {
            auto sent=ports_.events.publish({0,0,{}, {AudienceKind::Area,{},request.source.region},std::move(drops)});
            if(!sent) return {sent.status,{}};
        }
        // The prepared state already contains final identities and placement.
        ports_.items.commit(std::move(*next.value));
        state_.completed.swap(completions);
    }
    state_.deferred = std::move(deferred); state_.pending.erase(found);
    // GameInstance snapshots current unique availability at each preparation.
    return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::step(TickContext, FrameFacts &) { return StepStatus::Complete; }
}

namespace d2x::server::loot {
std::optional<bool> System::takeCompletion(EntityId source) {
    const auto found = state_.completed.find(source); if (found == state_.completed.end()) return {};
    const bool result = found->second; state_.completed.erase(found); return result;
}
}
