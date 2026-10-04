#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/death.hpp"
#include "gameplay/rewards/death_wave.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace d2x {
QuestDeathContext GameSessionImpl::deathQuestContext(const EnemyDied &death) const {
    QuestDeathContext context;
    context.records = state().player.character.quests.at(size_t(death.difficulty));
    if (int(death.region) == 120 && int(state().area.region) == 120 &&
        std::find(ancientIdentities.begin(), ancientIdentities.end(), death.identity.superUnique) != ancientIdentities.end() &&
        death.identity.spawnKey == "quest." + death.identity.superUnique) {
        context.ancientsCleared = std::all_of(ancientIdentities.begin(), ancientIdentities.end(), [&](auto identity) {
            const auto key = "quest." + std::string(identity);
            return std::any_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const auto &e) { return e.identity.spawnKey == key && e.hp <= 0; }) &&
                std::none_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const auto &e) { return e.identity.spawnKey == key && e.hp > 0; }) &&
                std::none_of(state().area.pendingSpawns.begin(), state().area.pendingSpawns.end(), [&](const auto &s) { return s.identity.spawnKey == key; });
        }) && std::any_of(region().objects.begin(), region().objects.end(), [](const auto &o) { return o.operateFn >= 62 && o.operateFn <= 64 && o.animationMode == 4; });
        context.ancientsRewardEligible = !state().player.actions.dead && state().player.character.level >= 20 * (int(death.difficulty) + 1);
    }
    context.burial = burialRegion_;
    context.towerCellar = towerCellarRegion_;
    context.catacombsFour = catacombsFourRegion_;
    if (const auto *countess = monsterContent_.superUnique("The Countess")) {
        context.countessMonster = countess->monster;
        context.countessSuperUnique = countess->id;
    }
    context.andarielAvailable = monsterContent_.find("andariel") != nullptr;
    context.jadeFigurineBoss = death.victim == jadeFigurineBoss_ && !jadeFigurineDropped_ &&
        quest(QuestId::GoldenBird).stage < 5;
    context.gidbinnBoss = death.victim == gidbinnBoss_ && quest(QuestId::BladeOfTheOldReligion).stage < 4;
    const std::array councilIds{"Ismail Vilehand", "Geleb Flamefinger", "Toorc Icefist"};
    if (int(death.region) == 83 && std::find(councilIds.begin(), councilIds.end(), death.identity.superUnique) != councilIds.end() &&
        death.identity.origin != SpawnOrigin::Summoned) {
        const auto &area = areaState(world_.index(death.region));
        context.councilCleared = std::all_of(councilIds.begin(), councilIds.end(), [&](const auto identity) {
            const auto *original = monsterContent_.superUnique(identity);
            if (!original) return false;
            const bool dead = std::any_of(area.enemies.begin(), area.enemies.end(), [&](const auto &enemy) {
                return enemy.identity.superUnique == original->id && enemy.hp <= 0;
            });
            const bool remaining = std::any_of(area.enemies.begin(), area.enemies.end(), [&](const auto &enemy) {
                return enemy.identity.superUnique == original->id && enemy.hp > 0;
            }) || std::any_of(area.pendingSpawns.begin(), area.pendingSpawns.end(), [&](const auto &spawn) {
                return spawn.identity.superUnique == original->id;
            });
            return dead && !remaining;
        });
        auto exists = [&](std::string_view code) {
            return std::any_of(inventory_.state().items.begin(), inventory_.state().items.end(), [&](const auto &entry) {
                return entry.second.definition == code && entry.second.nativeQuestDifficulty >= unsigned(death.difficulty);
            });
        };
        context.khalimFlailDrop = quest(QuestId::KhalimsWill).stage < 4 &&
            !exists(content_.khalimRecipe.inputs[3]) && !exists(content_.khalimRecipe.output);
        context.councilCubeDrop = !std::any_of(inventory_.state().items.begin(), inventory_.state().items.end(),
            [&](const auto &entry) { return entry.second.definition == content_.cubeCode; });
    }
    return context;
}
void GameSessionImpl::applyDeathQuest(const EnemyDied &death, const QuestDeathPlan &plan) {
    if (int(death.region) == 39 && death.identity.superUnique == "The Cow King" &&
        quest(QuestId::EveOfDestruction).stage >= questCompletionStage(QuestId::EveOfDestruction))
        simulation_->state_.player.character.cowKingKilled.at(size_t(death.difficulty)) = true;
    auto &book = simulation_->state_.player.character.quests.at(size_t(death.difficulty));
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
            case QuestDeathWorldEffect::BaalTyrael:
                pendingQuestNpcs_.push_back({death.region, death.position + Vec{-5, -5}, "tyrael3", state().frame + 1}); break;
            case QuestDeathWorldEffect::AncientsDefeated: completeAncientsBattle(); break;
            case QuestDeathWorldEffect::AncientsExperience: rewardAncientsExperience(); break;
            case QuestDeathWorldEffect::ForgeHammer: {
                const LootDrop hammer{content_.hellforge.hammer, 1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&hammer, 1), death.region, death.position); break;
            }
            case QuestDeathWorldEffect::IzualGhost:
                pendingQuestNpcs_.push_back({death.region, death.position, "izualghost", state().frame + 3}); break;
            case QuestDeathWorldEffect::MephistoSoulstone: {
                const LootDrop stone{content_.soulstoneCode, 1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&stone, 1), death.region, death.position); break;
            }
            case QuestDeathWorldEffect::KhalimFlail:
            case QuestDeathWorldEffect::CouncilCube: {
                const LootDrop item{effect == QuestDeathWorldEffect::KhalimFlail ? content_.khalimRecipe.inputs[3] : content_.cubeCode,
                    1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&item, 1), death.region, death.position); break;
            }
            case QuestDeathWorldEffect::Gidbinn: {
                const auto count = inventory_.state().items.size();
                const LootDrop blade{content_.gidbinnCode, 1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&blade, 1), death.region, death.position);
                gidbinnBoss_ = {};
                if (inventory_.state().items.size() == count)
                    for (auto &object : area.objects) if (object.operateFn == 31) {
                        object.operatedAt = -1; object.animationMode = 0; object.questTimer.reset();
                    }
                break;
            }
            case QuestDeathWorldEffect::JadeFigurine: {
                const auto count = inventory_.state().items.size();
                const LootDrop figurine{content_.goldenBird.figurine, 1, {}, unsigned(state().player.character.level), {}};
                spawnLoot(std::span(&figurine, 1), death.region, death.position);
                jadeFigurineDropped_ = inventory_.state().items.size() > count;
                if (!jadeFigurineDropped_) jadeFigurineBoss_ = {};
                break;
            }
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
