#pragma once
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/skills/spec.hpp"
#include <span>
#include <functional>

namespace d2x {
struct MonsterMissileCast {
    SkillCastSpec skill;
    MonsterDamageType element;
    bool killOnHit = true;
};
class Simulation {
    friend class GameSession;
    EntityIds &ids_;
    uint64_t unitRandom_ = 0; // Initialized by GameSession before spawning.
    const Grid *grid_ = nullptr; // Borrowed from GameSession's stable region storage.
    const RoomLayout *rooms_ = nullptr;
    std::map<int, MissileCollisionRule> missileCollisions_;
    bool missilePathClear(int missileId, Vec from, Vec to) const;
    bool clipMissilePath(int missileId, Vec from, Vec &to) const;
    bool safeZone_ = false;
    bool forceRun_ = false;
    WorldState state_;
    EquipmentStats equipmentStats_;
    CharacterAttributes characterStats_;
    std::function<CharacterAttributes()> hirelingAttributes_;
    int hirelingBossDamagePercent_ = 100;
    float hurtHireling(float amount, MonsterDamageType type, bool hitRecovery = true, bool alreadyMitigated = false);
    void recoverHireling(float damage, int hitClass = 0);
    float hirelingIncomingDamage(const Enemy &enemy, float damage) const;
    std::function<std::pair<int, bool>(const Enemy &)> monsterHitProperties_;
    Vec monsterTargetPosition(const Enemy &enemy) const;
    std::optional<std::pair<bool, float>> hostileMissileTarget(const Missile &missile, Vec to) const;
    std::function<void()> combatEffectsChanged_;
    void combatEffectsChanged(std::span<const RemovedCombatEffect> removed);
    void triggerCombatEffects(PlayerState &player, CombatEffectEvent event, Enemy &other);
    int resistancePenalty_ = 0;
    std::function<void(EntityId, bool)> wearEquipment_;
    std::function<bool(EntityId, bool)> spendProjectile_;
    std::function<bool(EntityId, bool)> canSpendProjectile_;
    std::function<std::optional<WeaponAttackTiming>(const WeaponDamage &, bool, bool)> attackTiming_;
    std::function<SkillCastSpec(int, int)> resolveMissileSkill_;
    std::function<int(const Enemy &)> monsterSize_;
    std::function<std::optional<MonsterAccuracy>(const Enemy &, RegionId, int)> monsterAccuracy_;
    std::function<std::optional<MonsterDefense>(const Enemy &, RegionId)> monsterDefense_;
    std::function<std::optional<float>(const Enemy &)> monsterWalkSpeed_;
    std::function<std::optional<float>(const Enemy &, int)> monsterMoveSpeed_;
    std::function<std::optional<MonsterNormalCombat>(const MonsterIdentity &, RegionId)> monsterNormalCombat_;
    std::function<std::optional<int>(const Enemy &, RegionId)> monsterCriticalChance_;
    std::function<std::optional<int>(const Enemy &, RegionId)> monsterDamageRegen_;
    std::function<int(const Enemy &)> monsterDrain_;
    int lifeStealDivisor_ = 1, manaStealDivisor_ = 1;
    std::function<std::optional<int>(const Enemy &, RegionId, MonsterDamageType)> monsterResistance_;
    std::function<int()> coldPierce_;
    std::function<std::optional<bool>(const Enemy &)> monsterFreezable_;
    int monsterFreezeDivisor_ = 1;
    std::function<std::optional<MonsterAiProfile>(const Enemy &)> monsterAi_;
    std::function<bool(RegionId)> zombieForcedPursuit_;
    std::function<std::optional<float>(const MonsterIdentity &)> monsterGetHitDuration_;
    std::function<std::optional<float>(const Enemy &)> monsterDeathDuration_;
    std::function<std::optional<float>(const Enemy &)> monsterSkill2Duration_;
    std::function<std::optional<float>(const Enemy &)> monsterResurrectionDuration_;
    std::function<std::optional<MonsterAttackTiming>(const Enemy &, int)> monsterAttackTiming_;
    std::function<std::optional<MonsterProjectile>(const Enemy &, int)> monsterProjectile_;
    std::function<std::optional<MonsterSpell>(const Enemy &, int)> monsterSpell_;
    std::function<std::optional<MonsterResurrection>(const Enemy &)> monsterResurrection_;
    std::function<std::optional<MonsterNest>(const Enemy &)> monsterNest_;
    std::function<std::optional<MonsterWeb>(const Enemy &)> monsterWeb_;
    std::vector<GameEvent> events_;
    Enemy *findEnemy(EntityId id);
    float hurtPlayer(float amount, MonsterDamageType type);
    AttackElements rollAttackElements(EntityId weapon, const CombatModifiers *modifiers = nullptr,
                                      const SkillCastSpec *skill = nullptr, uint64_t *randomState = nullptr);
    void resolveWeaponHit(Enemy &enemy, float physical, EntityId source,
                          const AttackElements &elements);
    void moveTo(Vec target);
    void requestAttack(const Attack &attack);
    const WeaponDamage *attackWeapon(bool thrown, bool leftHand) const;
    bool meleeReach(const Enemy &enemy, const WeaponDamage &weapon) const;
    bool beginWeaponAttack(Vec aim, EntityId target, const WeaponDamage &weapon, bool thrown, bool leftHand);
    bool beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target);
    void advanceWeaponAttack();
    bool firePhysicalProjectile(Vec target, const WeaponDamage &weapon, bool thrown,
                                const SkillCastSpec *skill = nullptr);
    void advancePhysicalMissile(Missile &missile, float dt, std::vector<Missile> &spawned);
    void resolveMissileImpact(const Missile &missile, std::vector<Missile> &spawned, Enemy *direct = nullptr);
    void advanceGroundTargetedMissile(Missile &missile, float dt, std::vector<Missile> &spawned);
    void advancePoisonCloud(Missile &missile, float dt);
    void applyEnemyPoison(Enemy &enemy, float rate, float duration, EntityId source, bool playerKillEffects);
    bool beginSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target, bool teleportAllowed,
                      int staticFieldMinimum, EntityId enemy = {});
    void releaseSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target,
                 int staticFieldMinimum, bool consumeMana = true);
    void advanceSkillCasting(PlayerState &player, float dt, bool moving);
    static void stopChannel(PlayerState &player);
    void damageEnemy(Enemy &enemy, float amount, EntityId source, float chill = 0,
                     bool ignoreActivation = false,
                     MonsterDamageType type = MonsterDamageType::Physical,
                     bool alreadyMitigated = false, bool playerKillEffects = true,
                     bool freezeHit = false);
    void meleeDamage(Enemy &enemy, const WeaponDamage &weapon);
    void updatePotions(float dt);
    void updatePlayer(float dt, Vec keyboard);
    void updateMonsters(float dt);
    void updateMonsterEnchantments();
    void applyMonsterEnchantmentHit(Enemy &enemy, bool hitHireling = false);
    void launchMonsterEnchantmentMissiles(Enemy &enemy, int missileId);
    void advanceHostileElementMissile(Missile &missile, float dt);
    bool tryMonsterTeleport(Enemy &enemy);
    std::function<std::optional<MonsterMissileCast>(int, int)> monsterSpecialMissile_;
    bool handleMonsterSpecialAi(Enemy &enemy, const MonsterAiProfile &ai,
                                float distance, bool clear);
    void beginMonsterAttack(Enemy &enemy, int forcedMode = 0);
    void resolveMonsterAttack(Enemy &enemy, int modeOverride = 0, bool projectile = false,
                               bool hitHireling = false);
    void launchMonsterProjectile(Enemy &enemy);
    void replicateMonsterMissile(const Enemy &enemy, Missile missile);
    std::set<int> noMultiShotMissiles_, unspreadMultiShotMissiles_;
    void launchMonsterSpell(Enemy &enemy);
    void resolveMonsterSpell(Enemy &enemy, const Missile &missile, bool hitHireling = false);
    void resolveMonsterResurrection(Enemy &enemy);
    std::optional<MonsterSpawn> nestSpawn(Enemy &enemy,
                                         std::span<const MonsterSpawn> queued);
    void activateSpiderWeb(Enemy &enemy);
    void leaveSpiderWeb(Enemy &enemy, float moved);
    void applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode,
                                bool hitHireling = false);
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
    void tick(float dt, Vec keyboard, bool forceRun = false);
    AreaState leaveArea();
    void enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, bool safeZone, AreaState area,
                   std::span<const MonsterSpawn> monsters, std::optional<Vec> coordinateOffset = {});
    void restartArea(Vec spawn, std::span<const MonsterSpawn> monsters);
    void heal();
    void applyPotion(const PotionDefinition &potion);
    void stopWalking();
};
} // namespace d2x
