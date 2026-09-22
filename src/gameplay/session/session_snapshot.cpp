#include "gameplay/session/session.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace d2x {
namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(std::string("Invalid save: ") + reason);
}
void scalar(float value, float minimum = 0, float maximum = 1.e9f) {
    require(std::isfinite(value) && value >= minimum && value <= maximum, "numeric state out of range");
}
void position(Vec value, const Grid &grid, bool walkable = false) {
    scalar(value.x, 0, float(grid.width));
    scalar(value.y, 0, float(grid.height));
    require(value.x < grid.width && value.y < grid.height, "position outside region");
    if (walkable)
        require(grid.walkable(value), "position on blocked ground");
}
void skill(Skill value) {
    require(int(value) >= 0 && size_t(value) < skillCount, "unknown skill");
}
void route(const std::deque<Vec> &points, const Grid &grid) {
    require(points.size() <= 65536, "route too long");
    for (auto point : points)
        position(point, grid, true);
}
} // namespace
SessionSnapshot GameSession::snapshot() const {
    SessionSnapshot result;
    result.contentFingerprint = contentFingerprint_;
    result.nextEntityId = ids_.cursor();
    for (const auto &region : regions_)
        result.maps.push_back(region.definition.mapPath);
    result.world = state();
    result.inactiveAreas = inactiveAreas_;
    // A moved-from area is an implementation detail, not a second saved copy.
    result.inactiveAreas.at(current_) = {};
    result.inactiveAreas.at(current_).region = region().definition.id;
    result.inventory = inventory_.state();
    result.containers = playerContainers_;
    result.loot = loot_.snapshot();
    // Automatic walking to a transient pickup/interaction does not outlive that request.
    if (pickup_.id || pendingInteraction_ || pendingExit_) {
        result.world.player.route.clear();
        result.world.player.attackTarget = {};
        result.world.player.moving = false;
    }
    validateSnapshot(result);
    return result;
}
int GameSession::validateSnapshot(const SessionSnapshot &s) const {
    require(s.world.mapSeed == state().mapSeed, "map seed differs; reopen with --load or --map-seed");
    require(s.world.population.difficulty == state().population.difficulty, "map difficulty differs");
    require(s.contentFingerprint == contentFingerprint_, "MPQ content or gameplay rules differ");
    require(s.world.population.difficulty >= 0 && s.world.population.difficulty <= 2,
            "population difficulty");
    require(s.maps.size() == regions_.size() && s.inactiveAreas.size() == regions_.size(), "region count");
    int current = -1;
    for (size_t i = 0; i < regions_.size(); ++i) {
        require(s.maps[i] == regions_[i].definition.mapPath,
                "map configuration differs; check --level/--variant/--preset");
        if (s.world.area.region == regions_[i].definition.id)
            current = int(i);
    }
    require(current >= 0 && s.nextEntityId > 0, "active region or ID cursor");
    std::set<EntityId> allocated;
    auto registerId = [&](EntityId id) {
        require(bool(id) && id.value < s.nextEntityId && allocated.insert(id).second,
                "duplicate or invalid entity ID");
    };
    const auto &player = s.world.player;
    registerId(player.id);
    require(player.id == state().player.id, "player identity");
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            registerId(object.id);
    const auto &grid = regions_[current].map.grid;
    position(player.pos, grid, player.leapTime <= 0);
    position(player.previous, grid);
    scalar(player.look.x, -1.001f, 1.001f);
    scalar(player.look.y, -1.001f, 1.001f);
    route(player.route, grid);
    scalar(player.hp, 0, playerRules().maxLife);
    scalar(player.mana, 0, playerRules().maxMana);
    scalar(player.stamina, 0, playerRules().maxStamina);
    for (auto timer : {player.castTime, player.spinTime, player.leapTime, player.hitTime, player.deathTime,
                       player.meleeTime, player.staminaBoost})
        scalar(timer);
    for (auto cooldown : player.cooldown)
        scalar(cooldown);
    require(player.dead == (player.hp == 0), "player death state");
    skill(player.lastSkill);
    // Inactive leap endpoints may belong to a previously visited region. Only an
    // active leap uses them for movement; otherwise require finite values only.
    for (auto point : {player.leapStart, player.leapEnd}) {
        scalar(point.x);
        scalar(point.y);
        if (player.leapTime > 0)
            position(point, grid);
    }
    auto restorations = [&](const auto &queue) {
        require(queue.size() <= 65536, "restoration queue too large");
        for (const auto &entry : queue) {
            scalar(entry.remaining);
            scalar(entry.rate);
            require(entry.rate > 0, "restoration rate");
        }
    };
    restorations(player.healing);
    restorations(player.manaRestoration);
    scalar(s.world.time);
    require(s.world.message.size() <= 4096, "message too large");
    std::set<EntityId> deadEnemies;
    auto validateArea = [&](const AreaState &area, size_t index, bool active) {
        require(area.region == regions_[index].definition.id, "area identity");
        require(area.enemies.size() <= 65536 && area.missiles.size() <= 65536 && area.effects.size() <= 65536,
                "too many actors");
        require(area.initialized || (area.enemies.empty() && area.pendingSpawns.empty() &&
                                     area.missiles.empty() && area.effects.empty() && area.kills == 0),
                "uninitialized area has actors");
        if (active)
            require(area.initialized, "active area is uninitialized");
        const auto &areaGrid = regions_[index].map.grid;
        int dead = 0;
        std::set<std::string> spawnKeys;
        auto validateIdentity = [&](MonsterKind kind, const MonsterIdentity &identity) {
            require(int(kind) >= 0 && int(kind) < int(MonsterKind::Count), "monster kind");
            auto source = monsterContent_.find(identity.monster);
            require(source && source->hostile(), "original monster identity");
            require(kind == monsterImplementation(identity.monster).kind, "monster implementation");
            require(int(identity.rank) >= 0 && int(identity.rank) <= int(MonsterRank::Boss), "monster rank");
            require(identity.origin == SpawnOrigin::Density || identity.origin == SpawnOrigin::Preset,
                    "monster spawn origin");
            require(identity.group > 0 && !identity.spawnKey.empty() && identity.spawnKey.size() <= 256 &&
                        spawnKeys.insert(identity.spawnKey).second,
                    "monster spawn key/group");
            if (!identity.superUnique.empty()) {
                auto unique = monsterContent_.superUnique(identity.superUnique);
                require(unique && unique->monster == identity.monster &&
                            identity.rank == MonsterRank::SuperUnique,
                        "super unique identity");
            } else
                require(identity.rank != MonsterRank::SuperUnique, "missing super unique identity");
        };
        require(area.pendingSpawns.size() <= 65536, "too many deferred spawns");
        std::set<uint32_t> pendingGroups;
        for (const auto &spawn : area.pendingSpawns) {
            validateIdentity(spawn.kind, spawn.identity);
            position(spawn.position, areaGrid, true);
            pendingGroups.insert(spawn.identity.group);
        }
        for (const auto &enemy : area.enemies) {
            registerId(enemy.id);
            require(!pendingGroups.contains(enemy.identity.group), "partially instantiated monster group");
            validateIdentity(enemy.kind, enemy.identity);
            position(enemy.pos, areaGrid, true);
            scalar(enemy.hp, 0, monsterDefinition(enemy.kind).maxLife);
            for (auto timer : {enemy.chill, enemy.stun, enemy.deathAge, enemy.hitFlash})
                scalar(timer);
            scalar(enemy.attack, -1.e9f);
            scalar(enemy.rethink, -1.e9f);
            route(enemy.route, areaGrid);
            if (enemy.hp == 0) {
                ++dead;
                deadEnemies.insert(enemy.id);
            }
        }
        require(area.kills == dead, "area kill count");
        for (const auto &missile : area.missiles) {
            registerId(missile.id);
            require(missile.owner == player.id, "missile owner");
            position(missile.pos, areaGrid);
            scalar(missile.velocity.x, -100000, 100000);
            scalar(missile.velocity.y, -100000, 100000);
            scalar(missile.remaining);
            skill(missile.skill);
        }
        for (const auto &effect : area.effects) {
            // A missile impact may be just outside the collision grid at a map edge.
            scalar(effect.pos.x, -100000, 100000);
            scalar(effect.pos.y, -100000, 100000);
            skill(effect.skill);
            scalar(effect.age);
            scalar(effect.duration);
            require(effect.age < effect.duration, "expired effect");
        }
    };
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (int(i) == current) {
            const auto &unused = s.inactiveAreas[i];
            require(!unused.initialized && unused.pendingSpawns.empty() && unused.enemies.empty() &&
                        unused.missiles.empty() && unused.effects.empty() && unused.kills == 0 &&
                        unused.region == s.world.area.region,
                    "duplicate active area");
            validateArea(s.world.area, i, true);
        } else
            validateArea(s.inactiveAreas[i], i, false);
    }
    if (player.attackTarget)
        require(std::any_of(s.world.area.enemies.begin(), s.world.area.enemies.end(),
                            [&](const Enemy &e) { return e.id == player.attackTarget; }),
                "attack target");
    inventory_.validateSnapshot(s.inventory, s.containers, player.id);
    for (const auto &[id, container] : s.inventory.containers)
        registerId(id);
    for (const auto &[id, item] : s.inventory.items) {
        registerId(id);
        if (auto ground = std::get_if<GroundLocation>(&item.location)) {
            auto region = std::find_if(regions_.begin(), regions_.end(),
                                       [&](const auto &r) { return r.definition.id == ground->region; });
            require(region != regions_.end(), "ground item region");
            position(ground->position, region->map.grid, true);
        }
    }
    for (auto id : s.loot.settled)
        require(bool(id) && id.value < s.nextEntityId &&
                    (!allocated.contains(id) || deadEnemies.contains(id)),
                "death settlement identity");
    for (auto id : deadEnemies)
        require(s.loot.settled.contains(id), "unsettled corpse");
    return current;
}
void GameSession::restore(SessionSnapshot s) {
    int current = validateSnapshot(s);
    // All allocation and validation precedes this no-throw commit.
    static_assert(std::is_nothrow_move_assignable_v<WorldState>);
    static_assert(std::is_nothrow_move_assignable_v<InventoryState>);
    simulation_.state_ = std::move(s.world);
    simulation_.grid_ = &regions_[current].map.grid;
    simulation_.rooms_ = &regions_[current].map.activation;
    simulation_.events_.clear();
    inventory_.state_ = std::move(s.inventory);
    inactiveAreas_.swap(s.inactiveAreas);
    playerContainers_ = s.containers;
    loot_.restore(std::move(s.loot));
    ids_.next_ = s.nextEntityId;
    current_ = current;
    pending_.clear();
    pickup_ = {};
    pendingInteraction_ = {};
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
    storage_ = {};
}
} // namespace d2x
