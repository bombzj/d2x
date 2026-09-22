#pragma once
#include "commands.hpp"
#include "events.hpp"
#include "potions.hpp"
#include "state.hpp"
#include "world/navigation.hpp"
#include <span>

namespace d2x {
class Simulation {
    friend class GameSession;
    EntityIds &ids_;
    const Grid *grid_ = nullptr; // Borrowed from GameSession's stable region storage.
    WorldState state_;
    std::vector<GameEvent> events_;
    Enemy *findEnemy(EntityId id);
    void moveTo(Vec target);
    void attackEnemy(EntityId target);
    bool cast(Skill skill, Vec target);
    void damage(Vec pos, float radius, float amount, EntityId source, float chill = 0);
    void updatePotions(float dt);
    void updatePlayer(float dt, Vec keyboard);
    void updateMonsters(float dt);
    void updateMissiles(float dt);
    void spawnEnemies(std::span<const MonsterSpawn> spawns);
    void clearActions();

  public:
    explicit Simulation(EntityIds &ids);
    const WorldState &state() const { return state_; }
    std::span<const GameEvent> events() const { return events_; }
    void beginTick() { events_.clear(); }
    template <class Event> void emit(Event event) {
        events_.emplace_back(std::in_place_type<Event>, std::move(event));
    }
    void execute(const GameCommand &command);
    void tick(float dt, Vec keyboard);
    AreaState leaveArea();
    void enterArea(const Grid &grid, Vec spawn, AreaState area, std::span<const MonsterSpawn> monsters);
    void restartArea(Vec spawn, std::span<const MonsterSpawn> monsters);
    void heal();
    void applyPotion(const PotionDefinition &potion);
    void stopWalking();
};
} // namespace d2x
