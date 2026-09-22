#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <deque>

namespace d2x {
struct Restoration {
    float remaining, rate;
};
struct PlayerState {
    EntityId id;
    Vec pos, previous, look{1, 0};
    std::deque<Vec> route;
    float hp = 250, mana = 150, stamina = 100;
    float castTime = 0, spinTime = 0, leapTime = 0, hitTime = 0, deathTime = 0, meleeTime = 0;
    Vec leapStart, leapEnd;
    std::array<float, skillCount> cooldown{};
    std::deque<Restoration> healing, manaRestoration;
    float staminaBoost = 0;
    EntityId attackTarget;
    Skill lastSkill = Skill::Fireball;
    bool running = true, moving = false, dead = false;
};
struct Enemy {
    EntityId id;
    MonsterKind kind = MonsterKind::Fallen;
    MonsterIdentity identity;
    Vec pos;
    float hp = 100, chill = 0, attack = 0;
    float stun = 0, deathAge = 0, hitFlash = 0, rethink = 0;
    std::deque<Vec> route;
};
struct Missile {
    EntityId id, owner;
    Vec pos, velocity;
    float remaining = 2;
    Skill skill = Skill::Fireball;
};
struct Effect {
    Vec pos;
    Skill skill;
    float age = 0, duration = .8f;
};
// Area combat state survives travel. Ground item ownership lives in session InventoryState.
struct AreaState {
    RegionId region = RegionId::Encampment;
    std::vector<Enemy> enemies;
    std::vector<MonsterSpawn> pendingSpawns;
    std::vector<Missile> missiles;
    std::vector<Effect> effects;
    int kills = 0;
    bool initialized = false;
};
// Player combat state survives travel. GameSession separately owns inventory/container state.
struct WorldState {
    uint32_t mapSeed = 210;
    PopulationSettings population;
    PlayerState player;
    AreaState area;
    float time = 0;
    std::string message;
};
} // namespace d2x
