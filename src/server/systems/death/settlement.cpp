#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <string_view>
namespace d2x::server::death {
namespace {
Vec deathDropOrigin(const AreaStore &areas,const PlayerState &player,RegionId area) {
    auto origin=player.position;
    const auto global=origin+areas.at(area).definition.origin;
    // 1.13c Items RVA 11D3F uses x+2/y+3; D2MOO's gold helper
    // transcription uses y for both coordinates, unlike its ear helper.
    if(areas.roomAtPosition(area,origin+Vec{.5f,.5f},int(global.x)+2,int(global.y)+3)) origin=origin+Vec{2,3};
    return origin;
}
DomainStatus dropGold(items::System &itemSystem,const AreaStore &areas,const PlayerState &player,const ActorContext &actor,
    items::State &world,unsigned &remaining,std::vector<DomainFact> &facts) {
    for(const auto &[code,definition]:player.rules.items->entries()) {
        if(!definition.equipment.isType("gold")) continue;
        while(remaining) {
            const auto position=itemSystem.placement({actor.area,deathDropOrigin(areas,player,actor.area)},world.world);
            if(!position || !definition.maxStack || world.world.items.size()>=4096) break;
            if(!itemSystem.identityCapacity(1)) return DomainStatus::Capacity;
            ItemInstance item;item.id=itemSystem.reserveIdentity();item.definition=code;
            item.quantity=std::min(remaining,definition.maxStack);item.location=*position;
            item.nativeSeed=uint32_t(item.id.value);
            if(player.rules.skills && player.rules.skills->pd2) item.nativeFormat=104;
            // Items' unforced constructor sets INSTORE even on death gold.
            item.nativeProperties=true;item.nativeFlags=0x2000;
            EquipmentValues values;values.levels.resize(player.rules.character->experience.size());
            remaining-=item.quantity;world.goldOwners.emplace(item.id,actor.player);
            world.equipment.items.emplace(item.id,std::move(values));
            world.world.items.emplace(item.id,item);
            // Player's death gold is ADDTOGROUND, not an item flip.
            facts.emplace_back(GroundDropFact{std::move(item),false});
        }
        break;
    }
    return DomainStatus::Applied;
}
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
    if(pvp) {
        // Fixed engine class identities in the ear payload, not CharStats row
        // indices (which may include the Expansion separator).
        constexpr std::array<std::string_view,7> classes{"ama","sor","nec","pal","bar","dru","ass"};
        const auto nativeClass=std::find(classes.begin(),classes.end(),player->definition.code);
        if(nativeClass==classes.end()) return {DomainStatus::InvalidRequest,{}};
        if(penalty>record.gold) {record.bankGold-=unsigned(penalty-record.gold);record.gold=unsigned(penalty);}
        unsigned remaining=unsigned(penalty);
        const auto status=dropGold(ports_.items,ports_.areas,*player,actor,world,remaining,drops);
        if(status!=DomainStatus::Applied) return {status,{}};
        record.gold-=unsigned(penalty)-remaining;
        // ITEMS_DropPlayerEar uses the prepared player class and the actual
        // victim identity, independent of the killer's continued connection.
        for(const auto &[code,definition]:player->rules.items->entries()) {
            if(!definition.equipment.isType("play")) continue;
            const auto origin=deathDropOrigin(ports_.areas,*player,actor.area);
            const auto position=ports_.items.placement({actor.area,origin},world.world);
            if(!position || world.world.items.size()>=4096) break;
            if(!ports_.items.identityCapacity(1)) return {DomainStatus::Capacity,{}};
            ItemInstance item;item.id=ports_.items.reserveIdentity();item.definition=code;item.level=unsigned(record.level);
            if(player->rules.skills && player->rules.skills->pd2) item.nativeFormat=104;
            item.nativeSeed=uint32_t(item.id.value);item.nativeProperties=true;
            item.nativeFlags=0x2000u|0x10000u|(record.hardcore?0x8000u:0);
            item.ear=ItemEarIdentity{unsigned(nativeClass-classes.begin()),unsigned(record.level),record.name};item.location=*position;
            EquipmentValues values;values.levels.resize(player->rules.character->experience.size());
            world.equipment.items.emplace(item.id,std::move(values));world.world.items.emplace(item.id,item);
            drops.emplace_back(GroundDropFact{std::move(item)});
            break;
        }
    } else if(penalty>record.gold) {
        record.bankGold-=unsigned(penalty-record.gold);record.gold=0;
    } else record.gold-=unsigned(penalty);
    if(!pvp) {
        // ApplyDeathPenalty drops the post-penalty PvE wallet at DT start;
        // only PvP leaves a carried remainder for the later corpse stage.
        unsigned remaining=record.gold;
        const auto status=dropGold(ports_.items,ports_.areas,*player,actor,world,remaining,drops);
        if(status!=DomainStatus::Applied) return {status,{}};
        record.gold=remaining;
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
    if(result) transition->second.recoverableExperience=recoverableExperience;
    return result;
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
        if (!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity, {}};
        corpse.id = ports_.items.reserveIdentity(); corpse.items = ports_.items.reserveIdentity();
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
    unsigned remaining=record.gold;
    const auto goldStatus=dropGold(ports_.items,ports_.areas,*player,actor,world,remaining,drops);
    if(goldStatus!=DomainStatus::Applied) return {goldStatus,{}};
    record.gold=remaining; // Preserve an undeliverable pile on capacity failure.
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
