#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_grades.hpp"
#include "gameplay/loot/special.hpp"
#include <algorithm>

namespace d2x {
namespace {
QuestRecord &tools(WorldState &world) {
    return world.player.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::ToolsOfTheTrade));
}
}
void GameSessionImpl::spawnDebugItem(const DebugSpawnItem &command) {
    auto reject = [&](const std::string &reason) { simulation_->emit(InteractionFailed{{}, reason}); };
    const auto *base = content_.items.find(command.code);
    auto ground = dropLocation();
    if (state().player.dead || !base || !base->equipment.known ||
        (base->family != ItemFamily::Weapon && base->family != ItemFamily::Armor) ||
        command.level < 1 || command.level > 99 || !ground) {
        reject("Original equipment or a walkable drop location is unavailable.");
        return;
    }
    uint64_t random = inventory_.state_.creationRandom;
    ItemGeneration generation;
    if (command.quality == ItemQuality::Magic || command.quality == ItemQuality::Rare) {
        if (!base->artAvailable) { reject("Original base item art is missing."); return; }
        auto rolled = rollAffixItem(content_, *base, command.quality, command.level, random,
                                    characterDefinition_.code);
        if (!rolled.deferred.empty()) { reject(rolled.deferred); return; }
        generation = std::move(rolled.generation);
        random = rolled.randomState;
    } else if (command.quality == ItemQuality::Set || command.quality == ItemQuality::Unique) {
        const auto &records = command.quality == ItemQuality::Set ? content_.setItems : content_.uniqueItems;
        auto selected = rollSpecialItem(records, command.code, command.level, random);
        if (!selected.row) { reject("No eligible original special item at this level."); return; }
        const auto &record = *std::find_if(records.begin(), records.end(),
            [&](const auto &entry) { return entry.row == *selected.row; });
        if (!record.artAvailable) { reject("Original special item art is missing."); return; }
        auto properties = rollSpecialProperties(record, selected.randomState);
        generation.quality = command.quality;
        generation.specialRow = int32_t(record.row);
        generation.requiredLevel = record.requiredLevel;
        generation.propertyRolls = std::move(properties.values);
        random = properties.randomState;
    } else {
        reject("Debug item quality must be magic, rare, set or unique.");
        return;
    }
    const auto previousRandom = inventory_.state_.creationRandom;
    inventory_.state_.creationRandom = random;
    auto created = inventory_.createItem(command.code, 1, *ground, unsigned(command.level), generation);
    if (!created) inventory_.state_.creationRandom = previousRandom;
    if (!created) { reject(inventoryErrorText(created.error)); return; }
    inventory_.state_.items.at(created.item).identified = true;
    publishInventory(std::move(created), {});
}
void GameSessionImpl::activateMalus(const WorldObject &source) {
    auto &record = tools(simulation_->state_);
    if (!barracksRegion_ || region().definition.id != *barracksRegion_ ||
        record.stage >= uint32_t(ToolsStage::RewardReady)) return;
    if (state().player.level < 8) {
        simulation_->emit(InteractionFailed{source.id, "Level 8 is required to take the Horadric Malus."});
        return;
    }
    bool exists = std::any_of(inventory_.state().items.begin(), inventory_.state().items.end(),
        [](const auto &entry) { return entry.second.definition == "hdm"; });
    if (!exists) {
        auto created = inventory_.createItem("hdm", 1,
            GroundLocation{region().definition.id, map().grid.nearest(source.accessPoint)});
        if (!created) {
            simulation_->emit(InteractionFailed{source.id, "Original Horadric Malus is unavailable."});
            return;
        }
        publishInventory(std::move(created), {});
    }
    for (auto &object : regions_.at(current_).objects)
        if (object.id == source.id) object.operatedAt = state().time;
    if (toolsAdvance(record, ToolsStage::MalusDropped))
        simulation_->emit(QuestAdvanced{ActOneQuest::ToolsOfTheTrade, record.stage});
    simulation_->emit(ObjectInteracted{source.id, source.interaction, source.name});
}
void GameSessionImpl::updateToolsQuestItems() {
    bool acquired = false;
    for (const auto &event : events())
        if (const auto *picked = std::get_if<ItemPickedUp>(&event);
            picked && picked->definition == "hdm") acquired = true;
    if (!acquired) return;
    auto &record = tools(simulation_->state_);
    if (toolsAdvance(record, ToolsStage::MalusAcquired))
        simulation_->emit(QuestAdvanced{ActOneQuest::ToolsOfTheTrade, record.stage});
}
void GameSessionImpl::imbueWithCharsi(const ImbueItem &command) {
    const auto *npc = object(command.npc);
    auto &record = tools(simulation_->state_);
    if (!npc || npc->name != "Charsi" || engagedNpc_ != command.npc ||
        !canReach(*npc) || region().definition.id != RegionId::Encampment ||
        record.stage != uint32_t(ToolsStage::RewardReady)) {
        simulation_->emit(InteractionFailed{command.npc, "Charsi's imbue reward is unavailable."});
        return;
    }
    const auto *item = inventory_.item(command.item.id);
    const auto *where = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
    const auto *base = item ? content_.items.find(item->definition) : nullptr;
    if (!item || item->revision != command.item.revision || !where ||
        where->container != playerContainers_.backpack || !base || !base->imbueable ||
        item->quantity != 1 || (item->quality != ItemQuality::Normal &&
        item->quality != ItemQuality::Superior && item->quality != ItemQuality::Inferior)) {
        simulation_->emit(InteractionFailed{command.npc, "Choose an unmodified weapon or armor in your backpack."});
        return;
    }
    const auto cell = where->cell;
    const auto code = item->definition;
    const int level = state().player.level > 5 ? state().player.level + 4 : state().player.level;
    auto generated = rollAffixItem(content_, *base, ItemQuality::Rare, level,
                                   inventory_.state_.creationRandom, characterDefinition_.code);
    if (!generated.deferred.empty()) {
        simulation_->emit(InteractionFailed{command.npc, generated.deferred});
        return;
    }
    auto removed = inventory_.consume(command.item, 1, inventoryAccess());
    if (!removed) {
        simulation_->emit(InteractionFailed{command.npc, "The selected item is unavailable."});
        return;
    }
    publishInventory(std::move(removed), command.item.id);
    inventory_.state_.creationRandom = generated.randomState;
    auto created = inventory_.createItem(code, 1,
        ContainerLocation{playerContainers_.backpack, cell}, unsigned(level), generated.generation);
    if (!created) throw std::logic_error("Imbue replacement failed after validated same-size removal");
    publishInventory(std::move(created), {});
    if (toolsAdvance(record, ToolsStage::Imbued))
        simulation_->emit(QuestAdvanced{ActOneQuest::ToolsOfTheTrade, record.stage});
}
} // namespace d2x
