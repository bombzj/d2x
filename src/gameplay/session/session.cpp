#include "core/random.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "content/character/character_attributes.hpp"
#include "content/items/item_properties.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include "core/fingerprint.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace d2x {
GameSession::~GameSession() = default;
const WorldState &GameSession::state() const { return simulation_->state(); }
void GameSession::setRunning(bool running) { simulation_->state_.player.running = running; }
bool GameSession::usableCorpse(EntityId id) const { return simulation_->usableCorpse(id); }
Vec GameSession::combatPosition(EntityId id) const { return simulation_->unitPosition(id); }
bool GameSession::canAttack(EntityId actor, EntityId target) const { return simulation_->canAttack(actor, target); }
bool GameSession::active(Vec position) const { return simulation_->active(position); }
std::span<const GameEvent> GameSession::events() const { return simulation_->events(); }
namespace {
bool baseMonsterRank(MonsterRank rank) {
    return rank == MonsterRank::Normal || rank == MonsterRank::Minion;
}
} // namespace
GameSession::GameSession(Archives &archives, const WorldSelection &selection, int startRegion,
                         uint32_t sessionSeed, PopulationSettings population, std::string characterClass,
                         std::string characterName)
    : random_(initialRandom(sessionSeed)), content_(loadClassicData(archives)), worldContent_(archives),
            monsterContent_(archives, content_.tables.at("monstats")),
            simulation_(std::make_unique<Simulation>(ids_)), loot_(childRandom(random_)) {
    simulation_->state_.player.characterClass = std::move(characterClass);
    simulation_->missileCollisions_ = content_.missileCollisions;
    simulation_->missileReturnFire_ = content_.missileReturnFire;
    simulation_->freezeDeathState_ = content_.states.at("freeze").definition;
    simulation_->shatterDeathState_ = content_.states.at("shatter").definition;
    simulation_->uninterruptableState_ = content_.states.at("uninterruptable").definition.id;
    simulation_->attractState_ = content_.states.at("attract").definition.id;
    simulation_->preventHealState_ = content_.states.at("preventheal").definition.id;
    simulation_->noMultiShotMissiles_ = content_.noMultiShotMissiles;
    simulation_->unspreadMultiShotMissiles_ = content_.unspreadMultiShotMissiles;
    simulation_->state_.player.name = std::move(characterName);
    characterDefinition_ = definitionFor(state().player.characterClass);
    simulation_->state_.population = population;
    simulation_->hirelingBossDamagePercent_ = content_.hirelingBossDamagePercent.at(size_t(population.difficulty));
    simulation_->lifeStealDivisor_ = content_.lifeStealDivisor.at(size_t(population.difficulty));
    simulation_->manaStealDivisor_ = content_.manaStealDivisor.at(size_t(population.difficulty));
    simulation_->monsterDrain_ = [this](const Enemy &enemy) {
        for (const auto &monster : content_.monsters)
            if (monster.name == enemy.identity.monster)
                return monster.drain.at(size_t(state().population.difficulty));
        return 0;
    };
    simulation_->resistancePenalty_ = content_.resistancePenalty.at(size_t(population.difficulty));
    simulation_->state_.mapSeed = selection.seed;
    shrineRandom_ = childRandom(random_);
    inventory_.state_.creationRandom = childRandom(random_);
    visualRandom_ = childRandom(random_);
    cainRandom_ = childRandom(random_);
    simulation_->unitRandom_ = childRandom(random_);
    inventory_.groundPlacement_ = [this](const GroundLocation &origin, const GroundLocation &target) {
        if (origin.region != target.region) return false;
        const auto found = std::find_if(regions_.begin(), regions_.end(),
            [&](const Region &region) { return region.definition.id == target.region; });
        if (found == regions_.end()) return false;
        const auto &grid = found->map.grid;
        // D2MOO drop mask: WALL | OBJECT | DOOR | NO_PATH | PET (ITEM is in InventoryService).
        // The field ray checks WALL | DOOR, not character walking collision.
        if (!grid.collisionSegment(target.position, target.position, 0x3c01) ||
            !grid.collisionSegment(origin.position, target.position, 0x0801)) return false;
        // Path.cpp / COLLISION_SetMaskWithPattern: NO_PATH or PET presence is
        // the center cell for sizes 1/2, a five-cell cross for size 3. Corpses have neither.
        auto occupies = [&](Vec position, int size) {
            if (size <= 0) return false;
            const float distance = std::abs(std::floor(position.x) - std::floor(target.position.x)) +
                                   std::abs(std::floor(position.y) - std::floor(target.position.y));
            return distance <= (size == 3 ? 1.f : 0.f);
        };
        if (state().area.region == target.region) {
            const auto &player = state().player;
            if (!player.dead && occupies(player.pos, 2)) return false; // UNITS_GetUnitSizeX(PLAYER).
            if (player.hireling.active())
                for (const auto &[id, monster] : monsterContent_.monsters())
                    if (monster.index == player.hireling.classId &&
                        occupies(player.hireling.pos, monster.collisionSize)) return false;
        }
        const auto &area = areaState(int(found - regions_.begin()));
        for (const auto &enemy : area.enemies)
            if (enemy.hp > 0)
                if (const auto *monster = monsterContent_.find(enemy.identity.monster);
                    monster && occupies(enemy.pos, monster->collisionSize)) return false;
        for (const auto &object : found->objects)
            if (!object.npcClass.empty())
                if (const auto *monster = monsterContent_.find(object.npcClass);
                    monster && occupies(object.pos, monster->collisionSize)) return false;
        return true;
    };
    inventory_.itemProperties_ = [this](const ItemInstance &item) {
        auto identified = item;
        identified.identified = true; // Physical limits exist before identification.
        return resolveItemStats(content_, identified, 1);
    };
    for (const auto &record : content_.uniqueItems)
        if (record.carryOne)
            inventory_.singleCarryUniques_.insert(int32_t(record.row));
    playerContainers_ = inventory_.createPlayerContainers(state().player.id);
    refreshCharacter();
    simulation_->heal();
    createStarterEquipment();
    refreshCharacter();
    simulation_->wearEquipment_ = [this](EntityId weapon, bool defending) {
        auto result = inventory_.wearEquipment(playerContainers_, weapon, defending,
                                               simulation_->state_.player.combatRandom,
                                               simulation_->state_.player.weaponSet);
        if (!result || !result.changes.empty())
            publishInventory(std::move(result), {});
    };
    simulation_->combatEffectsChanged_ = [this] { refreshCharacter(); };
    simulation_->auraEligible_ = [this](const CombatUnit &unit, bool checkNoAura) {
        if (!unit.monster && !unit.hireling) return true;
        const MonsterRecord *monster = unit.monster ? monsterContent_.find(unit.monster->identity.monster) : nullptr;
        if (unit.hireling)
            for (const auto &[id, record] : monsterContent_.monsters())
                if (record.index == unit.hireling->classId) { monster = &record; break; }
        if (!monster || monster->npc) return false;
        const auto &stats = content_.tables.at("monstats");
        if (checkNoAura && stats.number(monster->sourceRow, "noAura").value_or(0)) return false;
        const auto &extra = content_.tables.at("monstats2");
        const auto identity = stats.value(monster->sourceRow, "MonStatsEx");
        for (size_t row = 0; row < extra.rows().size(); ++row)
            if (extra.value(row, "Id") == identity) return extra.number(row, "isAtt").value_or(0) != 0;
        return false;
    };
    simulation_->initializeNaturalElite_ = [this](Enemy &enemy, const Enemy *owner) {
        if (enemy.identity.enchantment || (owner ? owner->identity.rank != MonsterRank::Unique ||
            !owner->identity.enchantment : enemy.identity.rank != MonsterRank::Champion &&
            enemy.identity.rank != MonsterRank::Unique)) return;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->boss || monsterImplementation(record->id).substitute) return;
        auto identity = enemy.identity;
        identity.rank = MonsterRank::Normal;
        const auto base = resolvedMonsterCombat(identity, state().area.region);
        if (!base) throw std::runtime_error("Natural elite lacks original combat attributes: " + record->id);
        auto modifiers = owner
            ? inheritedMonsterEnchantment(content_, *record, *base, state().population.difficulty, *owner->identity.enchantment)
            : rollMonsterEnchantment(content_, *record, *base, state().population.difficulty,
                                     enemy.combatRandom, enemy.identity.rank, true, false, enemy.identity.championVariantAllowed);
        int64_t life = int64_t(enemy.maxHp * 256.f);
        life += life * modifiers.lifePercent / 100;
        life = life * modifiers.lifeScalePercent / 100;
        if (owner) enemy.identity.rank = MonsterRank::Minion;
        enemy.identity.enchantment = std::move(modifiers);
        enemy.hp = enemy.maxHp = float(std::max<int64_t>(1, life)) / 256.f;
        enemy.nextAuraFrame = state().frame;
    };
    simulation_->monsterHitProperties_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? std::pair{record->hitClass, record->primeEvil} : std::pair{0, false};
    };
    simulation_->hirelingAttributes_ = [this] {
        const auto merc = hirelingStats();
        CharacterAttributes result;
        result.maxLife = merc.base.life; result.defense = merc.base.defense;
        result.fireResist = merc.fireResist; result.coldResist = merc.coldResist;
        result.lightningResist = merc.lightningResist; result.poisonResist = merc.poisonResist;
        result.combat = merc.combat;
        return result;
    };
    simulation_->attackTiming_ = [this](const WeaponDamage &weapon, bool thrown, bool leftHand)
        -> std::optional<WeaponAttackTiming> {
        const auto action = thrown ? (leftHand ? BasicSkillAction::LeftHandThrow : BasicSkillAction::Throw) :
                                     (leftHand ? BasicSkillAction::LeftHandSwing : BasicSkillAction::Attack);
        const auto skill = std::find_if(content_.skills.skills.begin(), content_.skills.skills.end(),
            [action](const auto &entry) { return entry.second.basicAction == action; });
        if (skill == content_.skills.skills.end()) return std::nullopt;
        const auto &mode = skill->second.animationMode;
        auto found = content_.skills.attackTimings.find(characterAppearance() + mode + equipmentStats().animationClass);
        if (found == content_.skills.attackTimings.end()) return std::nullopt;
        const auto &data = found->second;
        const int skillRate = characterStats().combat.attackRate - (state().player.chill > 0 ? 50 : 0);
        return WeaponAttackTiming{mode, data.frames,
            effectiveAttackSpeed(data.speed, weapon.fasterAttack, weapon.baseSpeed, skillRate),
            data.actionFrame, attackStartingFrame(characterCode(), weapon.weaponClass, mode)};
    };
    simulation_->monsterSize_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? record->collisionSize : 0;
    };
    simulation_->monsterMovementRule_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? record->movementRule() : MovementCollisionRule{0xffff, -1};
    };
    simulation_->monsterSpawnRule_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? record->spawnRule() : MovementCollisionRule{0xffff, -1};
    };
    simulation_->state_.player.combatRandom = childRandom(simulation_->unitRandom_);
    simulation_->monsterAccuracy_ = [this](const Enemy &enemy, RegionId region, int mode)
        -> std::optional<MonsterAccuracy> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region)) {
            auto rating = mode == 2 ? combat->attack2Rating : combat->attack1Rating;
            return rating ? std::optional<MonsterAccuracy>{{combat->level, *rating}} : std::nullopt;
        }
        if (state().population.difficulty != 0 || !baseMonsterRank(enemy.identity.rank))
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->boss)
            return std::nullopt;
        auto rating = mode == 2 ? record->normalAttackRating2 : record->normalAttackRating;
        if (!rating) return std::nullopt;
        return MonsterAccuracy{record->normalLevel, *rating};
    };
    simulation_->monsterSpecialMissile_ = [this](int id, int rank) -> std::optional<MonsterMissileCast> {
        auto found = content_.monsterSpecialMissiles.find(id);
        if (found == content_.monsterSpecialMissiles.end()) return std::nullopt;
        return MonsterMissileCast{resolveSkill(found->second.spec, rank, {}),
            MonsterDamageType(found->second.element), found->second.killOnHit};
    };
    simulation_->resolveMissileSkill_ = [this](EntityId actor, int id, int rank) {
        if (actor != state().player.id) throw std::runtime_error("Skill attributes unavailable for this actor");
        const auto *entry = content_.skills.find(id);
        if (!entry || !entry->spell) throw std::runtime_error("Missing originating missile skill");
        return resolveSkill(*entry->spell, rank, state().player.skillRanks,
                            fireMasteryPercent(), lightningMasteryPercent(),
                            characterStats().combat.coldSkillDamagePercent);
    };
    simulation_->spendProjectile_ = [this](EntityId weapon, bool thrown) {
        const auto *item = inventory_.item(weapon);
        if (!item) return false;
        EntityId spent = weapon;
        if (!thrown) {
            const auto *definition = inventory_.catalog().find(item->definition);
            if (!definition || definition->equipment.shoots.empty()) return false;
            spent = {};
            for (auto slot : {weaponHandSlot(false, state().player.weaponSet),
                              weaponHandSlot(true, state().player.weaponSet)}) {
                auto candidate = inventory_.equipped(playerContainers_, slot);
                const auto *quiver = inventory_.item(candidate);
                if (candidate == weapon || !quiver) continue;
                const auto *type = inventory_.catalog().find(quiver->definition);
                if (type && type->equipment.isType(definition->equipment.shoots)) {
                    spent = candidate;
                    break;
                }
            }
            if (!spent) return false;
        }
        auto result = inventory_.consumeEquipped(spent, playerContainers_);
        bool applied = bool(result);
        if (applied) publishInventory(std::move(result), {});
        return applied;
    };
    simulation_->canSpendProjectile_ = [this](EntityId weapon, bool thrown) {
        const auto *item = inventory_.item(weapon);
        if (!item || !item->quantity) return false;
        if (thrown) return inventory_.catalog().find(item->definition)->equipment.throwable;
        const auto *definition = inventory_.catalog().find(item->definition);
        if (!definition || definition->equipment.shoots.empty()) return false;
        for (auto slot : {weaponHandSlot(false, state().player.weaponSet), weaponHandSlot(true, state().player.weaponSet)}) {
            const auto *ammo = inventory_.item(inventory_.equipped(playerContainers_, slot));
            if (ammo && ammo->id != weapon && ammo->quantity &&
                inventory_.catalog().find(ammo->definition)->equipment.isType(definition->equipment.shoots)) return true;
        }
        return false;
    };
    simulation_->monsterDefense_ = [this](const Enemy &enemy, RegionId region)
        -> std::optional<MonsterDefense> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record) return std::nullopt;
        if (auto combat = resolvedMonsterCombat(enemy.identity, region))
            return combat->defense ? std::optional<MonsterDefense>{{combat->level, *combat->defense,
                                        record->demon, record->undead, record->boss}}
                                   : std::nullopt;
        return std::nullopt;
    };
    auto worldSelection = selection;
    simulation_->monsterMoveSpeed_ = [this](const Enemy &enemy, int velocityPercent) -> std::optional<float> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->walkVelocity)
            return std::nullopt;
        // UNITS_GetBaseVelocity always uses Velocity, including RN. Monster.cpp
        // starts velocitypercent at 75; AI velocity stats add to that base.
        const int rate = monsterMovementPercent(*record, state().population.difficulty,
                                                velocityPercent + enemy.combatEffects.modifiers(state().frame).velocityPercent +
                                                (enemy.webSlowRemaining > 0 ? enemy.webSlowPercent : 0) + (enemy.identity.enchantment
                                                    ? enemy.identity.enchantment->velocityPercent : 0), enemy.chill > 0);
        return float((*record->walkVelocity << 8) * rate / 100) * 25.f / 4096.f;
    };
    simulation_->monsterWalkSpeed_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->walkVelocity) return std::optional<float>{};
        // Legacy callers apply their own movement modifiers outside this base.
        return std::optional<float>{float((*record->walkVelocity << 8) *
            (75 + (enemy.identity.enchantment ? enemy.identity.enchantment->velocityPercent : 0)) / 100) * 25.f / 4096.f};
    };
    simulation_->monsterNormalCombat_ = [this](const MonsterIdentity &identity, RegionId region)
        -> std::optional<MonsterNormalCombat> {
        if (auto combat = resolvedMonsterCombat(identity, region)) return combat->damage;
        if (state().population.difficulty != 0 || !baseMonsterRank(identity.rank))
            return std::nullopt;
        const auto *record = monsterContent_.find(identity.monster);
        return record && !record->boss ? record->normalCombat : std::nullopt;
    };
    simulation_->monsterCriticalChance_ = [this](const Enemy &enemy, RegionId region)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region))
            return combat->criticalChance;
        return std::nullopt;
    };
    simulation_->monsterDamageRegen_ = [this](const Enemy &enemy, RegionId region)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region))
            return combat->damageRegen;
        return std::nullopt;
    };
    simulation_->monsterResistance_ = [this](const Enemy &enemy, RegionId region, MonsterDamageType type)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region))
            return combat->resistances[size_t(type)];
        return std::nullopt;
    };
    simulation_->corpseSelectable_ = [this](const Enemy &corpse) {
        const auto *record = monsterContent_.find(corpse.identity.monster);
        return record && record->corpseSelectable && record->walkVelocity.value_or(0) != 0;
    };
    simulation_->redemptionCorpseEligible_ = [this](const Enemy &corpse) {
        const auto *record = monsterContent_.find(corpse.identity.monster);
        return record && record->corpseSelectable && !record->npc &&
            !content_.tables.at("monstats").number(record->sourceRow, "noAura").value_or(0);
    };
    simulation_->coldPierce_ = [this](EntityId actor) { return actor == state().player.id ? coldPiercePercent() : 0; };
    simulation_->monsterFreezeDivisor_ = content_.monsterFreezeDivisor.at(size_t(population.difficulty));
    simulation_->monsterColdDivisor_ = content_.monsterColdDivisor.at(size_t(population.difficulty));
    simulation_->unitColdEffect_ = [this](const CombatUnit &unit) {
        const MonsterRecord *record = nullptr;
        if (unit.monster) record = monsterContent_.find(unit.monster->identity.monster);
        else if (unit.hireling) {
            for (const auto &[id, entry] : monsterContent_.monsters())
                if (entry.index == unit.hireling->classId) { record = &entry; break; }
        } else return -50;
        return record ? record->coldEffect.at(size_t(state().population.difficulty)) : 0;
    };
    simulation_->monsterFreezable_ = [this](const Enemy &enemy) -> std::optional<bool> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record) return std::nullopt;
        if (record->boss || enemy.identity.rank == MonsterRank::Boss ||
            enemy.identity.rank == MonsterRank::Unique ||
            enemy.identity.rank == MonsterRank::SuperUnique ||
            enemy.identity.rank == MonsterRank::Champion) return false;
        if (record->coldEffect.at(size_t(state().population.difficulty)) >= 0)
            return std::nullopt;
        return true;
    };
    simulation_->monsterAi_ = [this](const Enemy &enemy)
        -> std::optional<MonsterAiProfile> {
        if (!enemy.identity.enchantment && !baseMonsterRank(enemy.identity.rank))
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->boss) return std::nullopt;
        auto profile = record->aiProfiles.at(state().population.difficulty);
        if (!profile) return std::nullopt;
        if (profile->kind == MonsterAiKind::Skeleton &&
            (enemy.kind == MonsterKind::Skeleton || enemy.kind == MonsterKind::HellBovine))
            return profile;
        if (profile->kind == MonsterAiKind::Brute && enemy.kind == MonsterKind::Brute)
            return profile;
        if (profile->kind == MonsterAiKind::Zombie && enemy.kind == MonsterKind::Zombie)
            return profile;
        if (profile->kind == MonsterAiKind::Fallen && enemy.kind == MonsterKind::Fallen)
            return profile;
        if (profile->kind == MonsterAiKind::CorruptRogue &&
            enemy.kind == MonsterKind::CorruptRogue)
            return profile;
        if (profile->kind == MonsterAiKind::Goatman && enemy.kind == MonsterKind::Goatman)
            return profile;
        if (profile->kind == MonsterAiKind::QuillRat && enemy.kind == MonsterKind::QuillRat)
            return profile;
        if (profile->kind == MonsterAiKind::Wraith && enemy.kind == MonsterKind::Wraith)
            return profile;
        if (profile->kind == MonsterAiKind::CorruptLancer &&
            enemy.kind == MonsterKind::CorruptLancer)
            return profile;
        if (profile->kind == MonsterAiKind::CorruptArcher &&
            enemy.kind == MonsterKind::CorruptArcher)
            return profile;
        if (profile->kind == MonsterAiKind::SkeletonBow &&
            enemy.kind == MonsterKind::SkeletonBow)
            return profile;
        if (profile->kind == MonsterAiKind::Bighead && enemy.kind == MonsterKind::Bighead)
            return profile;
        if (profile->kind == MonsterAiKind::SkeletonMage &&
            enemy.kind == MonsterKind::SkeletonMage)
            return profile;
        if (profile->kind == MonsterAiKind::Fetish && enemy.kind == MonsterKind::Fetish)
            return profile;
        if (profile->kind == MonsterAiKind::Vampire && enemy.kind == MonsterKind::Vampire)
            return profile;
        if (profile->kind == MonsterAiKind::FallenShaman &&
            enemy.kind == MonsterKind::FallenShaman)
            return profile;
        if (profile->kind == MonsterAiKind::FoulCrowNest &&
            enemy.kind == MonsterKind::FoulCrowNest)
            return profile;
        if (profile->kind == MonsterAiKind::BloodHawk &&
            enemy.kind == MonsterKind::BloodHawk)
            return profile;
        if (profile->kind == MonsterAiKind::Arach && enemy.kind == MonsterKind::Arach)
            return profile;
        return std::nullopt;
    };
    simulation_->zombieForcedPursuit_ = [this](RegionId region) {
        auto level = worldContent_.levels().find(int(region));
        return level != worldContent_.levels().end() && level->second.name == "Burial Grounds";
    };
    simulation_->monsterGetHitDuration_ = [this](const MonsterIdentity &identity)
        -> std::optional<float> {
        const auto implementation = monsterImplementation(identity.monster);
        const auto *record = monsterContent_.find(identity.monster);
        if (implementation.substitute || !record || !record->getHitMode) return std::nullopt;
        const auto *motion = monsterContent_.motion(implementation.kind, "gh");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->sanctuaryState_ = content_.states.at("sanctuary").definition.id;
    simulation_->monsterKnockbackDuration_ = [this](const Enemy &enemy) -> std::optional<float> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || monsterImplementation(record->id).substitute) return std::nullopt;
        const auto &stats = content_.tables.at("monstats");
        const auto &extra = content_.tables.at("monstats2");
        for (size_t row = 0; row < extra.rows().size(); ++row)
            if (extra.value(row, "Id") == stats.value(record->sourceRow, "MonStatsEx")) {
                if (!extra.number(row, "mKB").value_or(0) || !extra.number(row, "mWL").value_or(0)) return std::nullopt;
                const auto *motion = monsterContent_.motion(enemy.kind, "gh");
                if (!motion || record->walkAnimationRate.value_or(0) <= 0) return std::nullopt;
                return float(motion->frames) * 256.f / (float(*record->walkAnimationRate) * 25.f);
            }
        return std::nullopt;
    };
    simulation_->monsterDeathDuration_ = [this](const Enemy &enemy)
        -> std::optional<float> {
        // The approved hostile substitute uses its displayed original DT, while
        // corpse eligibility is still resolved from the real monster identity.
        const auto *motion = monsterContent_.motion(enemy.kind, "dt");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->monsterSkill2Duration_ = [this](const Enemy &enemy)
        -> std::optional<float> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (monsterImplementation(enemy.identity.monster).substitute || !record ||
            !record->skill2Mode || enemy.kind != MonsterKind::Fallen) return std::nullopt;
        const auto *motion = monsterContent_.motion(enemy.kind, "s2");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->monsterResurrectionDuration_ = [this](const Enemy &enemy)
        -> std::optional<float> {
        if ((enemy.kind != MonsterKind::Fallen && enemy.kind != MonsterKind::NecroSkeleton) ||
            monsterImplementation(enemy.identity.monster).substitute) return std::nullopt;
        const auto *motion = monsterContent_.motion(enemy.kind, "s1");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->monsterAttackTiming_ = [this](const Enemy &enemy, int mode)
        -> std::optional<MonsterAttackTiming> {
        const auto *timing = monsterContent_.attackTiming(enemy.kind, mode);
        return timing ? std::optional<MonsterAttackTiming>(*timing) : std::nullopt;
    };
    simulation_->monsterProjectile_ = [this](const Enemy &enemy, int mode)
        -> std::optional<MonsterProjectile> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || monsterImplementation(enemy.identity.monster).substitute)
            return std::nullopt;
        if (mode == 1 && (enemy.kind == MonsterKind::CorruptArcher ||
                          enemy.kind == MonsterKind::SkeletonBow ||
                          enemy.kind == MonsterKind::SkeletonMage))
            return record->attack1Projectile;
        if (mode == 2 && (enemy.kind == MonsterKind::QuillRat ||
                          enemy.kind == MonsterKind::Bighead))
            return record->attack2Projectile;
        return std::nullopt;
    };
    simulation_->monsterSpell_ = [this](const Enemy &enemy, int mode)
        -> std::optional<MonsterSpell> {
        if (mode < 3 || mode > 6 ||
            (enemy.kind != MonsterKind::Vampire &&
             !(enemy.kind == MonsterKind::FallenShaman && mode == 4)))
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || monsterImplementation(enemy.identity.monster).substitute ||
            (!record->castMode && !record->sequenceMode)) return std::nullopt;
        return record->spells[size_t(mode - 3)];
    };
    simulation_->monsterResurrection_ = [this](const Enemy &enemy)
        -> std::optional<MonsterResurrection> {
        if (enemy.kind != MonsterKind::FallenShaman) return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || monsterImplementation(enemy.identity.monster).substitute ||
            !record->sequenceMode) return std::nullopt;
        return record->resurrection;
    };
    simulation_->monsterNest_ = [this](const Enemy &enemy)
        -> std::optional<MonsterNest> {
        if (enemy.kind != MonsterKind::FoulCrowNest ||
            monsterImplementation(enemy.identity.monster).substitute) return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->nest ||
            !monsterContent_.find(record->nest->child) ||
            monsterImplementation(record->nest->child).kind != MonsterKind::BloodHawk ||
            monsterImplementation(record->nest->child).substitute) return std::nullopt;
        return record->nest;
    };
    simulation_->monsterWeb_ = [this](const Enemy &enemy)
        -> std::optional<MonsterWeb> {
        if (enemy.kind != MonsterKind::Arach ||
            monsterImplementation(enemy.identity.monster).substitute) return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? record->web : std::nullopt;
    };
    worldSelection.difficulty = population.difficulty;
    auto plan = planWorld(archives, worldContent_, worldSelection);
    regions_ = loadRegions(archives, ids_, plan.regions, monsterContent_, worldContent_, selection.seed, rollRandom(random_));
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            if (!object.npcPath.empty())
                initialNpcMotions_.push_back({object.id, object.pos, object.npcLook,
                                              object.npcRoute, object.npcWait,
                                              object.npcTarget, object.npcRandom});
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            if (auto vendor = content_.vendors.find(object.npcClass);
                vendor != content_.vendors.end()) {
                uint64_t seed = childRandom(random_);
                vendorStocks_.emplace(object.id, planVendorStock(content_, vendor->second,
                                                                  equipmentActor().level,
                                                                  population.difficulty, seed));
            }
    linkLevelExits(regions_, worldContent_);
    for (const auto &region : regions_)
        if (region.definition.id == RegionId::Encampment)
            for (const auto &layer : region.map.data.walls)
                for (size_t index = 0; index < layer.size(); ++index) {
                    const auto &cell = layer[index];
                    if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11) &&
                        ((cell.value >> 20) & 63) == 33) {
                        Vec point{float(index % region.map.data.width * 5 + 3),
                                  float(index / region.map.data.width * 5 + 3)};
                        auto arrival = region.map.grid.nearest(point);
                        if (region.map.grid.walkable(arrival) && (arrival - point).length() <= 5)
                            townPortalArrival_ = arrival;
                    }
                }
    DataTable portalObjects(archives.read("data/global/excel/objects.txt"));
    for (size_t row = 0; row < portalObjects.rows().size(); ++row)
        if (portalObjects.number(row, "Id").value_or(-1) == 59 &&
            portalObjects.number(row, "OperateFn").value_or(0) == 15)
            portalReach_ = float(portalObjects.number(row, "OperateRange").value_or(0));
        else if (portalObjects.number(row, "Id").value_or(-1) == 60 &&
                 portalObjects.number(row, "OperateFn").value_or(0) == 15)
            cainPortalReach_ = float(portalObjects.number(row, "OperateRange").value_or(0));
    portalResources_ = true;
    for (auto file : {"data/global/objects/tp/cof/tpophth.cof",
                      "data/global/objects/tp/hd/tphdlitophth.dcc",
                      "data/global/objects/tp/tr/tptrlitophth.dcc"}) {
        if (archives.contains(file)) archives.read(file);
        else portalResources_ = false;
    }
    worldEntries_ = std::move(plan.entries);
    for (const auto &[id, level] : worldContent_.levels())
        if (level.act == 0) {
            if (level.name == "Den of Evil") denRegion_ = RegionId(id);
            if (level.name == "Burial Grounds") burialRegion_ = RegionId(id);
            if (level.name == "Stony Field") stonyRegion_ = RegionId(id);
            if (level.name == "Dark Wood") darkWoodRegion_ = RegionId(id);
            if (level.name == "Forgotten Tower") towerRegion_ = RegionId(id);
            if (level.name == "Tower Cellar Level 5") towerCellarRegion_ = RegionId(id);
            if (level.name == "Barracks") barracksRegion_ = RegionId(id);
            if (level.name == "Catacombs Level 4") catacombsFourRegion_ = RegionId(id);
            if (level.name == "Tristram") tristramRegion_ = RegionId(id);
        }
    if (!denRegion_ || !burialRegion_ || !stonyRegion_ || !darkWoodRegion_ || !tristramRegion_)
        throw std::runtime_error("Original Act I quest levels are missing");
    reconcileCainObjects();
    Fingerprint fingerprint;
    fingerprint.add(content_.profile);
    auto members = archives.used;
    for (const auto &member : members) {
        fingerprint.add(member);
        fingerprint.add(archives.read(member));
    }
    for (const auto &[code, item] : content_.items.entries()) {
        fingerprint.add(code);
        fingerprint.add(item.artAvailable ? "art" : "missing-art");
    }
    for (const auto &record : content_.uniqueItems)
        fingerprint.add(record.artAvailable ? "unique-art" : "unique-missing-art");
    for (const auto &record : content_.setItems)
        fingerprint.add(record.artAvailable ? "set-art" : "set-missing-art");
    contentFingerprint_ = fingerprint.value();
    for (const auto &region : regions_) {
        AreaState area;
        area.region = region.definition.id;
        inactiveAreas_.push_back(std::move(area));
    }
    if (startRegion < -1 || startRegion >= int(regions_.size()))
        throw std::out_of_range("--region exceeds the available scene count; prefer --level <Levels.txt ID>");
    enter(startRegion < 0 ? plan.start : regions_[startRegion].definition.id);
}
const CharacterDefinition &GameSession::definitionFor(std::string_view name) const {
    auto found = std::find_if(content_.characters.begin(), content_.characters.end(),
                              [name](const auto &entry) { return entry.name == name; });
    if (found == content_.characters.end())
        throw std::runtime_error("Unknown MPQ character class: " + std::string(name));
    return *found;
}
void GameSession::grantExperience(uint64_t amount) {
    auto &player = simulation_->state_.player;
    if (!amount || player.dead) return;
    const auto &thresholds = experienceThresholds();
    player.experience += std::min(amount, thresholds.back() - player.experience);
    int before = player.level;
    while (size_t(player.level + 1) < thresholds.size() &&
           player.experience >= thresholds[size_t(player.level + 1)])
        ++player.level;
    player.unspentAttributes += (player.level - before) * characterDefinition_.statPerLevel;
    player.unspentSkills += player.level - before;
    refreshCharacter(true);
}
void GameSession::enter(RegionId id, std::optional<Vec> arrival, std::optional<Vec> coordinateOffset) {
    auto found = std::find_if(regions_.begin(), regions_.end(),
                              [id](const Region &r) { return r.definition.id == id; });
    if (found == regions_.end())
        return;
    int index = int(found - regions_.begin());
    const bool returnToTown = current_ >= 0 && !regions_[current_].definition.safe && found->definition.safe;
    if (current_ >= 0)
        inactiveAreas_[current_] = simulation_->leaveArea();
    current_ = index;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    auto plan = inactiveAreas_[current_].initialized ? PopulationPlan{} : population(*found);
    simulation_->missileWorldOrigin_ = {float(found->recipe.worldX * 5), float(found->recipe.worldY * 5)};
    simulation_->enterArea(found->map.grid, found->map.activation, arrival.value_or(found->map.spawn),
                          found->definition.safe,
                          std::move(inactiveAreas_[current_]), plan.spawns, coordinateOffset);
    if (returnToTown) {
        // The single player town becomes unoccupied when leaving it. Rebuild
        // its stock on return using the current character level (SUnitProxy).
        for (const auto &npc : found->objects)
            if (auto vendor = content_.vendors.find(npc.npcClass); vendor != content_.vendors.end()) {
                auto &seed = inventory_.state_.creationRandom;
                auto stock = planVendorStock(content_, vendor->second, unsigned(state().player.level),
                    state().population.difficulty, childRandom(seed));
                vendorStocks_[npc.id] = std::move(stock);
                soldVendorOffers_.erase(npc.id);
                gambleStocks_.erase(npc.id);
            }
    }
    if (simulation_->state_.player.hireling.active()) {
        auto &hireling = simulation_->state_.player.hireling;
        hireling.pos = state().player.pos;
        hireling.route.clear();
        hireling.attack.reset(); hireling.attackTimer = 0;
        hireling.moving = false; hireling.animationTime = 0;
    }
    onQuestRegionEntered(id);
    std::cout << "Room activation: created=" << state().area.enemies.size()
              << " deferred=" << state().area.pendingSpawns.size() << '\n';
}
PopulationPlan GameSession::population(const Region &region) const {
    const auto level = worldContent_.levels().find(int(region.definition.id));
    const auto *record = level == worldContent_.levels().end() ? nullptr : &level->second;
    const auto &preset = worldContent_.presets().at(region.recipe.preset);
    auto plan = planPopulation(monsterContent_, record, preset, region.map, state().population);
    writePopulationReport(std::cout, plan, record, preset, state().population);
    return plan;
}
bool GameSession::inventoryDestinationAllowed(const ItemDestination &destination) const {
    if (auto ground = std::get_if<GroundLocation>(&destination))
        return ground->region == region().definition.id && std::isfinite(ground->position.x) &&
               std::isfinite(ground->position.y) && ground->position.x >= 0 && ground->position.y >= 0 &&
               ground->position.x < map().grid.width && ground->position.y < map().grid.height &&
               map().grid.collisionSegment(state().player.pos, ground->position, 0x0801);
    return true;
}
void GameSession::publishInventory(InventoryResult result, EntityId requested) {
    if (!result)
        simulation_->emit(InventoryRejected{requested, result.error});
    else {
        refreshCharacter();
        for (const auto &change : result.changes)
            simulation_->emit(change);
        if (requested)
            simulation_->emit(InventoryApplied{requested, result.item, result.transferred});
    }
}
void GameSession::tick(float dt, Vec keyboard, bool forceRun) {
    simulation_->beginTick();
    for (auto &region : regions_) region.refreshObjectCollision(state().time);
    validateStorage();
    auto commands = std::move(pending_);
    pending_.clear();
    bool transitioned = false;
    for (const auto &command : commands) {
        std::visit(
            [&](const auto &intent) {
                using T = std::decay_t<decltype(intent)>;
                if constexpr (std::is_same_v<T, UseExit>) {
                    beginExit(intent.slot);
                } else if constexpr (std::is_same_v<T, UseTownPortal>) {
                    beginPortal(intent.revision);
                } else if constexpr (std::is_same_v<T, UseCainPortal>) {
                    transitioned = beginCainPortal();
                } else if constexpr (std::is_same_v<T, WaypointTravel>) {
                    transitioned = travelWaypoint(intent);
                } else if constexpr (std::is_same_v<T, Travel>) {
                    if (!state().player.dead) {
                        std::optional<Vec> arrival;
                        for (const auto &destination : regions_)
                            if (destination.definition.id == intent.destination)
                                for (const auto &object : destination.objects)
                                    if (object.name == "Waypoint" && object.interaction == Interaction::Travel) {
                                        arrival = object.accessPoint;
                                        break;
                                    }
                        enter(intent.destination, arrival);
                        transitioned = true;
                    }
                } else if constexpr (std::is_same_v<T, MoveTo>) {
                    cancelPickup();
                    cancelInteraction();
                    if (!routeBoundaryMove(intent.position)) {
                        cancelExit();
                        simulation_->execute(command);
                    }
                } else if constexpr (std::is_same_v<T, RestartArea>) {
                    cancelExit();
                    cancelPickup();
                    const auto &r = region();
                    auto plan = population(r);
                    simulation_->restartArea(r.map.spawn, plan.spawns);
                    cancelInteraction();
                    closeStorage();
                    transitioned = true;
                } else if constexpr (std::is_same_v<T, PickupItem>) {
                    cancelExit();
                    cancelInteraction();
                    beginPickup(intent.item, intent.toCursor);
                } else if constexpr (std::is_same_v<T, UseItem>)
                    useItem(intent.item);
                else if constexpr (std::is_same_v<T, SwitchWeaponSet>) {
                    auto &player = simulation_->state_.player;
                    if (!player.dead && content_.stashLayout.expansion) {
                        player.weaponSet ^= 1u;
                        player.weaponAttack.reset();
                        player.attackTarget = {};
                        player.throwAttack = player.leftHandAttack = false;
                        player.attackPosition.reset();
                        player.meleeTime = 0;
                        refreshCharacter();
                    }
                }
                else if constexpr (std::is_same_v<T, IdentifyItem>)
                    identifyItem(intent);
                else if constexpr (std::is_same_v<T, UseBeltColumn>)
                    useBeltColumn(intent.column, intent.hireling);
                else if constexpr (std::is_same_v<T, CloseStorage>)
                    closeStorage();
                else if constexpr (std::is_same_v<T, Interact>) {
                    cancelExit();
                    cancelPickup();
                    interact(intent.target);
                } else if constexpr (std::is_same_v<T, IdentifyWithCain>) {
                    identifyWithCain(intent.target);
                } else if constexpr (std::is_same_v<T, TalkToNpc>) {
                    talkToNpc(intent.target);
                } else if constexpr (std::is_same_v<T, ClaimAkaraRespec>) {
                    claimAkaraRespec(intent.target);
                } else if constexpr (std::is_same_v<T, ImbueItem>) {
                    imbueWithCharsi(intent);
                } else if constexpr (std::is_same_v<T, CompleteActOne>) {
                    completeActOne(intent.npc);
                } else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    buyVendorItem(intent.vendor, intent.slot, intent.gamble);
                } else if constexpr (std::is_same_v<T, SellVendorItem>) {
                    sellVendorItem(intent);
                } else if constexpr (std::is_same_v<T, OpenGamble>) {
                    openGamble(intent.npc);
                } else if constexpr (std::is_same_v<T, OpenHirelingList>) {
                    openHirelingList(intent.npc);
                } else if constexpr (std::is_same_v<T, HireMercenary>) {
                    hireMercenary(intent);
                } else if constexpr (std::is_same_v<T, ResurrectHireling>) {
                    resurrectHireling(intent.npc);
                } else if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    useHirelingPotion(intent.item);
                } else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                    equipHirelingItem(intent);
                } else if constexpr (std::is_same_v<T, DebugGrantHireling>) {
                    grantDebugHireling();
                } else if constexpr (std::is_same_v<T, RepairVendorItem>) {
                    repairVendorItem(intent);
                } else if constexpr (std::is_same_v<T, EndNpcConversation>) {
                    gambleStocks_.erase(intent.target);
                    if (engagedNpc_ == intent.target)
                        engagedNpc_ = {};
                } else if constexpr (std::is_same_v<T, DebugGrantGold>) {
                    auto &player = simulation_->state_.player;
                    unsigned limit = unsigned(equipmentActor().level) * 10000;
                    if (intent.amount && intent.amount <= limit - player.gold)
                        player.gold += intent.amount;
                } else if constexpr (std::is_same_v<T, GoldTransaction>) {
                    transactGold(intent);
                } else if constexpr (std::is_same_v<T, DebugDropCube>) {
                    dropDebugCube();
                } else if constexpr (std::is_same_v<T, DebugSpawnItem>) {
                    spawnDebugItem(intent);
                } else if constexpr (std::is_same_v<T, DebugGrantExperience>) {
                    grantExperience(intent.amount);
                } else if constexpr (std::is_same_v<T, DebugUnlockWaypoints>) {
                    unlockWaypoints();
                } else if constexpr (std::is_same_v<T, DebugGrantShrine>) {
                    grantShrine(intent.code);
                } else if constexpr (std::is_same_v<T, DebugSpawnMonster>) {
                    spawnDebugMonster(intent);
                } else if constexpr (std::is_same_v<T, DebugDamageMonster>) {
                    damageDebugMonster(intent);
                } else if constexpr (std::is_same_v<T, AllocateAttribute>) {
                    auto &player = simulation_->state_.player;
                    if (!player.dead && allocateAttribute(player.allocated, player.unspentAttributes, intent.attribute))
                        refreshCharacter(true);
                } else if constexpr (std::is_same_v<T, AllocateSkill>) {
                    auto &player = simulation_->state_.player;
                    if (!canAllocateSkill(intent.id)) return;
                    ++player.skillRanks[intent.id];
                    --player.unspentSkills;
                    refreshCharacter();
                } else if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                    auto &keys = simulation_->state_.player.skillHotkeys;
                    if (intent.index >= keys.size() || intent.skill < -2 ||
                        (intent.skill >= 0 && (!skillAvailable(intent.skill) ||
                            content_.skills.find(intent.skill)->passive ||
                            (!intent.right && !content_.skills.find(intent.skill)->leftAllowed)))) return;
                    for (auto &key : keys)
                        if (key.skill == intent.skill && key.right == intent.right) key.skill = -2;
                    keys[intent.index] = {intent.skill, intent.right};
                } else if constexpr (std::is_same_v<T, SelectMouseSkill>) {
                    const auto *entry = content_.skills.find(intent.skill);
                    if (intent.skill < -1 || (intent.skill >= 0 &&
                        (!entry || entry->passive || !skillAvailable(intent.skill) ||
                         (!intent.right && !entry->leftAllowed)))) return;
                    auto &player = simulation_->state_.player;
                    player.selectedSkills[player.weaponSet * 2 + unsigned(intent.right)] = intent.skill;
                    if (intent.right && intent.skill != player.channelSkill()) simulation_->stopChannel(player);
                } else if constexpr (std::is_same_v<T, DebugResetAttributes>) {
                    auto &player = simulation_->state_.player;
                    if (!player.dead && allocatedPoints(player.allocated)) {
                        player.unspentAttributes += allocatedPoints(player.allocated);
                        player.allocated = {};
                        refreshCharacter();
                    }
                } else if constexpr (std::is_same_v<T, DebugResetSkills>) {
                    auto &player = simulation_->state_.player;
                    if (!player.dead) {
                        simulation_->stopChannel(player);
                        player.skillRanks.clear();
                        player.unspentSkills = player.level - 1;
                        for (const auto &difficulty : player.actOneQuests)
                            if (difficulty.at(questIndex(ActOneQuest::DenOfEvil)).stage ==
                                uint32_t(DenStage::Rewarded))
                                ++player.unspentSkills;
                        refreshCharacter();
                    }
                } else if constexpr (std::is_same_v<T, UseSkill>) {
                    useSkill(intent);
                } else if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                                     std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                                     std::is_same_v<T, LoadBook> ||
                                     std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem> ||
                                     std::is_same_v<T, EquipItem>)
                    executeInventory(command);
                else {
                    if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, Attack> ||
                                  std::is_same_v<T, StopMoving>) {
                        cancelExit();
                        cancelPickup();
                        cancelInteraction();
                    }
                    simulation_->execute(command);
                }
            },
            command);
        syncPlayerAura();
        // A click queued in the previous region must not affect the new region.
        if (transitioned)
            break;
    }
    if (!transitioned && keyboard.length() > .1f) {
        cancelExit();
        cancelPickup();
        cancelInteraction();
    }
    regions_.at(current_).refreshObjectCollision(state().time);
    std::set<int> summonSkills;
    for (const auto &pet : state().companions)
        if (pet.hp > 0 && pet.allegiance.owner == state().player.id) summonSkills.insert(pet.summonSkill);
    for (int skill : summonSkills) {
        const int rank = effectiveSkillRank(skill);
        simulation_->enforceSummonLimit(state().player.id, skill, rank < 4 ? rank : 2 + rank / 3);
    }
    syncPlayerAura();
    simulation_->tick(dt, transitioned ? Vec{} : keyboard, forceRun);
    auto replenished = inventory_.replenish(dt);
    if (!replenished.changes.empty()) publishInventory(std::move(replenished), {});
    advanceHireling(dt);
    updateObjectTimers();
    regions_.at(current_).refreshObjectCollision(state().time);
    advanceNpcPaths(dt);
    settleDeaths();
    updateDenQuest();
    updatePickup();
    updateCainQuestItems();
    updateToolsQuestItems();
    updateInteraction();
    regions_.at(current_).refreshObjectCollision(state().time);
    updatePortal();
    updateCainPortal();
    updateExit();
    validateStorage();
}
} // namespace d2x
