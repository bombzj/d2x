#pragma once
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/skills/original.hpp"
#include <span>
#include <functional>

namespace d2x {
class Simulation {
    friend class GameSession;
    friend class SkillSystem;
    EntityIds &ids_;
    const Grid *grid_ = nullptr; // Borrowed from GameSession's stable region storage.
    const RoomLayout *rooms_ = nullptr;
    bool safeZone_ = false;
    WorldState state_;
    EquipmentStats equipmentStats_;
    CharacterAttributes characterStats_;
    std::function<void(EntityId, bool)> wearEquipment_;
    std::function<bool(EntityId, bool)> spendProjectile_;
    std::function<std::optional<MonsterAccuracy>(const Enemy &)> monsterAccuracy_;
    std::function<std::optional<MonsterDefense>(const Enemy &)> monsterDefense_;
    std::function<std::optional<float>(const Enemy &)> monsterWalkSpeed_;
    std::function<std::optional<MonsterNormalCombat>(const MonsterIdentity &)> monsterNormalCombat_;
    std::function<std::optional<MonsterAiProfile>(const Enemy &)> monsterAi_;
    std::vector<GameEvent> events_;
    Enemy *findEnemy(EntityId id);
    void moveTo(Vec target);
    void attackEnemy(EntityId target, bool thrown, bool leftHand);
    bool firePhysicalProjectile(const Enemy &enemy, const WeaponDamage &weapon, bool thrown);
    bool cast(Skill skill, Vec target);
    bool castOriginal(const OriginalSkillCast &skill, Vec target, bool teleportAllowed,
                      int staticFieldMinimum);
    void damage(Vec pos, float radius, float amount, EntityId source, float chill = 0);
    void damageEnemy(Enemy &enemy, float amount, EntityId source, float chill = 0,
                     bool ignoreActivation = false);
    void meleeDamage(Enemy &enemy, bool leftHand = false);
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
    void enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, bool safeZone, AreaState area,
                   std::span<const MonsterSpawn> monsters);
    void restartArea(Vec spawn, std::span<const MonsterSpawn> monsters);
    void heal();
    void applyPotion(const PotionDefinition &potion);
    void stopWalking();
};
} // namespace d2x
