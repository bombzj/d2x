#include "gameplay/session/session.hpp"
#include "content/monster_loot.hpp"
#include "content/monster_experience.hpp"
#include "content/item_quality.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace d2x {
void GameSession::settleDeaths() {
    // Publishing item events can reallocate the event vector; copy deaths before appending anything.
    std::vector<EnemyDied> deaths;
    for (const auto &event : events())
        if (auto death = std::get_if<EnemyDied>(&event))
            deaths.push_back(*death);
    for (const auto &death : deaths) {
        if (loot_.settled(death.victim))
            continue;
        const bool questFirstKill = death.identity.monster == "andariel" &&
            quest(ActOneQuest::SistersToTheSlaughter).stage < uint32_t(SlaughterStage::AndarielSlain) &&
            catacombsFourRegion_ && death.region == *catacombsFourRegion_;
        updateBurialQuest(death);
        updateTowerQuest(death);
        updateSlaughterQuest(death);
        LootRequest request{death.victim, death.identity, death.region, death.difficulty,
                            questFirstKill};
        request.sourceSeed = true;
        if (death.identity.origin == SpawnOrigin::Summoned) {
            LootPlan empty;
            empty.randomState = death.lootRandom;
            loot_.settle(request, std::move(empty));
            continue;
        }
        const auto entry = resolveMonsterLoot(content_, monsterContent_, worldContent_, request);
        std::cout << "Monster loot entry: id=" << death.victim.value << " monster=" << death.identity.monster
                  << " rank=" << monsterRankName(death.identity.rank);
        LootPlan plan;
        plan.randomState = death.lootRandom;
        if (entry.status == LootEntryStatus::Ready) {
            auto ratios = content_.tables.find("itemratio");
            if (ratios == content_.tables.end())
                plan.deferred = "Missing original ItemRatio table";
            else {
                std::set<size_t> usedUniques;
                for (auto row : loot_.usedUniques())
                    usedUniques.insert(size_t(row));
                plan = planItemLoot(content_, ratios->second, entry.treasureClass, entry.itemLevel,
                                    entry.upgradeLevel, plan.randomState, usedUniques,
                                    characterDefinition_.code,
                                    simulation_.characterStats_.combat.magicFind,
                                    simulation_.characterStats_.combat.goldFind);
            }
            std::cout << " TC=" << entry.treasureClass << " itemLevel=" << entry.itemLevel
                      << " upgradeLevel=" << entry.upgradeLevel << " drops=" << plan.drops.size()
                      << " NoDrop=" << plan.noDrops << " deferred=" << plan.deferred << '\n';
        } else {
            if (entry.status == LootEntryStatus::Deferred)
                plan.deferred = entry.reason;
            std::cout << (entry.status == LootEntryStatus::Empty ? " empty: " : " deferred: ")
                      << entry.reason << '\n';
        }
        if (!plan.deferred.empty())
            simulation_.emit(LootDeferred{death.victim, plan.deferred});
        auto drops = loot_.settle(request, std::move(plan));
        spawnLoot(drops, death.region, death.position);
        if (death.killer == state().player.id) {
            grantHirelingExperience(death);
            auto experience = resolveMonsterExperience(content_, monsterContent_, worldContent_,
                {death.identity, death.region, death.difficulty, state().player.level});
            if (experience.deferred.empty())
                grantExperience(experience.amount + experience.amount *
                    uint64_t(std::max(0, characterStats().combat.experiencePercent)) / 100);
            else
                std::cout << "Monster experience deferred: id=" << death.victim.value
                          << " reason=" << experience.deferred << '\n';
        }
    }
}
void GameSession::spawnLoot(std::span<const LootDrop> drops, RegionId id, Vec origin) {
    auto region = std::find_if(regions_.begin(), regions_.end(),
                               [id](const Region &r) { return r.definition.id == id; });
    if (region == regions_.end())
        throw std::logic_error("Loot references an unknown region");
    const auto &grid = region->map.grid;
    for (const auto &drop : drops) {
        Vec position = origin + drop.offset;
        // Items.cpp starts at the drop offset if that room exists. The common
        // item resolver checks drop collision and the field from the actual source.
        if (position.x < 0 || position.y < 0 || position.x >= grid.width || position.y >= grid.height)
            position = origin;
        auto result = inventory_.createItem(drop.code, drop.quantity, GroundLocation{id, position},
                                            drop.level, drop.generation, origin);
        if (result.error == InventoryError::NoSpace) {
            simulation_.emit(LootDeferred{{}, "No free ground cell for this drop."});
            continue;
        }
        if (!result)
            throw std::logic_error("Invalid loot definition or placement");
        publishInventory(std::move(result), {});
    }
}
void GameSession::cancelPickup() {
    if (pickup_.id)
        simulation_.stopWalking();
    pickup_ = {};
    pickupToCursor_ = false;
}
void GameSession::beginPickup(ItemHandle handle, bool toCursor) {
    if (pickup_.id == handle.id && pickup_.revision == handle.revision && pickupToCursor_ == toCursor)
        return;
    cancelPickup();
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        simulation_.emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    const auto &player = state().player;
    if (player.dead || cursorItem() || !ground || ground->region != region().definition.id) {
        simulation_.emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    simulation_.stopWalking();
    simulation_.execute(MoveTo{ground->position});
    if (player.route.empty() && (ground->position - player.pos).length() > 1.8f) {
        simulation_.emit(PickupFailed{handle.id, "Cannot reach that item."});
        return;
    }
    pickup_ = handle;
    pickupToCursor_ = toCursor;
}
void GameSession::updatePickup() {
    if (!pickup_.id)
        return;
    const auto &player = state().player;
    if (player.dead || cursorItem()) {
        cancelPickup();
        return;
    }
    const auto handle = pickup_;
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        cancelPickup();
        simulation_.emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    if (!ground || ground->region != region().definition.id) {
        cancelPickup();
        simulation_.emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    if (player.castTime > 0 || player.meleeTime > 0)
        return;
    auto access = inventoryAccess();
    // Pickup is deliberately closer than generic container access; walls also block the hand-off.
    access.reach = 1.8f;
    if ((ground->position - player.pos).length() <= access.reach &&
        map().grid.collisionSegment(player.pos, ground->position, 0x0801)) {
        auto definition = item->definition;
        unsigned quantity = item->quantity;
        if (inventory_.catalog().find(definition)->equipment.isType("gold")) {
            unsigned capacity = unsigned(equipmentActor().level) * 10000;
            unsigned amount = std::min(quantity, capacity - player.gold);
            if (!amount) {
                cancelPickup();
                simulation_.emit(PickupFailed{handle.id, "Gold carrying limit reached."});
                return;
            }
            auto result = inventory_.consume(handle, amount, access);
            if (result)
                simulation_.state_.player.gold += amount;
            bool collected = bool(result);
            cancelPickup();
            publishInventory(std::move(result), handle.id);
            if (collected)
                simulation_.emit(ItemPickedUp{handle.id, std::move(definition), amount});
            return;
        }
        auto result = pickupToCursor_
            ? inventory_.move(MoveItem{handle, ContainerLocation{playerContainers_.cursor, {}}}, access)
            : inventory_.collect(handle, playerContainers_, access);
        quantity = result.transferred;
        const bool remainder = inventory_.item(handle.id) &&
            std::holds_alternative<GroundLocation>(inventory_.item(handle.id)->location);
        bool collected = bool(result);
        cancelPickup();
        publishInventory(std::move(result), handle.id);
        if (collected)
            simulation_.emit(ItemPickedUp{handle.id, std::move(definition), quantity});
        if (collected && remainder)
            simulation_.emit(PickupFailed{handle.id, "Merged what fits. The remainder stays on the ground."});
    } else if (player.route.empty()) {
        cancelPickup();
        simulation_.emit(PickupFailed{handle.id, "Cannot reach that item."});
    }
}
} // namespace d2x
