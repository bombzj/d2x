#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <iostream>

namespace d2x {
void GameSessionImpl::ensureRegion(RegionId id, bool neighbours) {
    const bool changed = world_.ensure(id, neighbours, ids_, monsterContent_, worldContent_, [&](Region &region) {
        if (region.definition.safe)
            for (const auto &layer : region.map.terrain.data.walls)
                for (size_t index = 0; index < layer.size(); ++index) {
                    const auto &cell = layer[index];
                    if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11) &&
                        ((cell.value >> 20) & 63) == 33) {
                        const Vec point{float(index % region.map.terrain.data.width * 5 + 3),
                            float(index / region.map.terrain.data.width * 5 + 3)};
                        const auto arrival = region.map.grid.nearest(point);
                        if (region.map.grid.walkable(arrival) && (arrival - point).length() <= 5) {
                            townPortalArrivals_[region.definition.id] = arrival;
                            if (region.definition.id == RegionId::Encampment) townPortalArrival_ = arrival;
                        }
                    }
                }
        for (auto &object : region.objects) {
            if (const auto *npc = monsterContent_.find(object.npcClass)) {
                if (const auto name = content_.itemStrings.find(npc->name); name != content_.itemStrings.end()) object.name = name->second;
                if (npc->interact && !npc->hostile()) object.interaction = npcCanHeal(object.npcClass) ? Interaction::Heal : Interaction::Talk;
            }
            if (!object.npcPath.empty()) initialNpcMotions_.push_back({object.id, object.pos, object.npcLook,
                object.npcRoute, object.npcWait, object.npcTarget, object.npcRandom});
            if (auto vendor = content_.vendors.find(object.npcClass); vendor != content_.vendors.end())
                vendorStocks_.emplace(object.id, planVendorStock(content_, vendor->second, equipmentActor().level,
                    state().population.difficulty, childRandom(random_)));
        }
    });
    if (changed) {
        reconcileCainObjects();
    }
}
void GameSessionImpl::enter(RegionId id, std::optional<Vec> arrival, std::optional<Vec> coordinateOffset) {
    ensureRegion(id, true);
    auto found = std::find_if(world_.regions().begin(), world_.regions().end(),
                              [id](const Region &r) { return r.definition.id == id; });
    if (found == world_.regions().end())
        return;
    int index = int(found - world_.regions().begin());
    const bool returnToTown = current_ >= 0 && !world_.regions()[current_].definition.safe && found->definition.safe;
    if (current_ >= 0 && found->recipe.act > region().recipe.act) {
        constexpr std::array actEndQuests{QuestId::SistersToTheSlaughter, QuestId::SevenTombs,
            QuestId::Guardian, QuestId::TerrorsEnd};
        const auto act = size_t(region().recipe.act);
        if (act < actEndQuests.size() && quest(actEndQuests[act]).stage >= questCompletionStage(actEndQuests[act]))
            simulation_->state_.player.character.completedActs[size_t(state().population.difficulty)][act] = true;
    }
    if (current_ >= 0 && int(region().definition.id) == 120 && int(id) != 120) resetAncients();
    if (current_ >= 0)
        areas_.park(size_t(current_), simulation_->leaveArea());
    current_ = index;
    pendingCorpse_ = {};
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    auto plan = areas_.initialized(size_t(current_)) ? PopulationPlan{} : population(*found);
    simulation_->missileWorldOrigin_ = {float(found->recipe.worldX * 5), float(found->recipe.worldY * 5)};
    simulation_->enterArea(found->map.grid, found->map.activation, arrival.value_or(found->map.spawn),
                          found->definition.safe,
                          areas_.take(size_t(current_)), plan.spawns, coordinateOffset);
    if (returnToTown) {
        // The single player town becomes unoccupied when leaving it. Rebuild
        // its stock on return using the current character level (SUnitProxy).
        for (const auto &npc : found->objects)
            if (auto vendor = content_.vendors.find(npc.npcClass); vendor != content_.vendors.end()) {
                auto &seed = inventory_.state_.creationRandom;
                auto stock = planVendorStock(content_, vendor->second, unsigned(state().player.character.level),
                    state().population.difficulty, childRandom(seed));
                vendorStocks_[npc.id] = std::move(stock);
                soldVendorOffers_.erase(npc.id);
                gambleStocks_.erase(npc.id);
            }
    }
    if (simulation_->state_.player.hireling.active()) {
        auto &hireling = simulation_->state_.player.hireling;
        hireling.pos = state().player.movement.pos;
        hireling.route.clear();
        hireling.attack.reset(); hireling.attackTimer = 0;
        hireling.moving = false; hireling.animationTime = 0;
    }
    onQuestRegionEntered(id);
    updateActTwoObjects();
    updateLaterQuestObjects();
    std::cout << "Room activation: created=" << state().area.enemies.size()
              << " deferred=" << state().area.pendingSpawns.size() << '\n';
}
PopulationPlan GameSessionImpl::population(const Region &region) const {
    const auto level = worldContent_.levels().find(int(region.definition.id));
    const auto *record = level == worldContent_.levels().end() ? nullptr : &level->second;
    const auto &preset = worldContent_.presets().at(region.recipe.preset);
    auto plan = planPopulation(monsterContent_, record, preset, region.map, state().population);
    writePopulationReport(std::cout, plan, record, preset, state().population);
    return plan;
}
} // namespace d2x
