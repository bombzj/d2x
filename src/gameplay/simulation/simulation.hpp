#pragma once
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/combat/damage_resolution.hpp"
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
    std::function<CharacterAttributes()> hirelingAttributes_;
    int hirelingBossDamagePercent_ = 100;
    void recoverHireling(float damage, int hitClass = 0);

    std::function<std::pair<int, bool>(const Enemy &)> monsterHitProperties_;
    Vec monsterTargetPosition(const Enemy &enemy) const;
    CombatUnit combatUnit(EntityId id);
    std::vector<CombatUnit> combatUnits();
    EntityId controllingPlayer(EntityId id) const;
    Relation relation(EntityId first, EntityId second) const;
    EntityId chooseTarget(EntityId actor, float range = 100);
    Vec unitPosition(EntityId id) const;
    int unitResistance(const CombatUnit &unit, MonsterDamageType type) const;
    float incomingDamage(EntityId attacker, EntityId defender, float amount) const;
    ResolvedDamage resolveIncoming(EntityId attacker, const CombatUnit &defender, float amount, MonsterDamageType type);
    float dealDamage(const DamageRequest &request);
    void recoverUnit(EntityId defender, EntityId attacker, float damage, bool elemental = false);
    void restoreUnit(EntityId id, float life, float mana = 0);
    void applyPoison(EntityId defender, float rate, float duration, EntityId source);
    void applyChill(EntityId defender, float duration, bool freeze = false);
    void applyWeb(EntityId defender, float duration, int percent);
    void onMonsterDamaged(Enemy &enemy, const DamageRequest &request, float dealt);
    std::optional<std::pair<EntityId, float>> missileTarget(const Missile &missile, Vec to);
    void updateCompanions(float dt);
    std::function<bool(const Enemy &)> corpseSelectable_;
    bool usableCorpse(EntityId id) const;
    EntityId corpseNear(Vec target) const;
    bool summonFromCorpse(PlayerState &owner, const SkillCastSpec &skill, EntityId corpse);
    void relocateCompanions(EntityId owner, Vec destination, EntityId only = {});
    void enforceSummonLimit(EntityId owner, int skill, int limit);
    std::function<void()> combatEffectsChanged_;
    void combatEffectsChanged(std::span<const RemovedCombatEffect> removed);
    void triggerCombatEffects(EntityId target, CombatEffectEvent event, EntityId other);
    int resistancePenalty_ = 0;
    std::function<void(EntityId, bool)> wearEquipment_;
    std::function<bool(EntityId, bool)> spendProjectile_;
    std::function<bool(EntityId, bool)> canSpendProjectile_;
    std::function<std::optional<WeaponAttackTiming>(const WeaponDamage &, bool, bool)> attackTiming_;
    std::function<SkillCastSpec(EntityId, int, int)> resolveMissileSkill_;
    std::function<int(const Enemy &)> monsterSize_;
    std::function<MovementCollisionRule(const Enemy &)> monsterMovementRule_;
    std::function<MovementCollisionRule(const Enemy &)> monsterSpawnRule_;
    MovementCollisionRule movementRule(const Enemy &enemy) const {
        return monsterMovementRule_ ? monsterMovementRule_(enemy) : MovementCollisionRule{0xffff, -1};
    }
    MovementCollisionRule spawnRule(const Enemy &enemy) const {
        return monsterSpawnRule_ ? monsterSpawnRule_(enemy) : MovementCollisionRule{0xffff, -1};
    }
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
    std::function<int(EntityId)> coldPierce_;
    std::function<std::optional<bool>(const Enemy &)> monsterFreezable_;
    int monsterFreezeDivisor_ = 1;
    int monsterColdDivisor_ = 1;
    std::function<int(const CombatUnit &)> unitColdEffect_;
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
    AttackElements rollAttackElements(EntityId weapon, const CombatModifiers *modifiers = nullptr,
                                      const SkillCastSpec *skill = nullptr, uint64_t *randomState = nullptr);
    void resolveWeaponHit(EntityId defender, float physical, EntityId source,
                          const AttackElements &elements);
    void moveTo(Vec target);
    void requestAttack(const Attack &attack);
    const WeaponDamage *attackWeapon(bool thrown, bool leftHand) const;
    bool meleeReach(EntityId defender, const WeaponDamage &weapon) const;
    bool beginWeaponAttack(Vec aim, EntityId target, const WeaponDamage &weapon, bool thrown, bool leftHand);
    bool beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target);
    void advanceWeaponAttack();
    bool firePhysicalProjectile(Vec target, const WeaponDamage &weapon, bool thrown,
                                const SkillCastSpec *skill = nullptr);
    void advancePhysicalMissile(Missile &missile, float dt, std::vector<Missile> &spawned);
    void resolveMissileImpact(const Missile &missile, std::vector<Missile> &spawned, EntityId direct = {});
    void advanceGroundTargetedMissile(Missile &missile, float dt, std::vector<Missile> &spawned);
    void advancePoisonCloud(Missile &missile, float dt);

    bool beginSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target, bool teleportAllowed,
                      int staticFieldMinimum, EntityId enemy = {});
    void releaseSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target,
                 int staticFieldMinimum, bool consumeMana = true, EntityId targetUnit = {});
    void advanceSkillCasting(PlayerState &player, float dt, bool moving);
    static void stopChannel(PlayerState &player);
    void damageEnemy(Enemy &enemy, float amount, EntityId source, float chill = 0,
                     bool ignoreActivation = false,
                     MonsterDamageType type = MonsterDamageType::Physical,
                     bool alreadyMitigated = false,
                     bool freezeHit = false);
    void meleeDamage(EntityId defender, const WeaponDamage &weapon);
    void updatePotions(float dt);
    void updatePlayer(float dt, Vec keyboard);
    void updateMonsters(float dt);
    void updateMonsterEnchantments();
    void applyMonsterEnchantmentHit(Enemy &enemy, EntityId defender = {}, bool recovery = true);
    void launchMonsterEnchantmentMissiles(Enemy &enemy, int missileId);
    bool tryMonsterTeleport(Enemy &enemy);
    std::function<std::optional<MonsterMissileCast>(int, int)> monsterSpecialMissile_;
    bool handleMonsterSpecialAi(Enemy &enemy, const MonsterAiProfile &ai,
                                float distance, bool clear);
    void beginMonsterAttack(Enemy &enemy, int forcedMode = 0);
    void resolveMonsterAttack(Enemy &enemy, int modeOverride = 0, bool projectile = false,
                               EntityId defender = {});
    void launchMonsterProjectile(Enemy &enemy);
    void replicateMonsterMissile(const Enemy &enemy, Missile missile);
    std::set<int> noMultiShotMissiles_, unspreadMultiShotMissiles_;
    void launchMonsterSpell(Enemy &enemy);
    void resolveMonsterSpell(Enemy &enemy, const Missile &missile, EntityId defender = {});
    void resolveMonsterResurrection(Enemy &enemy);
    std::optional<MonsterSpawn> nestSpawn(Enemy &enemy,
                                         std::span<const MonsterSpawn> queued);
    void activateSpiderWeb(Enemy &enemy);
    void leaveSpiderWeb(Enemy &enemy, float moved);
    void applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode,
                                EntityId defender = {}, bool recovery = true);
    void updateMissiles(float dt);
    void launchFrozenOrb(PlayerState &player, const SkillCastSpec &skill, Vec target);
    void advanceFrozenOrb(Missile &missile, std::vector<Missile> &spawned);
    void spawnFrozenOrbBolt(const Missile &orb, Vec target, bool nova, std::vector<Missile> &spawned);
    float frozenOrbColdDuration(EntityId attacker, const CombatUnit &target, int frames) const;
    void spawnEnemies(std::span<const MonsterSpawn> spawns);
    void activateMonsters();
    void clearActions();

  public:
    explicit Simulation(EntityIds &ids);
    const WorldState &state() const { return state_; }
    bool canAttack(EntityId attacker, EntityId defender) const;
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
