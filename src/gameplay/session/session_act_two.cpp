#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "world/maze.hpp"
#include "gameplay/loot/special.hpp"
#include "content/items/object_loot.hpp"
#include "content/items/item_quality.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ItemGeneration GameSessionImpl::questItemGeneration(std::string_view code, uint64_t &random) const {
    ItemGeneration generation;
    if (code != "msf" && code != "vip" && code != "hst") return generation;
    const auto record = std::find_if(content_.uniqueItems.begin(), content_.uniqueItems.end(),
        [&](const auto &value) { return value.code == code; });
    if (record == content_.uniqueItems.end() || !record->artAvailable)
        throw std::runtime_error("Original quest unique item is unavailable: " + std::string(code));
    auto properties = rollSpecialProperties(*record, random);
    random = properties.randomState;
    generation.quality = ItemQuality::Unique;
    generation.specialRow = int32_t(record->row);
    generation.requiredLevel = record->requiredLevel;
    generation.propertyRolls = std::move(properties.values);
    return generation;
}
bool GameSessionImpl::carriesQuestItem(std::string_view code) const {
    for (const auto &[id, item] : inventory_.state().items) {
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || (location->container != playerContainers_.backpack &&
            location->container != playerContainers_.cube && location->container != playerContainers_.equipment)) continue;
        if (item.definition == code && (code == content_.cubeCode ||
            item.nativeQuestDifficulty >= unsigned(state().population.difficulty))) return true;
    }
    return false;
}
void GameSessionImpl::activateActTwoObject(EntityId id, std::optional<ItemHandle> submitted) {
    auto &objects = world_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &value) { return value.id == id; });
    if (found == objects.end() || state().player.actions.dead || !canReach(*found)) return;
    const auto operation = found->operateFn;
    if (submitted && operation != 25) return;
    auto &book = simulation_->state_.player.character.actOneQuests.at(size_t(state().population.difficulty));
    auto &staff = book.at(questIndex(QuestId::HoradricStaff));
    if (found->questDestination || operation == 34) {
        const auto destination = found->questDestination.value_or(RegionId(int(region().definition.id) == 54 ? 74 : 54));
        if (operation == 34 && int(region().definition.id) != 54 && int(region().definition.id) != 74) return;
        found->animationMode = 2;
        ensureRegion(destination);
        const auto target = std::find_if(world_.regions().begin(), world_.regions().end(), [&](const auto &value) { return value.definition.id == destination; });
        std::optional<Vec> arrival;
        if (target != world_.regions().end())
            for (const auto &portal : target->objects)
                if ((portal.questDestination && *portal.questDestination == region().definition.id) ||
                    (operation == 34 && portal.operateFn == 34)) { arrival = target->map.grid.nearest(portal.pos); break; }
        enter(destination, arrival);
        return;
    }
    if (operation == 42 && int(region().definition.id) == 74) {
        const Vec source = found->pos;
        const std::string name = found->name;
        found->operatedAt = found->operatedAt < 0 ? state().time : found->operatedAt;
        auto &arcane = book.at(questIndex(QuestId::ArcaneSanctuary));
        if (arcane.stage < 4) { arcane.stage = 4; simulation_->emit(QuestAdvanced{QuestId::ArcaneSanctuary, 4}); }
        if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q4", "Successful", "Narrator"))
            simulation_->emit(NpcDialogueStarted{id, name, speech->text});
        ensureRegion(RegionId(46));
        for (auto &area : world_.regions()) {
            if (int(area.definition.id) != 46 && int(area.definition.id) != 74) continue;
            if (std::any_of(area.objects.begin(), area.objects.end(), [](const auto &value) { return value.questDestination.has_value(); })) continue;
            WorldObject portal;
            portal.id = ids_.allocate(); portal.act = 1; portal.palette = area.recipe.act;
            portal.objectClass = 60;
            portal.pos = area.map.grid.nearest(int(area.definition.id) == 74 ? source : area.map.spawn);
            portal.accessPoint = portal.pos;
            portal.appearance = {"objects", {}, "on", "hth", {}};
            portal.animationMode = 2;
            configureWorldObject(portal, questObjectRows_);
            portal.interaction = Interaction::ActTwoQuest;
            portal.questDestination = RegionId(int(area.definition.id) == 74 ? 46 : 74);
            area.objects.push_back(std::move(portal));
            area.refreshObjectCollision(state().time);
        }
        return;
    }
    if (operation == 24 && int(region().definition.id) == 61 && found->operatedAt < 0) {
        const auto entry = resolveObjectTreasure(content_, worldContent_, region().definition.id, state().population.difficulty);
        if (!entry.deferred.empty()) { simulation_->emit(LootDeferred{id, entry.deferred}); return; }
        std::set<size_t> usedUniques;
        for (auto row : loot_.usedUniques()) usedUniques.insert(size_t(row));
        auto plan = planItemLoot(content_, content_.tables.at("itemratio"), entry.treasureClass,
            entry.itemLevel, 0, world_.at(current_).objectSeed, usedUniques, characterDefinition_.code, 0, 0, DropQuality::Magic);
        if (!plan.deferred.empty()) { simulation_->emit(LootDeferred{id, plan.deferred}); return; }
        if (staff.stage < 6 && !carriesQuestItem("vip") && !carriesQuestItem("hst")) {
            plan.drops.push_back({"vip", 1, {}, unsigned(entry.itemLevel), {}});
        }
        const int count = 5 + int(limitedRandom(plan.randomState, 5));
        for (int index = 0; index < count; ++index)
            plan.drops.push_back({"gld", 1 + limitedRandom(plan.randomState, 5), {2, 3}, 1, {}});
        world_.at(current_).objectSeed = plan.randomState;
        auto drops = loot_.settle({id, {}, region().definition.id, state().population.difficulty}, std::move(plan));
        spawnLoot(drops, region().definition.id, found->pos);
        found->operatedAt = state().time;
        auto &sun = book.at(questIndex(QuestId::TaintedSun));
        if (sun.stage < 3) { sun.stage = 3; simulation_->emit(QuestAdvanced{QuestId::TaintedSun, sun.stage}); }
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name});
    } else if (operation == 25 && int(region().definition.id) == actTwoTombs(state().mapSeed)[0] &&
               found->operatedAt < 0 && staff.stage < 6) {
        if (!submitted) {
            simulation_->emit(ObjectInteracted{id, found->interaction, found->name});
            return;
        }
        const auto *source = submitted ? inventory_.item(submitted->id) : nullptr;
        const auto *location = source ? std::get_if<ContainerLocation>(&source->location) : nullptr;
        if (!source || source->revision != submitted->revision || source->definition != "hst" ||
            source->nativeQuestDifficulty < unsigned(state().population.difficulty) || !location ||
            (location->container != playerContainers_.backpack && location->container != playerContainers_.cursor)) {
            simulation_->emit(InteractionFailed{id, "Place the complete Horadric Staff into the orifice."}); return;
        }
        WorldObject entrance;
        entrance.act = 1;
        entrance.palette = region().recipe.act;
        entrance.objectClass = 100;
        entrance.pos = found->pos + Vec{-13, 3};
        entrance.accessPoint = map().grid.nearest(entrance.pos, playerMovement);
        entrance.appearance = {"objects", {}, "on", "hth", {}};
        entrance.animationMode = 2;
        configureWorldObject(entrance, questObjectRows_);
        auto openedMap = map();
        if (!openedMap.openTombWall(entrance.pos) ||
            (openedMap.grid.nearest(entrance.pos, playerMovement) - entrance.pos).length() > 5 ||
            openedMap.grid.path(state().player.movement.pos, openedMap.grid.nearest(entrance.pos, playerMovement), false, playerMovement).empty()) {
            simulation_->emit(InteractionFailed{id, "The original tomb entrance is blocked; the staff has not been consumed."});
            return;
        }
        const auto &missiles = content_.tables.at("missiles");
        std::optional<int> openingDelay;
        for (size_t row = 0; row < missiles.rows().size(); ++row)
            if (missiles.number(row, "Id") == 338)
                if (const auto range = missiles.number(row, "Range"); range && *range > 75)
                    openingDelay = std::max(1, (*range - 75) / 20) * 20;
        if (!openingDelay) {
            simulation_->emit(InteractionFailed{id, "Original Horadric Staff opening data is unavailable."});
            return;
        }
        auto backup = inventory_.state_;
        InventoryResult transaction;
        for (auto container : {playerContainers_.backpack, playerContainers_.cube, playerContainers_.equipment, playerContainers_.cursor})
            for (auto itemId : inventory_.contents(container)) {
                const auto *item = inventory_.item(itemId);
                if (item->definition != "hst" && item->definition != "msf" && item->definition != "vip") continue;
                if (item->nativeQuestDifficulty < unsigned(state().population.difficulty)) continue;
                auto removed = container == playerContainers_.equipment ? inventory_.consumeEquipped(itemId, playerContainers_)
                    : inventory_.consume(item->handle(), item->quantity, inventoryAccess());
                if (!removed) { inventory_.state_ = std::move(backup); return; }
                transaction.changes.insert(transaction.changes.end(), removed.changes.begin(), removed.changes.end());
            }
        found->operatedAt = state().time;
        staff.stage = 6;
        publishInventory(std::move(transaction), {});
        tombCollapseFrame_ = state().frame + *openingDelay;
        const auto &collapse = entrance.animationRules[1];
        const int collapseFrames = collapse.fps > 0 ? int(std::ceil(collapse.frames * 25.f / collapse.fps)) : 1;
        tombOpeningFrame_ = *tombCollapseFrame_ + collapseFrames;
        simulation_->emit(QuestAdvanced{QuestId::HoradricStaff, staff.stage});
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name});
    } else if (operation == 43 && found->modeAt(state().time) == 2) {
        if (int(region().definition.id) != 73 && !questExitAllowed(RegionId(73))) return;
        const auto destination = int(region().definition.id) == 73 ? RegionId(actTwoTombs(state().mapSeed)[0]) : RegionId(73);
        ensureRegion(destination);
        enter(destination);
    }
}
bool GameSessionImpl::canInsertStaff(EntityId id) const {
    const auto target = std::find_if(region().objects.begin(), region().objects.end(), [&](const auto &value) { return value.id == id; });
    return target != region().objects.end() && target->operateFn == 25 && target->operatedAt < 0 && !state().player.actions.dead &&
        int(region().definition.id) == actTwoTombs(state().mapSeed)[0] &&
        quest(QuestId::HoradricStaff).stage < uint32_t(StaffStage::Submitted) && canReach(*target);
}
void GameSessionImpl::updateActTwoObjects() {
    auto &sun = simulation_->state_.player.character.actOneQuests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::TaintedSun));
    if (sunDarkeningFrame_ && state().frame >= *sunDarkeningFrame_) {
        sunDarkeningFrame_.reset();
        if (!sun.stage) { sun.stage = 1; simulation_->emit(QuestAdvanced{QuestId::TaintedSun, sun.stage}); }
    }
    for (auto &region : world_.regions())
        for (auto &object : region.objects)
            if (object.objectClass == 318)
                object.animationMode = quest(QuestId::ArcaneSanctuary).stage > 0 ? 2 : 0;
            else if (int(region.definition.id) == 73 && object.npcClass == "tyrael1")
                object.questHidden = quest(QuestId::SevenTombs).stage < 2;
            else if (int(region.definition.id) == 73 && object.objectClass == 153 && object.operatedAt < 0)
                object.animationMode = quest(QuestId::SevenTombs).stage >= 2 ? 2 : 0;
    if (quest(QuestId::HoradricStaff).stage < 6 || (tombCollapseFrame_ && state().frame < *tombCollapseFrame_)) return;
    const auto tomb = RegionId(actTwoTombs(state().mapSeed)[0]);
    auto region = std::find_if(world_.regions().begin(), world_.regions().end(), [&](const auto &value) { return value.definition.id == tomb; });
    if (region == world_.regions().end() || !region->loaded) return;
    if (std::any_of(region->objects.begin(), region->objects.end(), [](const auto &value) { return value.objectClass == 100; })) {
        if (!tombOpeningFrame_ || state().frame >= *tombOpeningFrame_) {
            tombOpeningFrame_.reset();
            tombCollapseFrame_.reset();
        }
        return;
    }
    const auto orifice = std::find_if(region->objects.begin(), region->objects.end(), [](const auto &value) { return value.operateFn == 25; });
    if (orifice == region->objects.end()) return;
    WorldObject portal;
    portal.id = ids_.allocate();
    portal.act = 1;
    portal.palette = region->recipe.act;
    portal.objectClass = 100;
    portal.pos = orifice->pos + Vec{-13, 3};
    portal.accessPoint = region->map.grid.nearest(portal.pos, playerMovement);
    portal.appearance.category = "objects";
    portal.appearance.mode = "on";
    portal.appearance.weapon = "hth";
    portal.animationMode = 2;
    configureWorldObject(portal, questObjectRows_);
    if (!region->map.openTombWall(portal.pos)) return;
    if (tombCollapseFrame_) portal.operatedAt = state().time;
    portal.accessPoint = region->map.grid.nearest(portal.pos, playerMovement);
    region->objects.push_back(std::move(portal));
    region->refreshObjectCollision(state().time);
}
void GameSessionImpl::completeActTwo(EntityId npc) {
    const auto *meshif = object(npc);
    if (!meshif || !npcAccess(npc).contact()) return;
    const bool east = meshif->npcClass == "meshif1" && int(region().definition.id) == 40;
    const bool west = meshif->npcClass == "meshif2" && int(region().definition.id) == 75;
    if ((!east && !west) || (east && quest(QuestId::SevenTombs).stage < 5)) return;
    enter(RegionId(east ? 75 : 40));
    if (east)
        for (const auto &waypoint : region().objects)
            if (waypoint.isWaypoint() && simulation_->state_.waypoints.emplace(region().definition.id, state().time).second)
                simulation_->emit(WaypointActivated{waypoint.id});
}
} // namespace d2x