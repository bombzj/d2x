#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
Simulation::Simulation(EntityIds &ids) : ids_(ids) {
    state_.player.id = ids_.allocate();
    heal();
}
void Simulation::clearActions() {
    auto &p = state_.player;
    p.route.clear();
    p.attackTarget = {};
    p.castTime = p.spinTime = p.leapTime = p.meleeTime = p.hitTime = 0;
    p.moving = false;
    state_.message.clear();
}
AreaState Simulation::leaveArea() {
    return std::move(state_.area);
}
void Simulation::enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, bool safeZone, AreaState area,
                           std::span<const MonsterSpawn> monsters) {
    grid_ = &grid;
    rooms_ = &rooms;
    safeZone_ = safeZone;
    state_.area = std::move(area);
    clearActions();
    state_.player.pos = state_.player.previous = grid.nearest(spawn);
    if (!state_.area.initialized) {
        state_.area.pendingSpawns.assign(monsters.begin(), monsters.end());
        state_.area.initialized = true;
    }
    activateMonsters();
    emit(RegionEntered{state_.area.region});
}
void Simulation::restartArea(Vec spawn, std::span<const MonsterSpawn> monsters) {
    auto id = state_.area.region;
    heal();
    state_.player.cooldown.fill(0);
    AreaState area;
    area.region = id;
    enterArea(*grid_, *rooms_, spawn, safeZone_, std::move(area), monsters);
}
void Simulation::heal() {
    auto &p = state_.player;
    p.hp = playerRules().maxLife;
    p.mana = playerRules().maxMana;
    p.stamina = playerRules().maxStamina;
    p.healing.clear();
    p.manaRestoration.clear();
    p.staminaBoost = 0;
    p.dead = false;
    p.deathTime = 0;
}
void Simulation::spawnEnemies(std::span<const MonsterSpawn> spawns) {
    auto &area = state_.area;
    area.enemies.reserve(area.enemies.size() + spawns.size());
    for (const auto &spawn : spawns) {
        Enemy enemy;
        enemy.id = ids_.allocate();
        enemy.combatRandom = (uint64_t(666) << 32) | (state_.population.seed ^ uint32_t(enemy.id.value));
        enemy.identity = spawn.identity;
        enemy.kind = spawn.kind;
        enemy.pos = spawn.position;
        enemy.hp = monsterDefinition(enemy.kind).maxLife;
        area.enemies.push_back(std::move(enemy));
    }
}
Enemy *Simulation::findEnemy(EntityId id) {
    auto &enemies = state_.area.enemies;
    auto found = std::find_if(enemies.begin(), enemies.end(), [id](const Enemy &e) { return e.id == id; });
    return found == enemies.end() ? nullptr : &*found;
}
void Simulation::execute(const GameCommand &command) {
    if (!grid_)
        return;
    std::visit(
        [this](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveTo>)
                moveTo(intent.position);
            else if constexpr (std::is_same_v<T, Attack>)
                attackEnemy(intent.target);
            else if constexpr (std::is_same_v<T, DebugKill>) {
                if (!state_.player.dead)
                    if (auto enemy = findEnemy(intent.target);
                        enemy && enemy->hp > 0 && (intent.ignoreActivation || active(enemy->pos)))
                        damageEnemy(*enemy, enemy->hp, state_.player.id, 0, intent.ignoreActivation);
            }
            else if constexpr (std::is_same_v<T, CastSkill>)
                cast(intent.skill, intent.target);
            else if constexpr (std::is_same_v<T, ToggleRun>)
                state_.player.running = !state_.player.running;
            else if constexpr (std::is_same_v<T, StopMoving>)
                stopWalking();
        },
        command);
}
void Simulation::tick(float dt, Vec keyboard) {
    if (!grid_ || dt <= 0)
        return;
    auto &p = state_.player;
    p.previous = p.pos;
    state_.time += dt;
    for (auto &cooldown : p.cooldown)
        cooldown = std::max(0.f, cooldown - dt);
    p.castTime = std::max(0.f, p.castTime - dt);
    p.spinTime = std::max(0.f, p.spinTime - dt);
    p.hitTime = std::max(0.f, p.hitTime - dt);
    p.meleeTime = std::max(0.f, p.meleeTime - dt);
    p.moving = false;
    if (p.dead)
        p.deathTime += dt;
    for (auto &e : state_.area.enemies) {
        if (!active(e.pos))
            continue;
        e.hitFlash = std::max(0.f, e.hitFlash - dt);
        if (e.hp <= 0)
            e.deathAge += dt;
    }
    if (!p.dead) {
        updatePotions(dt);
        updatePlayer(dt, keyboard);
        activateMonsters();
        updateMonsters(dt);
        if (p.hp <= 0) {
            p.dead = true;
            p.healing.clear();
            p.manaRestoration.clear();
            p.staminaBoost = 0;
            p.route.clear();
            p.attackTarget = {};
            state_.message = "You have died. Press R to return.";
            emit(PlayerDied{p.id});
        }
    }
    updateMissiles(dt);
    if (!p.dead && state_.area.pendingSpawns.empty() && !state_.area.enemies.empty() &&
        state_.area.kills == int(state_.area.enemies.size()))
        state_.message = "Area cleared. F2: travel onward. R: repopulate the area.";
    for (auto &e : state_.area.effects)
        e.age += dt;
    std::erase_if(state_.area.effects, [](const Effect &e) { return e.age >= e.duration; });
}
} // namespace d2x
