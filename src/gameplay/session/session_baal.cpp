#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>

namespace d2x {
bool GameSessionImpl::activateBaalObject(EntityId id) {
    const auto *target = object(id);
    if (!target || target->act != 4 || (target->operateFn != 70 && target->operateFn != 72)) return false;
    if (state().player.actions.dead || !canReach(*target)) return true;
    if (target->operateFn == 72) {
        auto &record = simulation_->state_.player.character.quests[size_t(state().population.difficulty)][questIndex(QuestId::EveOfDestruction)];
        if (int(region().definition.id) != 132 || record.stage != 5 || !(record.flags & baalTyraelSpoken)) return true;
        record.flags |= baalFinalPortalUsed;
        simulation_->emit(QuestAdvanced{QuestId::EveOfDestruction, record.stage});
        enter(RegionId(109)); return true;
    }
    if (int(region().definition.id) == 131) {
        if (target->animationMode != 2) {
            simulation_->emit(InteractionFailed{id, "Baal's throne must be cleared before this portal opens."}); return true;
        }
        enter(RegionId(132));
    } else if (int(region().definition.id) == 132) enter(RegionId(131));
    return true;
}
void GameSessionImpl::updateBaalObjects() {
    if (int(region().definition.id) == 132) {
        if (quest(QuestId::EveOfDestruction).stage == 5 && (quest(QuestId::EveOfDestruction).flags & baalTyraelSpoken)) {
            const auto npc = std::find_if(region().objects.begin(), region().objects.end(), [](const auto &o) { return o.npcClass == "tyrael3"; });
            if (npc != region().objects.end()) createQuestPortal(state().player.movement.pos + Vec{5, 0}, 565, RegionId(109));
        }
        return;
    }
    if (int(region().definition.id) != 131 || state().player.actions.dead) return;
    auto &objects = world_.at(size_t(current_)).objects;
    const bool departed = std::any_of(objects.begin(), objects.end(), [](const auto &o) { return o.npcClass == "baalthrone" && o.questHidden; });
    for (auto &portal : objects) if (portal.operateFn == 70 && portal.objectClass == 563)
        portal.setAnimationMode(departed ? 2 : 0, state().time);
    for (auto &throne : objects) {
        if (throne.npcClass != "baalthrone") continue;
        if (throne.questHidden) {
            for (auto &portal : objects) if (portal.operateFn == 70 && portal.objectClass == 563) portal.setAnimationMode(2, state().time);
            continue;
        }
        if ((state().player.movement.pos - throne.pos).length() >= 64) continue;
        // AITHINK_Fn134_BaalThrone counts live hostile monsters within 64,
        // including the original ambient population. Lured-out minions stop
        // blocking, as in the original; distant pending rooms are not skipped.
        const bool blocked = std::any_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const auto &e) {
            return e.hp > 0 && simulation_->relation(state().player.id, e.id) == Relation::Hostile && (e.pos - throne.pos).length() < 64;
        }) || std::any_of(state().area.pendingSpawns.begin(), state().area.pendingSpawns.end(), [&](const auto &s) {
            return (s.position - throne.pos).length() < 64;
        });
        if (blocked || throne.questTimer && state().frame < *throne.questTimer) continue;
        if (!throne.questWavePrepared) {
            throne.questWavePrepared = true; throne.questTimer = state().frame + 250; continue;
        }
        if (throne.questHits < 5) {
            const auto identity = "Baal Subject " + std::to_string(throne.questHits + 1);
            const auto *boss = monsterContent_.superUnique(identity);
            if (!boss || !spawnQuestEnemy(throne.pos + Vec{0, 13}, boss->monster, boss->id)) {
                throne.questTimer = state().frame + 10; continue;
            }
            ++throne.questHits; throne.questWavePrepared = false; throne.questTimer = state().frame + 100;
        } else {
            // The five waves have left the throne. The original BaalToStairs
            // AI removes the throne actor and opens Objects.563.
            throne.questHidden = true; throne.questTimer.reset();
            for (auto &portal : objects) if (portal.operateFn == 70 && portal.objectClass == 563) portal.setAnimationMode(2, state().time);
            auto &record = simulation_->state_.player.character.quests[size_t(state().population.difficulty)][questIndex(QuestId::EveOfDestruction)];
            if (record.stage < 4) { record.stage = 4; simulation_->emit(QuestAdvanced{QuestId::EveOfDestruction, 4}); }
        }
    }
}
} // namespace d2x
