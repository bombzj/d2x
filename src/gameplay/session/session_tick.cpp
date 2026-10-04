#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <set>
#include <utility>

namespace d2x {
bool GameSessionImpl::setPlayerInput(PlayerFrameInput input) {
    if (!input.actor || input.actor != state().player.id) return false;
    playerInput_ = input;
    return true;
}
void GameSessionImpl::advance(float dt) {
    const auto input = playerInput_.actor == state().player.id ? playerInput_ : PlayerFrameInput{};
    tick(dt, input.direction, input.forceRun);
}
void GameSessionImpl::tick(float dt, Vec keyboard, bool forceRun) {
    simulation_->beginTick();
    for (auto &region : world_.regions()) region.refreshObjectCollision(state().time);
    validateStorage();
    const bool transitioned = dispatchCommands();
    if (!transitioned && keyboard.length() > .1f) {
        pendingCorpse_ = {};
        cancelExit();
        cancelPickup();
        cancelInteraction();
    }
    world_.at(size_t(current_)).refreshObjectCollision(state().time);
    std::set<int> summonSkills;
    for (const auto &pet : state().companions)
        if (!state().player.actions.dead && pet.hp > 0 && pet.allegiance.owner == state().player.id)
            summonSkills.insert(pet.summonSkill);
    for (int skill : summonSkills) {
        const auto *record = content_.skills.find(skill);
        if (!record || !record->spell || !record->spell->summon) continue;
        const int rank = effectiveSkillRank(skill);
        simulation_->enforceSummonLimit(state().player.id, skill, rank < 4 ? rank : 2 + rank / 3);
    }
    syncPlayerAura();
    simulation_->tick(dt, {state().player.id, transitioned ? Vec{} : keyboard, forceRun});
    settlePlayerDeath();
    completePlayerDeathAnimation();
    auto replenished = inventory_.replenish(dt);
    if (!replenished.changes.empty()) publishInventory(std::move(replenished), {});
    advanceHireling(dt);
    updateObjectTimers();
    updateActTwoObjects();
    updateLaterQuestObjects();
    world_.at(size_t(current_)).refreshObjectCollision(state().time);
    advanceNpcPaths(dt);
    settleDeaths();
    updateDenQuest();
    updatePickup();
    updateCorpseRecovery();
    updateQuestItems();
    updateToolsQuestItems();
    updateInteraction();
    world_.at(size_t(current_)).refreshObjectCollision(state().time);
    updatePortal();
    updateCainPortal();
    updateExit();
    validateStorage();
    ++viewRevision_;
}
} // namespace d2x
