#pragma once
#include "gameplay/rewards/death.hpp"
#include "gameplay/quest/state.hpp"
#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace d2x {
struct QuestDeathContext {
    std::array<QuestRecord, size_t(QuestId::Count)> records;
    std::optional<RegionId> burial, towerCellar, catacombsFour;
    std::string countessMonster, countessSuperUnique;
    bool andarielAvailable = false;
    bool jadeFigurineBoss = false, gidbinnBoss = false;
    bool khalimFlailDrop = false, councilCubeDrop = false;
    bool councilCleared = false;
    bool ancientsCleared = false, ancientsRewardEligible = false;
};
enum class QuestDeathWaveKind { BloodRaven, Andariel, Radament };
struct QuestDeathWave { QuestDeathWaveKind kind; };
enum class QuestDeathNoticeEffect { None, ReconcileCain, DropRadamentBook, SlaughterReactions };
struct QuestDeathTransition {
    QuestId quest;
    QuestRecord next;
    QuestDeathNoticeEffect beforeNotice = QuestDeathNoticeEffect::None;
};
enum class QuestDeathWorldEffect { TowerChests, SlaughterPortal, DurielDoor, JadeFigurine, Gidbinn,
    KhalimFlail, CouncilCube, MephistoSoulstone, IzualGhost, ForgeHammer, AncientsDefeated, AncientsExperience, BaalTyrael };
using QuestDeathStep = std::variant<QuestDeathTransition, QuestDeathWave, QuestDeathWorldEffect>;
struct QuestDeathPlan {
    bool andarielFirstKill = false, questFirstKill = false;
    std::vector<QuestDeathStep> steps;
};
// Capture eligibility before any transition, then prepare effects in original order.
QuestDeathPlan planQuestDeath(const EnemyDied &death, const QuestDeathContext &context);
} // namespace d2x
