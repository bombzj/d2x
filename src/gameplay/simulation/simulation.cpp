#include "gameplay/units/resources.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void Simulation::emit(QuestAdvanced event) {
    event.completed = event.stage >= questCompletionStage(event.quest);
    events_.emplace_back(std::in_place_type<QuestAdvanced>, std::move(event));
}
void Simulation::clearActions() {
    state_.player.skills.pendingCast.reset();
    skills().stopChannel(skillCaster(state_.player.id));
    auto &p = state_.player;
    clearAttackIntent(skillWeaponCaster(p.id));
    p.actions.castTime = p.actions.meleeTime = p.actions.hitTime = 0;
    cancelWeaponAction(skillWeaponCaster(p.id));
    p.actions.charge.reset();
    p.actions.blockAnimation.reset();
    p.movement.moving = false;
    p.movement.runningNow = false;
    state_.message.clear();
}
AreaState Simulation::leaveArea() {
    return std::move(state_.area);
}
void Simulation::enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, bool safeZone, AreaState area,
                           std::span<const MonsterSpawn> monsters, std::optional<Vec> coordinateOffset) {
    grid_ = &grid;
    rooms_ = &rooms;
    safeZone_ = safeZone;
    state_.area = std::move(area);
    clearActions();
    state_.player.movement.pos = state_.player.movement.previous = grid.walkable(spawn, playerMovement) ? spawn : grid.nearest(spawn, playerMovement);
    if (!state_.area.initialized) {
        state_.area.pendingSpawns.assign(monsters.begin(), monsters.end());
        state_.area.initialized = true;
    }
    activateMonsters();
    relocateCompanions(state_.player.id, state_.player.movement.pos);
    emit(RegionEntered{state_.area.region, coordinateOffset});
}
void Simulation::restartArea(Vec spawn, std::span<const MonsterSpawn> monsters) {
    auto id = state_.area.region;
    heal();
    AreaState area;
    area.region = id;
    enterArea(*grid_, *rooms_, spawn, safeZone_, std::move(area), monsters);
}
void Simulation::heal() {
    auto &p = state_.player;
    p.resources.hp = state_.player.attributes.maxLife;
    p.resources.mana = state_.player.attributes.maxMana;
    p.resources.stamina = state_.player.attributes.maxStamina;
    p.resources.healing.clear();
    p.resources.manaRestoration.clear();
    // NPC healing cures ailments; it does not dispel beneficial item states.
    // D2MOO SUNITNPC_HealPlayer removes poison/freeze/curable states only.
    p.resources.chill = 0;
    p.resources.poisonRemaining = p.resources.poisonPerSecond = 0;
    p.resources.webSlowRemaining = 0;
    p.resources.webSlowPercent = 0;
    p.resources.webSource = {};
    if (p.hireling.active() && hirelingAttributes_) {
        auto &merc = p.hireling;
        merc.hp = float(hirelingAttributes_().maxLife);
        merc.chill = merc.poisonRemaining = merc.poisonPerSecond = 0;
        merc.webSlowRemaining = 0; merc.healing.clear();
    }
    p.actions.dead = false;
    p.actions.deathTime = 0;
}
void Simulation::spawnEnemies(std::span<const MonsterSpawn> spawns) {
    auto &area = state_.area;
    const size_t first = area.enemies.size();
    area.enemies.reserve(area.enemies.size() + spawns.size());
    for (const auto &spawn : spawns) {
        Enemy enemy;
        enemy.id = ids_.allocate();
        enemy.combatRandom = childRandom(unitRandom_);
        enemy.identity = spawn.identity;
        enemy.kind = spawn.kind;
        enemy.pos = spawn.position;
        enemy.aiHome = spawn.position;
        enemy.skillPositions = spawn.skillPositions;
        enemy.deathUnselectable = enemy.kind == MonsterKind::BloodRaven ||
                     enemy.identity.superUnique == "The Countess";
        enemy.maxHp = monsterDefinition(enemy.kind).maxLife;
        auto baseIdentity = enemy.identity;
        if (baseIdentity.rank == MonsterRank::Champion || baseIdentity.rank == MonsterRank::Unique ||
            baseIdentity.rank == MonsterRank::SuperUnique)
            baseIdentity.rank = MonsterRank::Normal;
        if (monsterNormalCombat_)
            if (auto combat = monsterNormalCombat_(baseIdentity, area.region, nullptr)) {
                rollRandom(enemy.combatRandom);
                enemy.maxHp = float(combat->minLife +
                    uint32_t(enemy.combatRandom) % unsigned(combat->maxLife - combat->minLife + 1));
            }
        enemy.hp = enemy.maxHp;
        if (enemy.kind == MonsterKind::FoulCrowNest && monsterAi_)
            if (auto ai = monsterAi_(enemy); ai && ai->kind == MonsterAiKind::FoulCrowNest)
                enemy.aiWait = float(ai->params[0]) / 25.f;
        area.enemies.push_back(std::move(enemy));
    }
    if (initializeNaturalElite_) {
        for (size_t index = first; index < area.enemies.size(); ++index)
            initializeNaturalElite_(area.enemies[index], nullptr);
        for (size_t index = first; index < area.enemies.size(); ++index) {
            auto &minion = area.enemies[index];
            if (minion.identity.ownerSpawnKey.empty()) continue;
            const auto owner = std::find_if(area.enemies.begin(), area.enemies.end(), [&](const Enemy &candidate) {
                return candidate.identity.spawnKey == minion.identity.ownerSpawnKey;
            });
            if (owner != area.enemies.end()) initializeNaturalElite_(minion, &*owner);
        }
    }
}
Enemy *Simulation::findEnemy(EntityId id) {
    auto &enemies = state_.area.enemies;
    auto found = std::find_if(enemies.begin(), enemies.end(), [id](const Enemy &e) { return e.id == id; });
    if (found != enemies.end()) return &*found;
    for (auto &companion : state_.companions) if (companion.id == id) return &companion;
    return nullptr;
}
void Simulation::combatEffectsChanged(std::span<const RemovedCombatEffect> removed) {
    if (combatEffectsChanged_) combatEffectsChanged_();
    restoreStaminaOnEffectRemoval(state_.player.resources.stamina, float(state_.player.attributes.maxStamina), removed);
}
} // namespace d2x
