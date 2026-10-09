#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <algorithm>
#include "gameplay/skills/behavior.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "gameplay/quest/catalog.hpp"
namespace d2x::server::inventory {
namespace {
DomainStatus questPickup(const PlayerState &player, const ItemDefinition &base,
    const ItemCatalog &catalog, unsigned difficulty) {
    if (!base.questTag) return DomainStatus::Applied;
    // ItemMode::sub_6FC425F0: the native quest tag is not a blanket pickup ban.
    const auto &actTwo=player.rules.character->actTwo;
    const auto &later=player.rules.character->laterQuests;
    const bool laterItem=base.code==later.figurine || base.code==later.bird || base.code==later.lifePotion || base.code==later.gidbinn || base.code==later.tome || base.code==later.soulstone || base.code==later.hammer || base.code==later.defrostPotion || base.code==later.resistanceScroll || base.code==later.khalimWill || std::find(later.khalimParts.begin(),later.khalimParts.end(),base.code)!=later.khalimParts.end();
    if (!base.opensCube && base.code != "bks" && base.code != "bkd" &&
        base.code != "hdm" && base.code != "leg" && !actTwo.artifact(base.code) && base.code!=actTwo.book && !laterItem) return DomainStatus::Unavailable;
    const auto &quests = player.persistent.player.quests.at(difficulty);
    const auto &bird=quests.at(questIndex(QuestId::GoldenBird));
    if(base.code==later.figurine && bird.stage>=6) return DomainStatus::Conflict;
    if(base.code==later.lifePotion && !(bird.flags&goldenBirdPotionPending)) return DomainStatus::Conflict;
    if(base.code==later.gidbinn && quests.at(questIndex(QuestId::BladeOfTheOldReligion)).stage>=4) return DomainStatus::Conflict;
    const auto &ice=quests.at(questIndex(QuestId::PrisonOfIce));
    if(base.code==later.resistanceScroll && (!(ice.flags&iceScrollGranted) || ice.flags&iceScrollUsed)) return DomainStatus::Conflict;
    // ItemMode's original cross-quest tag guards. Tags stay the raw MPQ value;
    // do not infer another tag from a journal slot or a host-only stage.
    constexpr std::array guards{
        std::pair{QuestId::KhalimsWill,QuestId::LamEsensTome},
        std::pair{QuestId::BladeOfTheOldReligion,QuestId::KhalimsWill},
        std::pair{QuestId::HellsForge,QuestId::FallenAngel}};
    if(laterItem) for(const auto &[completed,tag]:guards)
        if(base.questTag==int(questDefinition(tag).nativeSlot) && quests.at(questIndex(completed)).stage>=questCompletionStage(completed)) return DomainStatus::Conflict;
    if(!base.opensCube && actTwo.artifact(base.code) && quests.at(questIndex(QuestId::HoradricStaff)).stage>=uint32_t(StaffStage::Submitted)) return DomainStatus::Conflict;
    if(base.code==actTwo.book && (quests.at(questIndex(QuestId::RadamentsLair)).flags&radamentBookUsed)) return DomainStatus::Conflict;
    if ((base.code == "bks" || base.code == "bkd") &&
        quests.at(questIndex(QuestId::SearchForCain)).stage >= uint32_t(CainStage::Rewarded)) return DomainStatus::Conflict;
    if (base.code == "hdm" &&
        quests.at(questIndex(QuestId::ToolsOfTheTrade)).stage >= uint32_t(ToolsStage::Imbued)) return DomainStatus::Conflict;
    // sub_6FC428F0 excludes the stash (InvPage 1), but checks carried items and
    // corpse inventories. The MPQ-backed carry pairs include bark/translation.
    for (const auto &[id, item] : player.persistent.inventory.items) {
        (void)id;
        const auto *at = std::get_if<ContainerLocation>(&item.location);
        if (!at || player.persistent.inventory.containers.at(at->container).spec.kind == ContainerKind::Stash) continue;
        const auto *other = catalog.find(item.definition);
        if (other && other->questTag == base.questTag && (other->code == base.code ||
            std::find(base.questCarryConflicts.begin(), base.questCarryConflicts.end(), other->code) != base.questCarryConflicts.end())) return DomainStatus::Conflict;
    }
    return DomainStatus::Applied;
}
}
DomainResult<> System::dropGold(const ActorContext &actor, unsigned amount) {
    const auto &player = *ports_.players.find(actor.player);
    if (player.persistent.player.hp <= 0 || !amount || amount > player.persistent.player.gold) return {DomainStatus::InvalidRequest, {}};
    auto world = ports_.items.read();
    const ItemDefinition *definition = nullptr;
    for (const auto &[code, item] : ports_.definitions->entries()) { (void)code; if (item.equipment.isType("gold")) { definition = &item; break; } }
    if (!definition || !definition->maxStack) return {DomainStatus::Unavailable, {}};
    std::vector<DomainFact> drops;
    unsigned remaining = amount;
    while (remaining) {
        if (world.world.items.size() >= 4096 || world.revision == UINT64_MAX) return {DomainStatus::Capacity, {}};
        const auto position = ports_.items.placement({actor.area, player.position}, world.world);
        if (!position) return {DomainStatus::Conflict, {}};
        if(!ports_.items.identityCapacity(1)) return {DomainStatus::Capacity,{}};
        ItemInstance item; item.id = ports_.items.reserveIdentity(); item.definition = definition->code;
        item.quantity = std::min(remaining, definition->maxStack); item.location = GroundLocation{actor.area, *position};
        item.nativeSeed = uint32_t(item.id.value);
        EquipmentValues values; values.levels.resize(player.rules.character->experience.size());
        drops.emplace_back(GroundDropFact{item});
        remaining -= item.quantity; world.equipment.items.emplace(item.id, std::move(values)); world.world.items.emplace(item.id, std::move(item));
    }
    auto record = player.persistent.player; record.gold -= amount;
    transactions::InventoryEdit edit{actor, player.inventoryRevision, player.characterRevision, player.persistent.inventory, {}, record.weaponSet};
    edit.character = std::move(record); edit.world = transactions::WorldEdit{world.revision, std::move(world)};
    edit.publicFacts = std::move(drops);
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
DomainResult<> System::ground(const ActorContext &actor, const GroundTransfer &request, std::optional<SkillCastSpec> telekinesis) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    const auto resolved = ports_.items.resolve({request.drop ? std::optional<PlayerId>{actor.player} : std::nullopt, request.item});
    if (!resolved) return {resolved.status, {}};
    const auto &source = *resolved.value;
    if (source.revision == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto *definition = ports_.definitions->find(source.definition);
    if (!definition) return {DomainStatus::Unavailable, {}};
    if (!request.drop) {
        const auto status = questPickup(*player, *definition, *ports_.definitions, unsigned(player->persistent.difficulty));
        if (status != DomainStatus::Applied) return {status, {}};
    }
    auto world = ports_.items.read();
    if (world.revision == UINT64_MAX || (request.drop && world.world.items.size() >= 4096)) return {DomainStatus::Capacity, {}};
    auto equipment = std::make_shared<EquipmentRules>(*player->rules.equipment);
    auto record = player->persistent.player;
    if(telekinesis) {
        if(request.drop || request.cursor || telekinesis->effect!=SkillBehavior::Telekinesis || record.mana<telekinesis->manaCost) return {DomainStatus::InvalidRequest,{}};
        const auto &type=definition->equipment;
        if(!type.isType("scro") && !type.isType("gold") && !type.isType("tpot") && !type.isType("misl") && !type.isType("poti") && !type.isType("key")) {
            const auto *position=std::get_if<GroundLocation>(&source.location);
            if(!position || position->region!=actor.area) return {DomainStatus::InvalidRequest,{}};
            const int x=int(position->position.x)-int(player->position.x),y=int(position->position.y)-int(player->position.y);
            if(x*x+y*y>telekinesis->telekinesisRange*telekinesis->telekinesisRange) return {DomainStatus::InvalidRequest,{}};
            transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,record};
            edit.player.mana-=telekinesis->manaCost;edit.charge=telekinesis->charge;edit.publicFacts.emplace_back(SoundFact{actor.actor,0,actor.area,0x13});
            auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
        }
        record.mana-=telekinesis->manaCost;
    }
    const auto cursor = ContainerLocation{player->persistent.containers.cursor, {}};
    detail::Draft draft(*player, *ports_.definitions, *equipment, *player->rules.character);
    if (request.drop) {
        if (source.location != ItemLocation{cursor}) return {DomainStatus::InvalidRequest, {}};
        const auto position = ports_.items.placement({player->area, player->position}, world.world);
        if (!position) return {DomainStatus::Conflict, {}};
        auto dropped = source; ++dropped.revision; dropped.location = GroundLocation{player->area, *position};
        world.world.items.emplace(source.id, std::move(dropped));
        world.equipment.items.insert_or_assign(source.id, equipment->items.at(source.id));
        world.equipment.includeSets(*equipment);
        equipment->items.erase(source.id);
        draft.edit.inventory.items.erase(source.id);
        draft.edit.changes.push_back({source.id, source.revision + 1, ItemChangeKind::Removed, source.location, {}, 0});
    } else {
        const auto *position = std::get_if<GroundLocation>(&source.location);
        if (!position || position->region != player->area || (position->position - player->position).length() > 50.f || draft.at(cursor))
            return {DomainStatus::InvalidRequest, {}};
        const int dx=int(position->position.x)-int(player->position.x),dy=int(position->position.y)-int(player->position.y);
        if(telekinesis && dx*dx+dy*dy>telekinesis->telekinesisRange*telekinesis->telekinesisRange) return {DomainStatus::InvalidRequest,{}};
        if (!telekinesis && (position->position - player->position).length() > 1.8f) {
            const auto status = ports_.movement.execute(actor, {MovementAction::Move, position->position, player->running});
            if (status != CommandStatus::Applied) return {DomainStatus::Conflict, {}};
            state_.pickups.insert_or_assign(actor.player, Pickup{actor, request, player->locomotionSequence,ports_.items.groundGeneration(request.item.id)});
            return {DomainStatus::Applied, std::monostate{}};
        }
        if (!telekinesis && !ports_.items.reachable(*position, player->position)) return {DomainStatus::InvalidRequest, {}};
        if (definition->equipment.isType("gold")) {
            const auto capacity = unsigned(record.level) * 10000;
            const auto amount = std::min(source.quantity, capacity > record.gold ? capacity - record.gold : 0);
            if (!amount) return {DomainStatus::Conflict, {}};
            record.gold += amount;
            world.world.items.erase(source.id);
            if (amount != source.quantity) {
                if(!ports_.items.identityCapacity(1)) return {DomainStatus::Capacity,{}};
                auto left = source; left.id = ports_.items.reserveIdentity(); left.quantity -= amount; left.revision = 1;
                world.equipment.items.emplace(left.id, world.equipment.items.at(source.id));
                world.world.items.emplace(left.id, std::move(left));
            }
        } else {
            if(definition->opensCube) for(const auto &[id,owned]:player->persistent.inventory.items) {
                (void)id;const auto *at=std::get_if<ContainerLocation>(&owned.location);
                if(owned.definition==source.definition && at && player->persistent.inventory.containers.at(at->container).spec.kind!=ContainerKind::Corpse) return {DomainStatus::Conflict,{}};
            }
            if (world.equipment.items.at(source.id).singleCarry)
                for (const auto &[id, existing] : player->persistent.inventory.items) {
                    (void)id;
                    const auto *at = std::get_if<ContainerLocation>(&existing.location);
                    if (at && player->persistent.inventory.containers.at(at->container).spec.kind != ContainerKind::Corpse &&
                        existing.quality == ItemQuality::Unique && existing.specialRow == source.specialRow) return {DomainStatus::Conflict, {}};
                }
            equipment->items.insert_or_assign(source.id, world.equipment.items.at(source.id));
            equipment->includeSets(world.equipment);
            auto copy = source; copy.location = cursor;
            draft.edit.inventory.items.emplace(copy.id, copy);
            if (!request.cursor) {
                const auto merged = draft.mergeCarried(source.id);
                if (merged != DomainStatus::Applied) return {merged, {}};
            }
            bool equipped = false;
            if (!request.cursor && draft.edit.inventory.items.contains(source.id)) {
                const auto result = draft.autoEquip(source.id);
                if (!result) return {result.status,{}};
                equipped = *result.value;
            }
            std::erase_if(draft.edit.changes, [&](const auto &change) { return change.item == source.id; });
            auto current = draft.edit.inventory.items.find(source.id);
            if (current == draft.edit.inventory.items.end()) world.world.items.erase(source.id);
            else {
                std::optional<ContainerLocation> destination;
                if (request.cursor) destination = cursor;
                else if (equipped) destination = std::get<ContainerLocation>(current->second.location);
                else {
                    if (definition->autoBelt) destination = draft.space(source.id, player->persistent.containers.belt);
                    if (!destination) destination = draft.space(source.id, player->persistent.containers.backpack);
                }
                auto &item = current->second;
                if (destination) {
                    item.location = *destination; if (!equipped) ++item.revision;
                    draft.edit.changes.push_back({item.id, item.revision, ItemChangeKind::Created, {}, item.location, item.quantity});
                    world.world.items.erase(source.id);
                } else {
                    if (item.quantity == source.quantity && item.charges == source.charges) return {DomainStatus::Conflict, {}};
                    auto &left = world.world.items.at(source.id); left.quantity = item.quantity; left.charges = item.charges; ++left.revision;
                    draft.edit.inventory.items.erase(current);
                    equipment->items.erase(source.id);
                }
            }
            if (!draft.edit.inventory.items.contains(source.id)) equipment->items.erase(source.id);
        }
        if (!world.world.items.contains(source.id)) world.equipment.items.erase(source.id);
    }
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
    edit.equipment = std::move(equipment); edit.character = std::move(record);if(telekinesis) edit.charge=telekinesis->charge;
    if (request.drop) edit.publicFacts.emplace_back(GroundDropFact{world.world.items.at(source.id)});
    edit.world = transactions::WorldEdit{world.revision, std::move(world)};
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}

namespace d2x::server::inventory {
StepStatus System::step(TickContext tick, FrameFacts &) {
    std::erase_if(state_.storage,[&](const auto &entry){return !storageAccess(entry.first) && !cubeAccess(entry.first);});
    for (auto it = state_.pickups.begin(); it != state_.pickups.end();) {
        auto pending = it->second;
        const auto *player = ports_.players.find(it->first);
        const auto *area = ports_.areas.find(pending.actor.area);
        const auto item = ports_.items.read().world.items.find(pending.request.item.id);
        if (!player || !player->entered || player->actor != pending.actor.actor || player->area != pending.actor.area ||
            !area || area->generation != pending.actor.areaGeneration || player->persistent.player.hp <= 0 || player->locomotionSequence != pending.locomotion ||
            item==ports_.items.read().world.items.end() || !pending.groundGeneration ||
            ports_.items.groundGeneration(pending.request.item.id)!=pending.groundGeneration) { it = state_.pickups.erase(it); continue; }
        const auto &at = std::get<GroundLocation>(item->second.location);
        if(at.region!=pending.actor.area) {it=state_.pickups.erase(it);continue;}
        if ((at.position - player->position).length() > 1.8f && !player->route.empty()) { ++it; continue; }
        if ((at.position - player->position).length() <= 1.8f) {
            pending.actor.tick = tick.tick;
            pending.request.item=item->second.handle();
            const auto result = ground(pending.actor, pending.request);
            if (result.status == DomainStatus::Capacity) { ++it; continue; }
            if (result) ports_.movement.execute(pending.actor, {MovementAction::Stop, {}, false});
        }
        it = state_.pickups.erase(it);
    }
    return replenish(tick);
}
}

namespace d2x::server::inventory::detail {
DomainResult<transactions::WorldEdit> prepareSpill(const PlayerState &player, const Edit &edit,
                                                  EquipmentRules &rules, items::System &items) {
    auto world = items.read();
    if (world.revision == UINT64_MAX || world.world.items.size() > 4096 || edit.spilled.size() > 4096 - world.world.items.size())
        return {DomainStatus::Capacity, {}};
    for (auto item : edit.spilled) {
        const auto position = items.placement({player.area, player.position}, world.world);
        if (!position || world.world.items.contains(item.id)) return {DomainStatus::Conflict, {}};
        item.location = GroundLocation{player.area, *position};
        world.equipment.items.insert_or_assign(item.id, rules.items.at(item.id));
        world.world.items.emplace(item.id, std::move(item));
        rules.items.erase(item.id);
    }
    world.equipment.includeSets(rules);
    return {DomainStatus::Applied, transactions::WorldEdit{world.revision, std::move(world)}};
}
}

namespace d2x::server::inventory {
DomainResult<> System::telekinesis(const ActorContext &actor, EntityId id, const SkillCastSpec &skill) {
    const auto found=ports_.items.read().world.items.find(id);if(found==ports_.items.read().world.items.end()) return {DomainStatus::Stale,{}};
    return ground(actor,{found->second.handle(),false,false},skill);
}
std::optional<Vec> System::groundPosition(EntityId id, RegionId area) const {
    const auto found=ports_.items.read().world.items.find(id);if(found==ports_.items.read().world.items.end()) return {};
    const auto *at=std::get_if<GroundLocation>(&found->second.location);if(!at || at->region!=area) return {};return at->position;
}
}
