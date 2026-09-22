#pragma once
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include <span>

namespace d2x {
class Simulation {
    friend class GameSession;
    friend class SkillSystem;
    EntityIds &ids_;
    const Grid *grid_ = nullptr; // Borrowed from GameSession's stable region storage.
    const RoomLayout *rooms_ = nullptr;
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
    void activateMonsters();
    void clearActions();

  public:
    explicit Simulation(EntityIds &ids);
    const WorldState &state() const { return state_; }
    bool active(Vec position) const { return rooms_ && rooms_->nearby(state_.player.pos, position); }
    std::span<const GameEvent> events() const { return events_; }
    void beginTick() { events_.clear(); }
    template <class Event> void emit(Event event) {
        events_.emplace_back(std::in_place_type<Event>, std::move(event));
    }
    void execute(const GameCommand &command);
    void tick(float dt, Vec keyboard);
    AreaState leaveArea();
    void enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, AreaState area,
                   std::span<const MonsterSpawn> monsters);
    void restartArea(Vec spawn, std::span<const MonsterSpawn> monsters);
    void heal();
    void applyPotion(const PotionDefinition &potion);
    void stopWalking();
};
} // namespace d2x
