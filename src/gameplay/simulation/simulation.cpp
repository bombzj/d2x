#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
Simulation::Simulation(EntityIds &ids) : ids_(ids) {
    state_.player.id = ids_.allocate();
    heal();
}
void Simulation::clearActions() {
    state_.player.pendingCast.reset();
    stopChannel(state_.player);
    auto &p = state_.player;
    p.route.clear();
    p.attackTarget = {};
    p.throwAttack = p.leftHandAttack = false;
    p.castTime = p.meleeTime = p.hitTime = 0;
    p.weaponAttack.reset();
    p.attackPosition.reset();
    p.moving = false;
    p.runningNow = false;
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
    state_.player.pos = state_.player.previous = grid.walkable(spawn) ? spawn : grid.nearest(spawn);
    if (!state_.area.initialized) {
        state_.area.pendingSpawns.assign(monsters.begin(), monsters.end());
        state_.area.initialized = true;
    }
    activateMonsters();
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
    p.hp = characterStats_.maxLife;
    p.mana = characterStats_.maxMana;
    p.stamina = characterStats_.maxStamina;
    p.healing.clear();
    p.manaRestoration.clear();
    // NPC healing cures ailments; it does not dispel beneficial item states.
    // D2MOO SUNITNPC_HealPlayer removes poison/freeze/curable states only.
    p.chill = 0;
    p.poisonRemaining = p.poisonPerSecond = 0;
    p.webSlowRemaining = 0;
    p.webSlowPercent = 0;
    p.webSource = {};
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
        enemy.maxHp = monsterDefinition(enemy.kind).maxLife;
        if (monsterNormalCombat_)
            if (auto combat = monsterNormalCombat_(enemy.identity, area.region)) {
                enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                     (enemy.combatRandom >> 32);
                enemy.maxHp = float(combat->minLife +
                    uint32_t(enemy.combatRandom) % unsigned(combat->maxLife - combat->minLife + 1));
            }
        enemy.hp = enemy.maxHp;
        if (enemy.kind == MonsterKind::FoulCrowNest && monsterAi_)
            if (auto ai = monsterAi_(enemy); ai && ai->kind == MonsterAiKind::FoulCrowNest)
                enemy.aiWait = float(ai->params[0]) / 25.f;
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
                requestAttack(intent);
            else if constexpr (std::is_same_v<T, DebugKill>) {
                if (!state_.player.dead)
                    if (auto enemy = findEnemy(intent.target);
                        enemy && enemy->hp > 0 && (intent.ignoreActivation || active(enemy->pos)))
                        damageEnemy(*enemy, enemy->hp, state_.player.id, 0, intent.ignoreActivation);
            }
            else if constexpr (std::is_same_v<T, ToggleRun>)
                state_.player.running = !state_.player.running;
            else if constexpr (std::is_same_v<T, StopMoving>)
                stopWalking();
            else if constexpr (std::is_same_v<T, StopChannel>)
                stopChannel(state_.player);
        },
        command);
}
void Simulation::combatEffectsChanged(std::span<const RemovedCombatEffect> removed) {
    if (combatEffectsChanged_) combatEffectsChanged_();
    for (const auto &entry : removed)
        if (entry.effect.spec.restoreStaminaOnRemoval)
            state_.player.stamina = float(characterStats_.maxStamina);
}
void Simulation::tick(float dt, Vec keyboard, bool forceRun) {
    if (!grid_ || dt <= 0)
        return;
    auto &p = state_.player;
    forceRun_ = forceRun;
    p.previous = p.pos;
    ++state_.frame;
    state_.time += dt;
    if (auto removed = p.combatEffects.expire(state_.frame); !removed.empty())
        combatEffectsChanged(removed);
    advanceSkillCasting(p, dt, keyboard.length() > .1f);
    advanceWeaponAttack();
    p.castTime = std::max(0.f, p.castTime - dt);
    p.hitTime = std::max(0.f, p.hitTime - dt);
    p.chill = std::max(0.f, p.chill - dt);
    p.webSlowRemaining = std::max(0.f, p.webSlowRemaining - dt);
    if (p.webSlowRemaining == 0) {
        p.webSlowPercent = 0;
        p.webSource = {};
    }
    p.moving = false;
    if (p.dead)
        p.deathTime += dt;
    for (auto &e : state_.area.enemies) {
        if (!active(e.pos))
            continue;
        e.hitFlash = std::max(0.f, e.hitFlash - dt);
        if (e.hp <= 0)
            e.deathAge += dt;
        else if (e.poisonRemaining > 0) {
            const float elapsed = std::min(dt, e.poisonRemaining);
            e.poisonRemaining -= elapsed;
            damageEnemy(e, e.poisonPerSecond * elapsed, e.poisonSource, 0, false,
                        MonsterDamageType::Poison, true, e.poisonPlayerEffects);
            if (e.poisonRemaining <= 0) e.poisonPerSecond = 0;
        }
        if (e.hp > 0 && e.openWoundsRemaining > 0) {
            const float elapsed = std::min(dt, e.openWoundsRemaining);
            e.openWoundsRemaining -= elapsed;
            damageEnemy(e, e.openWoundsPerSecond * elapsed, e.openWoundsSource, 0, false,
                        MonsterDamageType::Physical, true, e.openWoundsPlayerEffects);
            if (e.openWoundsRemaining <= 0) e.openWoundsPerSecond = 0;
        }
    }
    if (!p.dead) {
        updatePotions(dt);
        if (p.poisonRemaining > 0) {
            const float elapsed = std::min(dt, p.poisonRemaining);
            p.hp = std::max(1.f, p.hp - elapsed * p.poisonPerSecond);
            p.poisonRemaining -= elapsed;
            if (p.poisonRemaining <= 0) p.poisonPerSecond = 0;
        }
        updatePlayer(dt, keyboard);
        activateMonsters();
        updateMonsterEnchantments();
        updateMonsters(dt);
    }
    updateMissiles(dt);
    if (!p.dead && p.hp <= 0) {
        p.dead = true;
        combatEffectsChanged(p.combatEffects.onDeath(EffectUnitKind::Player));
        stopChannel(p);
        p.pendingCast.reset();
        p.weaponAttack.reset();
        p.attackPosition.reset();
        p.meleeTime = 0;
        p.castTime = 0;
        p.healing.clear();
        p.manaRestoration.clear();
        p.chill = 0;
        p.poisonRemaining = p.poisonPerSecond = 0;
        p.route.clear();
        p.attackTarget = {};
        p.throwAttack = false;
        p.leftHandAttack = false;
        state_.message = "You have died. Press Ctrl+R to return.";
        emit(PlayerDied{p.id});
    }
    if (!p.dead && state_.area.pendingSpawns.empty() && !state_.area.enemies.empty() &&
        state_.area.kills == int(state_.area.enemies.size()))
        state_.message = "Area cleared. Ctrl+F2: travel onward. Ctrl+R: repopulate the area.";
    for (auto &e : state_.area.effects)
        e.age += dt;
    std::erase_if(state_.area.effects, [](const Effect &e) { return e.age >= e.duration; });
}
} // namespace d2x
