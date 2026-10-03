#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/death.hpp"
#include "gameplay/rewards/death_wave.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace d2x {
QuestDeathContext GameSessionImpl::deathQuestContext(const EnemyDied &death) const {
    QuestDeathContext context;
    context.records = state().player.character.actOneQuests.at(size_t(death.difficulty));
    context.burial = burialRegion_;
    context.towerCellar = towerCellarRegion_;
    context.catacombsFour = catacombsFourRegion_;
    if (const auto *countess = monsterContent_.superUnique("The Countess")) {
        context.countessMonster = countess->monster;
        context.countessSuperUnique = countess->id;
    }
    context.andarielAvailable = monsterContent_.find("andariel") != nullptr;
    return context;
}
void GameSessionImpl::applyDeathQuest(const EnemyDied &death, const QuestDeathPlan &plan) {
    auto &book = simulation_->state_.player.character.actOneQuests.at(size_t(death.difficulty));
    const auto slot = world_.index(death.region);
    if (slot < 0) throw std::logic_error("Quest death references an unknown region");
    for (const auto &step : plan.steps) std::visit([&](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, QuestDeathTransition>) {
            auto &record = book.at(questIndex(effect.quest));
            record = effect.next;
            switch (effect.beforeNotice) {
            case QuestDeathNoticeEffect::None: break;
            case QuestDeathNoticeEffect::ReconcileCain: reconcileCainObjects(); break;
            case QuestDeathNoticeEffect::DropRadamentBook: {
                const LootDrop skillBook{"ass", 1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&skillBook, 1), death.region, death.position);
                break;
            }
            case QuestDeathNoticeEffect::SlaughterReactions:
                for (const auto *npc : {"Deckard Cain", "Akara", "Kashya"})
                    pendingNpcQuestMessages_.insert(std::string("A1Q6/Successful/") + npc);
                break;
            }
            simulation_->emit(QuestAdvanced{effect.quest, record.stage});
        } else if constexpr (std::is_same_v<T, QuestDeathWave>) {
            applyDeathWave(death, effect.kind);
        } else if constexpr (std::is_same_v<T, QuestDeathWorldEffect>) {
            auto &area = world_.at(size_t(slot));
            switch (effect) {
            case QuestDeathWorldEffect::TowerChests:
                for (auto &object : area.objects)
                    if (object.objectClass == 371) object.towerRewardStart = state().frame;
                break;
            case QuestDeathWorldEffect::DurielDoor:
                for (auto &object : area.objects)
                    if (object.objectClass == 153) { object.operatedAt = state().time; object.animationMode = 2; }
                break;
            case QuestDeathWorldEffect::SlaughterPortal:
                if (portalResources_ && townPortalArrival_ && portalReach_ > 0 &&
                    state().nextPortalRevision < std::numeric_limits<uint64_t>::max()) {
                    auto position = area.map.grid.nearest(death.position);
                    simulation_->state_.portal = {true, ++simulation_->state_.nextPortalRevision,
                        death.region, position, *townPortalArrival_, state().time};
                }
                break;
            }
        }
    }, step);
}
void GameSessionImpl::applyDeathWave(const EnemyDied &death, QuestDeathWaveKind kind) {
    // Current simulator owns one active area's enemies. Do not borrow a different
    // area just because the recipient is in it; cross-area scheduling is not implemented.
    if (state().area.region != death.region) return;
    auto *source = simulation_->findEnemy(death.victim);
    if (!source) return;
    DeathWaveDelay delay;
    switch (kind) {
    case QuestDeathWaveKind::BloodRaven: delay = {25, 100}; break;
    case QuestDeathWaveKind::Andariel: delay = {1, 50}; break;
    case QuestDeathWaveKind::Radament: {
        const auto &missiles = content_.tables.at("missiles");
        int maximumDelay = 100;
        for (size_t row = 0; row < missiles.rows().size(); ++row)
            if (missiles.value(row, "Missile") == "radamentdeath")
                maximumDelay = std::max(100, missiles.number(row, "Range").value_or(200) - 100);
        delay = {40, unsigned(maximumDelay - 40)};
        break;
    }
    }
    std::vector<DeathWaveTarget> targets;
    targets.reserve(state().area.enemies.size());
    for (const auto &enemy : state().area.enemies) {
        bool eligible = enemy.hp > 0 && simulation_->relation(source->id, enemy.id) == Relation::Allied &&
            region().map.activation.nearby(source->pos, enemy.pos);
        if (kind == QuestDeathWaveKind::BloodRaven) {
            const auto *monster = monsterContent_.find(enemy.identity.monster);
            eligible = eligible && monster && monster->undead;
        }
        targets.push_back({enemy.id, enemy.pos, eligible});
    }
    auto plan = planDeathWave(death.position, state().frame, delay, targets, source->combatRandom);
    source->combatRandom = plan.random;
    auto next = plan.deaths.begin();
    for (auto &enemy : simulation_->state_.area.enemies)
        if (next != plan.deaths.end() && enemy.id == next->target) {
            enemy.questDeathFrame = next->frame;
            ++next;
        }
}
} // namespace d2x
