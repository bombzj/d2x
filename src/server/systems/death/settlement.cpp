#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <string_view>
namespace d2x::server::death {
DomainResult<unsigned> System::dropGold(const ActorContext &actor,unsigned amount,bool corpse,const InventoryState *ground) {
    if(!amount) return {DomainStatus::Applied,0u};
    const auto *player=ports_.players.find(actor.player);if(!player) return {DomainStatus::InvalidActor,{}};
    auto &transition=state_.transitions.at(actor.actor);
    auto &slot=corpse?transition.corpseGold:transition.penaltyGold;
    if(!slot) {slot=Transition::GoldDrop{};slot->amount=amount;}
    auto &drop=*slot;
    if(drop.amount!=amount) return {DomainStatus::Stale,{}};
    const auto remaining=[&] {return corpse || drop.created?0u:amount;};
    if(!drop.selected) {
        if((!corpse && uint64_t(amount)>uint64_t(player->persistent.player.level)*10000) ||
            (corpse && amount>unsigned(INT32_MAX))) {
            drop.selected=drop.finished=true;return {DomainStatus::Applied,remaining()};
        }
        const auto choice=ports_.items.selectPlayerGold(actor.area,player->position);
        if(!choice) return {choice.status,{}};
        drop.code=choice.value->code;drop.level=choice.value->level;
        drop.location=ports_.items.placement({actor.area,player->position},ground?*ground:ports_.items.read().world);
        drop.selected=true;if(!drop.location) drop.finished=true;
    }
    if(drop.finished) return {DomainStatus::Applied,remaining()};
    if(drop.queued) {
        const auto completion=ports_.loot.takeCompletionDetail(actor.actor);
        if(!completion) return {DomainStatus::PendingContent,{}};
        drop.finished=true;drop.created=completion->accepted && completion->created!=0;
        return {DomainStatus::Applied,remaining()};
    }
    loot::Request request;request.occurrence=transition.occurrence;
    request.source.source=actor.actor;request.source.region=drop.location->region;
    request.source.difficulty=ports_.settings.difficulty;request.position=drop.location->position;
    request.environment=request.reportCompletion=true;
    loot::NativeDrop native;native.code=drop.code;native.level=int(drop.level);native.exactPosition=true;
    native.unforcedCreation=true;native.quantityOverride=corpse?amount:std::min(amount,2000000000u);native.goldOwner=actor.player;native.flip=false;
    request.nativeDrops.push_back(std::move(native));
    const auto queued=ports_.loot.queue(request);if(!queued) return {queued.status,{}};
    drop.queued=true;return {DomainStatus::PendingContent,{}};
}
DomainResult<> System::begin(const ActorContext &actor) {
    const auto *player=ports_.players.find(actor.player);
    const auto transition=state_.transitions.find(actor.actor);
    if(!player || player->actor!=actor.actor || player->persistent.player.hp>0 ||
        !player->rules.character || transition==state_.transitions.end()) return {DomainStatus::InvalidActor,{}};
    auto record=player->persistent.player;
    if(record.hardcore) record.hardcoreDead=true;
    const auto &message=transition->second.message;
    const bool pvp=!message.other.empty();
    auto world=ports_.items.read();
    if(world.revision==UINT64_MAX || !ports_.transactions.hasOutputCapacity()) return {DomainStatus::Capacity,{}};
    const uint64_t total=uint64_t(record.gold)+record.bankGold;
    auto penalty=total*unsigned(std::min(record.level,20))/100;
    if(ports_.settings.singlePlayer) {
        const auto protectedGold=uint64_t(record.level)*500;
        penalty=std::min<uint64_t>(record.gold,std::min(penalty,total>protectedGold?total-protectedGold:0));
    }
    std::vector<DomainFact> drops;
    std::optional<Transition::EarDrop> ear;
    if(pvp) {
        // Fixed engine class identities in the ear payload, not CharStats row
        // indices (which may include the Expansion separator).
        constexpr std::array<std::string_view,7> classes{"ama","sor","nec","pal","bar","dru","ass"};
        const auto nativeClass=std::find(classes.begin(),classes.end(),player->definition.code);
        if(nativeClass==classes.end()) return {DomainStatus::InvalidRequest,{}};
        if(penalty>record.gold) {record.bankGold-=unsigned(penalty-record.gold);record.gold=unsigned(penalty);}
        const auto gold=dropGold(actor,unsigned(penalty),false);if(!gold) return {gold.status,{}};
        record.gold-=unsigned(penalty)-*gold.value;
        world=ports_.items.read();
        // ITEMS_DropPlayerEar uses the prepared player class and the actual
        // victim identity, independent of the killer's continued connection.
        for(const auto &[code,definition]:player->rules.items->entries()) {
            // ITEMS_DropPlayerEar resolves the exact 'ear' code. Other
            // player-body-part records are not substitutes in modified MPQs.
            if(code!="ear" || !definition.equipment.isType("play")) continue;
            const auto position=ports_.items.placement({actor.area,player->position},world.world);
            if(!position || world.world.items.size()>=4096) break;
            ear=Transition::EarDrop{*position,{unsigned(nativeClass-classes.begin()),unsigned(record.level),record.name},record.hardcore};
            break;
        }
    } else if(penalty>record.gold) {
        record.bankGold-=unsigned(penalty-record.gold);record.gold=0;
    } else record.gold-=unsigned(penalty);
    if(!pvp) {
        // ApplyDeathPenalty drops the post-penalty PvE wallet at DT start;
        // only PvP leaves a carried remainder for the later corpse stage.
        const auto gold=dropGold(actor,record.gold,false);if(!gold) return {gold.status,{}};
        record.gold=*gold.value;world=ports_.items.read();
    }
    uint64_t recoverableExperience=0;
    const auto &thresholds=player->rules.character->experience;
    if(!pvp && record.level>1 && size_t(record.level+1)<thresholds.size()) {
        const auto floor=thresholds[size_t(record.level)];
        if(record.experience<floor) return {DomainStatus::InvalidRequest,{}};
        const auto loss=std::min(record.experience-floor,(thresholds[size_t(record.level+1)]-floor)*unsigned(player->rules.character->deathExperiencePenalty)/100);
        record.experience-=loss;recoverableExperience=loss*75/100;
    }
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,
        player->persistent.inventory,{},record.weaponSet};
    edit.character=std::move(record);edit.publicFacts=std::move(drops);
    edit.facts.emplace_back(DeathFact{transition->second.occurrence,actor.actor,{},actor.area,uint32_t(penalty)});
    if(pvp) for(const auto &[id,recipient]:ports_.players.all()) if(recipient.entered)
        edit.directed.push_back({id,{PlayerMessageFact{6,player->persistent.player.name,message.other,0,0,message.characterClass}}});
    edit.world=transactions::WorldEdit{world.revision,std::move(world)};
    auto plan=ports_.transactions.prepare(std::move(edit));
    if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {
        transition->second.recoverableExperience=recoverableExperience;
        transition->second.ear=std::move(ear);
        transition->second.penaltyGold.reset();
    }
    return result;
}
DomainResult<> System::dropEar(const ActorContext &actor) {
    auto &transition=state_.transitions.at(actor.actor);
    if(!transition.ear) return {DomainStatus::Applied,std::monostate{}};
    auto &ear=*transition.ear;
    if(ear.queued) {
        if(!ports_.loot.takeCompletionDetail(actor.actor)) return {DomainStatus::PendingContent,{}};
        transition.ear.reset();return {DomainStatus::Applied,std::monostate{}};
    }
    loot::Request request;request.occurrence=transition.occurrence;
    request.source.source=actor.actor;request.source.region=ear.location.region;
    request.source.difficulty=ports_.settings.difficulty;
    request.position=ear.location.position;request.environment=request.reportCompletion=true;
    loot::NativeDrop drop;drop.code="ear";drop.level=int(ear.identity.level);drop.quality=ItemQuality::Normal;
    drop.exactPosition=true;drop.ear=ear.identity;drop.namedEar=ear.named;drop.flip=false;
    request.nativeDrops.push_back(std::move(drop));
    const auto queued=ports_.loot.queue(request);if(!queued) return queued;
    ear.queued=true;return {DomainStatus::PendingContent,{}};
}
DomainResult<> System::settle(const ActorContext &actor) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || player->persistent.player.hp > 0 || !player->rules.character) return {DomainStatus::InvalidActor, {}};
    inventory::detail::Draft draft(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
    auto corpses = player->persistent.corpses;
    auto world = ports_.items.read();
    if (world.revision == UINT64_MAX || !ports_.transactions.hasOutputCapacity()) return {DomainStatus::Capacity, {}};
    auto equipment = std::make_shared<EquipmentRules>(*player->rules.equipment);
    std::vector<DomainFact> drops;
    PlayerCorpse corpse; corpse.owner = actor.actor; corpse.region = actor.area; corpse.position = player->position; corpse.look = player->look;
    const auto transition=state_.transitions.find(actor.actor);
    if(transition==state_.transitions.end() || !transition->second.penaltySettled || actor.tick<transition->second.ready)
        return {DomainStatus::Conflict,{}};
    corpse.recoverableExperience=transition->second.recoverableExperience;
    if (corpses.size() < 16) {
        if(!transition->second.corpseId) {
            if (!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity, {}};
            transition->second.corpseId=ports_.items.reserveIdentity(NativeUnitType::Player);
            transition->second.corpseItems=ports_.items.reserveIdentity(std::nullopt);
        }
        corpse.id=transition->second.corpseId;corpse.items=transition->second.corpseItems;
        draft.edit.inventory.containers.emplace(corpse.items, ContainerState{corpse.items, {actor.actor, ContainerKind::Corpse, int(EquipmentSlot::Count) + 1, 1}});
    }
    const auto drop = [&](EntityId id) -> bool {
        auto &item = draft.edit.inventory.items.at(id);
        const auto position = ports_.items.placement({actor.area, player->position}, world.world);
        if (!position || item.revision == UINT64_MAX || world.world.items.size() >= 4096) return false;
        draft.edit.changes.push_back({id, item.revision + 1, ItemChangeKind::Removed, item.location, {}, 0});
        item.location = *position; ++item.revision;
        drops.emplace_back(GroundDropFact{item});
        world.world.items.emplace(id, item); world.equipment.items.insert_or_assign(id, equipment->items.at(id));
        equipment->items.erase(id); draft.edit.inventory.items.erase(id); return true;
    };
    for (int slot = 0; slot <= int(EquipmentSlot::Count); ++slot) {
        const auto id = slot == int(EquipmentSlot::Count) ? draft.at({draft.containers().cursor, {}}) : draft.equipped(EquipmentSlot(slot));
        if (!id) continue;
        if (corpse.items) {
            if (const auto result = draft.move(id, {corpse.items, {slot, 0}}); result != DomainStatus::Applied) return {result, {}};
        } else if (!drop(id)) return {DomainStatus::Capacity, {}};
    }
    std::vector<EntityId> excess;
    for (const auto &[id, item] : draft.edit.inventory.items)
        if (const auto *at = std::get_if<ContainerLocation>(&item.location); at && at->container == draft.containers().belt && at->cell.y > 0) excess.push_back(id);
    for (const auto id : excess) {
        if (const auto destination = draft.space(id, draft.containers().backpack)) {
            if (const auto result = draft.move(id, *destination); result != DomainStatus::Applied) return {result, {}};
        } else if (!drop(id)) return {DomainStatus::Capacity, {}};
    }
    draft.edit.inventory.containers.at(draft.containers().belt).spec.rows = 1;
    // PLRMODE_StartXY_Dead / CORPSE_Handler drops the carried remainder
    // after the DT penalty stage, including the remainder of a PvP wallet.
    auto record=player->persistent.player;
    const auto gold=dropGold(actor,record.gold,true,&world.world);if(!gold) return {gold.status,{}};
    record.gold=*gold.value; // Retail CORPSE_Handler clears the wallet even if creation fails.
    // Native gold was published through Loot before this equipment transaction.
    // Keep any overflow equipment drops staged in this candidate as well.
    auto refreshed=ports_.items.read();
    for(const auto &fact:drops) if(const auto *drop=std::get_if<GroundDropFact>(&fact)) {
        refreshed.world.items.emplace(drop->item.id,drop->item);
        refreshed.equipment.items.emplace(drop->item.id,world.equipment.items.at(drop->item.id));
    }
    world=std::move(refreshed);
    if (corpse.id) corpses.push_back(corpse);
    world.equipment.includeSets(*equipment);
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
    edit.character = std::move(record); edit.corpses = std::move(corpses); edit.equipment = std::move(equipment);
    edit.publicFacts = std::move(drops);
    edit.world = transactions::WorldEdit{world.revision, std::move(world)};
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}
