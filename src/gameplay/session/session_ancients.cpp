#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>

namespace d2x {
namespace {
// A5Q5_SpawnAncientMonsters, original statue operations 62/63/64.
std::string_view ancientForStatue(const WorldObject &object) {
    if (object.operateFn == 63) return ancientIdentities[0];
    if (object.operateFn == 64) return ancientIdentities[1];
    if (object.operateFn == 62) return ancientIdentities[2];
    return {};
}
bool ancientSpawn(std::string_view key) {
    return std::any_of(ancientIdentities.begin(), ancientIdentities.end(), [&](auto identity) {
        return key == "quest." + std::string(identity);
    });
}
}
bool GameSessionImpl::activateAncientsObject(EntityId id) {
    if (int(region().definition.id) != 120) return false;
    auto &objects = world_.at(size_t(current_)).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &o) { return o.id == id; });
    if (found == objects.end() || found->operateFn < 62 || found->operateFn > 66) return false;
    if (state().player.actions.dead || !canReach(*found)) return true;
    if (found->operateFn == 66) {
        if (questExitAllowed(RegionId(128))) enter(RegionId(128));
        else simulation_->emit(InteractionFailed{id, "Defeat the Ancients at the required level before entering the Worldstone Keep."});
        return true;
    }
    if (found->operateFn >= 62 && found->operateFn <= 64 && found->animationMode == 2 && quest(QuestId::RiteOfPassage).stage == 4) {
        if (const auto *speech = questSpeech(content_.npcDialogues, "A5Q6", "Init", "Ancients"))
            simulation_->emit(NpcDialogueStarted{id, found->name, speech->text});
        auto &record = simulation_->state_.player.character.quests[size_t(state().population.difficulty)][questIndex(QuestId::EveOfDestruction)];
        if (record.stage < 2) { record.stage = 2; simulation_->emit(QuestAdvanced{QuestId::EveOfDestruction, 2}); }
        simulation_->state_.player.character.quests[size_t(state().population.difficulty)][questIndex(QuestId::RiteOfPassage)].flags |= 32;
        return true;
    }
    if (found->operateFn != 65 || found->animationMode != 0) return true;
    // Validate all three original statues and identities before starting anything.
    for (auto identity : ancientIdentities) {
        const auto *boss = monsterContent_.superUnique(identity);
        if (!boss || !monsterContent_.find(boss->monster) ||
            std::none_of(objects.begin(), objects.end(), [&](const auto &o) { return ancientForStatue(o) == identity; })) {
            simulation_->emit(InteractionFailed{id, "Original Ancients or their statues are unavailable."}); return true;
        }
    }
    if (simulation_->state_.portal.active && int(simulation_->state_.portal.field) == 120)
        simulation_->state_.portal.active = false;
    for (auto &portal : simulation_->state_.publicPortals)
        if (int(portal.field) == 120) portal.active = false;
    found->setAnimationMode(2, state().time);
    for (auto &statue : objects) if (!ancientForStatue(statue).empty()) {
        statue.setAnimationMode(3, state().time); statue.questTimer = state().frame + 20;
    }
    auto &record = simulation_->state_.player.character.quests[size_t(state().population.difficulty)][questIndex(QuestId::RiteOfPassage)];
    if (record.stage < 3) { record.stage = 3; simulation_->emit(QuestAdvanced{QuestId::RiteOfPassage, 3}); }
    if (auto group = content_.npcDialogues.find("act5:AncientsAct5Intro"); group != content_.npcDialogues.end() && !group->second.empty())
        simulation_->emit(NpcDialogueStarted{id, found->name, group->second.front().text});
    simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return true;
}
void GameSessionImpl::updateAncientsObjects() {
    if (int(region().definition.id) != 120) return;
    if (state().player.actions.dead) { resetAncients(); return; }
    for (auto &statue : world_.at(size_t(current_)).objects) {
        if (statue.operateFn == 66 && quest(QuestId::RiteOfPassage).stage == 4)
            statue.setAnimationMode(2, state().time);
        const auto identity = ancientForStatue(statue);
        if (identity.empty() || !statue.questTimer || state().frame < *statue.questTimer) continue;
        const auto *boss = monsterContent_.superUnique(identity);
        if (boss && spawnQuestEnemy(statue.pos, boss->monster, boss->id)) {
            statue.setAnimationMode(4, state().time); statue.questTimer.reset();
        } else statue.questTimer = state().frame + 10;
    }
}
void GameSessionImpl::resetAncients() {
    if (int(region().definition.id) != 120) return;
    auto &objects = world_.at(size_t(current_)).objects;
    if (std::none_of(objects.begin(), objects.end(), [](const auto &o) {
        return !ancientForStatue(o).empty() && (o.animationMode == 3 || o.animationMode == 4);
    })) return;
    auto &area = simulation_->state_.area;
    std::erase_if(area.enemies, [](const auto &e) { return ancientSpawn(e.identity.spawnKey); });
    std::erase_if(area.pendingSpawns, [](const auto &s) { return ancientSpawn(s.identity.spawnKey); });
    for (auto &object : objects) if (!ancientForStatue(object).empty() || object.operateFn == 65) {
        object.setAnimationMode(0, state().time); object.questTimer.reset(); object.operatedAt = -1;
    }
}
void GameSessionImpl::completeAncientsBattle() {
    if (int(region().definition.id) != 120) return;
    for (auto &object : world_.at(size_t(current_)).objects)
        if (!ancientForStatue(object).empty() || object.operateFn == 66) {
            object.setAnimationMode(2, state().time); object.questTimer.reset();
        }
}
void GameSessionImpl::rewardAncientsExperience() {
    const auto &thresholds = experienceThresholds();
    const auto level = size_t(state().player.character.level);
    if (level + 1 >= thresholds.size()) return;
    constexpr std::array<uint64_t, 3> rewards{1400000, 20000000, 40000000};
    grantExperience(std::min(rewards.at(size_t(state().population.difficulty)), thresholds[level + 1] - thresholds[level]));
}
} // namespace d2x
