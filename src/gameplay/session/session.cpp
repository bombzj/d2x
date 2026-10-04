#include "gameplay/skills/spec.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "core/random.hpp"
#include "gameplay/session/session_impl.hpp"
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

namespace d2x {
GameSessionImpl::~GameSessionImpl() = default;
const WorldState &GameSessionImpl::state() const { return simulation_->state(); }
void GameSessionImpl::setRunning(bool running) {
    simulation_->state_.player.movement.running = running;
    ++viewRevision_;
}
bool GameSessionImpl::usableCorpse(EntityId id) const { return simulation_->usableCorpse(id); }
Vec GameSessionImpl::combatPosition(EntityId id) const { return simulation_->unitPosition(id); }
bool GameSessionImpl::canAttack(EntityId actor, EntityId target) const { return simulation_->canAttack(actor, target); }
bool GameSessionImpl::active(Vec position) const { return simulation_->active(position); }
std::span<const GameEvent> GameSessionImpl::events() const { return simulation_->events(); }
namespace {
bool baseMonsterRank(MonsterRank rank) {
    return rank == MonsterRank::Normal || rank == MonsterRank::Minion;
}
} // namespace
GameSessionImpl::GameSessionImpl(Archives &archives, const WorldSelection &selection, int startRegion,
                         uint32_t sessionSeed, PopulationSettings population, std::string characterClass,
                         std::string characterName)
        : random_(initialRandom(sessionSeed)), content_(loadClassicData(archives)), worldContent_(archives),
            archives_(archives), world_(archives),
            monsterContent_(archives, content_.tables.at("monstats")),
            simulation_(std::make_unique<Simulation>(ids_)), loot_(childRandom(random_)) {
    simulation_->state_.player.character.characterClass = std::move(characterClass);
    simulation_->missileCollisions_ = content_.missileCollisions;
    simulation_->missileReturnFire_ = content_.missileReturnFire;
    simulation_->freezeDeathState_ = content_.states.at("freeze").definition;
    simulation_->shatterDeathState_ = content_.states.at("shatter").definition;
    simulation_->uninterruptableState_ = content_.states.at("uninterruptable").definition.id;
    simulation_->attractState_ = content_.states.at("attract").definition.id;
    simulation_->preventHealState_ = content_.states.at("preventheal").definition.id;
    simulation_->noMultiShotMissiles_ = content_.noMultiShotMissiles;
    simulation_->unspreadMultiShotMissiles_ = content_.unspreadMultiShotMissiles;
    simulation_->state_.player.character.name = std::move(characterName);
    characterDefinition_ = definitionFor(state().player.character.characterClass);
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
        const auto found = std::find_if(world_.regions().begin(), world_.regions().end(),
            [&](const Region &region) { return region.definition.id == target.region; });
        if (found == world_.regions().end()) return false;
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
            if (!player.actions.dead && occupies(player.movement.pos, 2)) return false; // UNITS_GetUnitSizeX(PLAYER).
            if (player.hireling.active())
                for (const auto &[id, monster] : monsterContent_.monsters())
                    if (monster.index == player.hireling.classId &&
                        occupies(player.hireling.pos, monster.collisionSize)) return false;
        }
        const auto &area = areaState(int(found - world_.regions().begin()));
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
                                               simulation_->state_.player.character.weaponSet);
        if (!result || !result.changes.empty())
            publishInventory(std::move(result), {});
    };
    simulation_->combatEffectsChanged_ = [this] { refreshCharacter(); };
    simulation_->auraEligible_ = [this](const RuntimeCombatUnit &unit, bool checkNoAura) {
        if (!unit.monster && !unit.hireling) return true;
        const MonsterRecord *monster = unit.monster ? monsterContent_.find(unit.records.monster->identity.monster) : nullptr;
        if (unit.hireling)
            for (const auto &[id, record] : monsterContent_.monsters())
                if (record.index == unit.records.hireling->classId) { monster = &record; break; }
        if (!monster || monster->npc) return false;
        const auto &stats = content_.tables.at("monstats");
        if (checkNoAura && stats.number(monster->sourceRow, "noAura").value_or(0)) return false;
        const auto &extra = content_.tables.at("monstats2");
        const auto identity = stats.value(monster->sourceRow, "MonStatsEx");
        for (size_t row = 0; row < extra.rows().size(); ++row)
            if (extra.value(row, "Id") == identity) return extra.number(row, "isAtt").value_or(0) != 0;
        return false;
    };
    simulation_->curseEligible_ = [this](const RuntimeCombatUnit &unit, bool ai) {
        if (!unit.monster) return !ai;
        const auto *record = monsterContent_.find(unit.records.monster->identity.monster);
        if (!record || !record->curseable || record->npc) return false;
        return !ai || (!unit.stats.boss && unit.stats.rank != MonsterRank::Unique && unit.stats.rank != MonsterRank::SuperUnique &&
            record->switchAi && !unit.effects->hasState(simulation_->uninterruptableState_, state().frame));
    };
    simulation_->aiCurseDivisor_ = std::max(1, content_.tables.at("difficultylevels").number(size_t(selection.difficulty), "AiCurseDiv").value_or(1));
    simulation_->terrorMovement_ = [this](const Enemy &enemy) -> std::pair<int, bool> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->walkVelocity.value_or(0) <= 0) return {0, false};
        return {std::clamp(100 * record->runVelocity.value_or(0) / *record->walkVelocity - 100, 0, 120),
                record->runMode};
    };
    simulation_->initializeNaturalElite_ = [this](Enemy &enemy, const Enemy *owner) {
        const auto *jadeMonster = monsterContent_.find(enemy.identity.monster);
        if (!owner && !jadeFigurineBoss_ && !jadeFigurineDropped_ &&
            !quest(QuestId::GoldenBird).stage &&
            worldContent_.levels().at(int(state().area.region)).act == 2 &&
            (enemy.identity.rank == MonsterRank::Unique || enemy.identity.rank == MonsterRank::SuperUnique) &&
            enemy.identity.origin != SpawnOrigin::Summoned && jadeMonster && !jadeMonster->flying &&
            jadeMonster->id != "fetish11") jadeFigurineBoss_ = enemy.id;
        const auto *fixed = monsterContent_.superUnique(enemy.identity.superUnique);
        const bool laterAct = worldContent_.levels().at(int(state().area.region)).act >= 2;
        const bool supportedFixed = fixed && (laterAct || fixed->id == "Bishibosh" || fixed->id == "Bonebreak" ||
            fixed->id == "Coldcrow" || fixed->id == "Rakanishu" || fixed->id == "Treehead WoodFist" ||
            fixed->id == "Pitspawn Fouldog" || fixed->id == "Corpsefire" || fixed->id == "The Cow King" ||
            fixed->id == "Boneash" || fixed->id == "The Smith" || fixed->id == "Griswold" ||
            fixed->id == "The Countess");
        if (enemy.enchantment || (owner ? (owner->identity.rank != MonsterRank::Unique &&
            owner->identity.rank != MonsterRank::SuperUnique) ||
            !owner->enchantment : enemy.identity.rank != MonsterRank::Champion &&
            enemy.identity.rank != MonsterRank::Unique && !supportedFixed)) return;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || (record->boss && !supportedFixed) || (monsterImplementation(record->id).substitute && !laterAct)) return;
        auto identity = enemy.identity;
        identity.rank = MonsterRank::Normal;
        const auto base = resolvedMonsterCombat(identity, state().area.region);
        if (!base) throw std::runtime_error("Natural elite lacks original combat attributes: " + record->id);
        auto modifiers = owner
            ? inheritedMonsterEnchantment(content_, *record, *base, state().population.difficulty, *owner->enchantment)
            : rollMonsterEnchantment(content_, *record, *base, state().population.difficulty,
                                     enemy.combatRandom, enemy.identity.rank, true, false, enemy.identity.championVariantAllowed,
                                     supportedFixed ? fixed : nullptr);
        int64_t life = int64_t(enemy.maxHp * 256.f);
        life += life * modifiers.lifePercent / 100;
        life = life * modifiers.lifeScalePercent / 100;
        if (owner) enemy.identity.rank = MonsterRank::Minion;
        enemy.enchantment = std::move(modifiers);
        enemy.hp = enemy.maxHp = float(std::max<int64_t>(1, life)) / 256.f;
        enemy.nextAuraFrame = state().frame;
    };
    simulation_->monsterHitProperties_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        return record ? std::pair{record->hitClass, record->primeEvil} : std::pair{0, false};
    };
    simulation_->countessFirewall_ = monsterContent_.countessFirewall();
    simulation_->telekinesisTarget_ = [this](EntityId target, int range, bool operate) {
        return telekinesisTarget(target, range, operate);
    };
    if (const auto blaze = content_.states.find("blaze"); blaze != content_.states.end())
        simulation_->blazeState_ = blaze->second.definition.id;
    if (const auto shield = content_.states.find("energyshield"); shield != content_.states.end())
        simulation_->energyShieldState_ = shield->second.definition.id;
    simulation_->hirelingAttributes_ = [this] {
        const auto merc = hirelingStats();
        CharacterAttributes result;
        result.maxLife = merc.base.life; result.defense = merc.base.defense;
        result.fireResist = merc.fireResist; result.coldResist = merc.coldResist;
        result.lightningResist = merc.lightningResist; result.poisonResist = merc.poisonResist;
        result.combat = merc.combat;
        return result;
    };
    simulation_->attackTiming_ = [this](const WeaponDamage &weapon, bool thrown, bool leftHand, std::string_view requestedMode)
        -> std::optional<WeaponAttackTiming> {
        const auto action = thrown ? (leftHand ? BasicSkillAction::LeftHandThrow : BasicSkillAction::Throw) :
                                     (leftHand ? BasicSkillAction::LeftHandSwing : BasicSkillAction::Attack);
        const auto skill = std::find_if(content_.skills.skills.begin(), content_.skills.skills.end(),
            [action](const auto &entry) { return entry.second.basicAction == action; });
        if (skill == content_.skills.skills.end()) return std::nullopt;
        const std::string mode = requestedMode.empty() ? skill->second.animationMode : std::string(requestedMode);
        auto found = content_.skills.attackTimings.find(characterAppearance() + mode + equipmentStats().animationClass);
        if (found == content_.skills.attackTimings.end()) return std::nullopt;
        const auto &data = found->second;
        if (mode == "bl") {
            const int faster = std::max(0, characterStats().combat.fasterBlock);
            const int rate = (characterStats().combat.shieldDefensePercent > 0 ? 100 : 50) + 120 * faster / (120 + faster);
            return WeaponAttackTiming{mode, data.frames, std::clamp(data.speed * rate / 100, 1, 32767), 0, 0};
        }
        const int skillRate = characterStats().combat.attackRate - (state().player.resources.chill > 0 ? 50 : 0);
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
        if (auto combat = resolvedMonsterCombat(enemy.identity, region, enemy.enchantmentData())) {
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
    configureSkillSources();
    simulation_->spendProjectile_ = [this](EntityId weapon, bool thrown) {
        const auto *item = inventory_.item(weapon);
        if (!item) return false;
        EntityId spent = weapon;
        if (!thrown) {
            const auto *definition = inventory_.catalog().find(item->definition);
            if (!definition || definition->equipment.shoots.empty()) return false;
            spent = {};
            for (auto slot : {weaponHandSlot(false, state().player.character.weaponSet),
                              weaponHandSlot(true, state().player.character.weaponSet)}) {
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
        for (auto slot : {weaponHandSlot(false, state().player.character.weaponSet), weaponHandSlot(true, state().player.character.weaponSet)}) {
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
        if (auto combat = resolvedMonsterCombat(enemy.identity, region, enemy.enchantmentData()))
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
                                                (enemy.webSlowRemaining > 0 ? enemy.webSlowPercent : 0) + (enemy.enchantment
                                                    ? enemy.enchantment->velocityPercent : 0), enemy.chill > 0);
        return float((*record->walkVelocity << 8) * rate / 100) * 25.f / 4096.f;
    };
    simulation_->monsterWalkSpeed_ = [this](const Enemy &enemy) {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->walkVelocity) return std::optional<float>{};
        // Legacy callers apply their own movement modifiers outside this base.
        return std::optional<float>{float((*record->walkVelocity << 8) *
            (75 + (enemy.enchantment ? enemy.enchantment->velocityPercent : 0)) / 100) * 25.f / 4096.f};
    };
    simulation_->monsterNormalCombat_ = [this](const MonsterIdentity &identity, RegionId region,
                                               const MonsterEnchantment *enchantment)
        -> std::optional<MonsterNormalCombat> {
        if (auto combat = resolvedMonsterCombat(identity, region, enchantment)) return combat->damage;
        if (state().population.difficulty != 0 || !baseMonsterRank(identity.rank))
            return std::nullopt;
        const auto *record = monsterContent_.find(identity.monster);
        return record && !record->boss ? record->normalCombat : std::nullopt;
    };
    simulation_->monsterCriticalChance_ = [this](const Enemy &enemy, RegionId region)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region, enemy.enchantmentData()))
            return combat->criticalChance;
        return std::nullopt;
    };
    simulation_->monsterDamageRegen_ = [this](const Enemy &enemy, RegionId region)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region, enemy.enchantmentData()))
            return combat->damageRegen;
        return std::nullopt;
    };
    simulation_->monsterResistance_ = [this](const Enemy &enemy, RegionId region, MonsterDamageType type)
        -> std::optional<int> {
        if (auto combat = resolvedMonsterCombat(enemy.identity, region, enemy.enchantmentData()))
            return combat->resistances[size_t(type)];
        return std::nullopt;
    };
    simulation_->corpseSelectable_ = [this](const Enemy &corpse) {
        if (corpse.kind == MonsterKind::BloodRaven || corpse.identity.superUnique == "The Countess") return false;
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
    simulation_->unitColdEffect_ = [this](const RuntimeCombatUnit &unit) {
        const MonsterRecord *record = nullptr;
        if (unit.monster) record = monsterContent_.find(unit.records.monster->identity.monster);
        else if (unit.hireling) {
            for (const auto &[id, entry] : monsterContent_.monsters())
                if (entry.index == unit.records.hireling->classId) { record = &entry; break; }
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
        if (!enemy.enchantment && !baseMonsterRank(enemy.identity.rank) &&
            enemy.kind != MonsterKind::BloodRaven && enemy.kind != MonsterKind::Andariel)
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || (record->boss && enemy.kind != MonsterKind::Griswold &&
            enemy.kind != MonsterKind::BloodRaven && enemy.kind != MonsterKind::Andariel))
            return std::nullopt;
        auto profile = record->aiProfiles.at(state().population.difficulty);
        if (!profile) return std::nullopt;
        if (profile->kind == MonsterAiKind::Andariel && enemy.kind == MonsterKind::Andariel) return profile;
        if (enemy.identity.superUnique == "The Countess") { profile->kind = MonsterAiKind::Countess; return profile; }
        if ((profile->kind == MonsterAiKind::Smith && enemy.kind == MonsterKind::Smith) ||
            (profile->kind == MonsterAiKind::Griswold && enemy.kind == MonsterKind::Griswold) ||
            (profile->kind == MonsterAiKind::BloodRaven && enemy.kind == MonsterKind::BloodRaven)) return profile;
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
            !record->skill2Mode || (enemy.kind != MonsterKind::Fallen && record->ai != "Hydra")) return std::nullopt;
        const auto *motion = monsterContent_.motion(enemy.kind, "s2");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->monsterResurrectionDuration_ = [this](const Enemy &enemy)
        -> std::optional<float> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || monsterImplementation(enemy.identity.monster).substitute) return std::nullopt;
        if (enemy.kind == MonsterKind::FallenShaman)
            return record->resurrectionMode == "nu" ? std::optional<float>(0.f) : std::nullopt;
        if (enemy.kind != MonsterKind::Fallen && enemy.kind != MonsterKind::NecroSkeleton)
            return std::nullopt;
        const auto *motion = monsterContent_.motion(enemy.kind, "s1");
        return motion ? std::optional<float>(motion->duration) : std::nullopt;
    };
    simulation_->monsterAttackTiming_ = [this](const Enemy &enemy, int mode)
        -> std::optional<MonsterAttackTiming> {
        if (enemy.identity.superUnique == "The Countess" && mode == 3) mode = 1;
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
                          enemy.kind == MonsterKind::SkeletonMage || enemy.kind == MonsterKind::BloodRaven))
            return record->attack1Projectile;
        if (mode == 4 && enemy.kind == MonsterKind::BloodRaven) return record->attack1Projectile;
        if (mode == 2 && (enemy.kind == MonsterKind::QuillRat ||
                          enemy.kind == MonsterKind::Bighead))
            return record->attack2Projectile;
        return std::nullopt;
    };
    simulation_->monsterSpell_ = [this](const Enemy &enemy, int mode)
        -> std::optional<MonsterSpell> {
        if (mode < 3 || mode > 6 ||
            (enemy.kind != MonsterKind::Vampire && enemy.kind != MonsterKind::Andariel &&
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
        if ((enemy.kind != MonsterKind::FoulCrowNest && enemy.kind != MonsterKind::BloodRaven) ||
            monsterImplementation(enemy.identity.monster).substitute) return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->nest ||
            !monsterContent_.find(record->nest->child) ||
            (monsterImplementation(record->nest->child).kind != MonsterKind::BloodHawk &&
             monsterImplementation(record->nest->child).kind != MonsterKind::Zombie) ||
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
    world_.initialize(ids_, plan, monsterContent_, worldContent_, selection.seed, rollRandom(random_));
    if (startRegion < -1 || startRegion >= int(world_.regions().size()))
        throw std::out_of_range("--region exceeds the available scene count; prefer --level <Levels.txt ID>");
    ensureRegion(startRegion < 0 ? plan.start : world_.regions()[startRegion].definition.id, true);
    linkLevelExits(world_.regions(), worldContent_);
    for (const auto &region : world_.regions())
        if (region.definition.safe)
            for (const auto &layer : region.map.terrain.data.walls)
                for (size_t index = 0; index < layer.size(); ++index) {
                    const auto &cell = layer[index];
                    if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11) &&
                        ((cell.value >> 20) & 63) == 33) {
                        Vec point{float(index % region.map.terrain.data.width * 5 + 3),
                                  float(index / region.map.terrain.data.width * 5 + 3)};
                        auto arrival = region.map.grid.nearest(point);
                        if (region.map.grid.walkable(arrival) && (arrival - point).length() <= 5) {
                            townPortalArrivals_[region.definition.id] = arrival;
                            if (region.definition.id == RegionId::Encampment)
                                townPortalArrival_ = arrival;
                        }
                    }
                }
    DataTable portalObjects(archives.read("data/global/excel/objects.txt"));
    content_.tables.emplace("objects", portalObjects);
    for (size_t row = 0; row < portalObjects.rows().size(); ++row) {
        std::map<std::string, std::string> fields;
        for (const auto &column : portalObjects.columns()) fields.emplace(column, portalObjects.value(row, column));
        questObjectRows_.push_back(std::move(fields));
    }
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
    fingerprint.add("quest-rules-v18-later-acts");
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
    std::vector<RegionId> areaIds;
    for (const auto &region : world_.regions()) areaIds.push_back(region.definition.id);
    areas_.reset(areaIds);
    if (startRegion < -1 || startRegion >= int(world_.regions().size()))
        throw std::out_of_range("--region exceeds the available scene count; prefer --level <Levels.txt ID>");
    enter(startRegion < 0 ? plan.start : world_.regions()[startRegion].definition.id);
}
const CharacterDefinition &GameSessionImpl::definitionFor(std::string_view name) const {
    auto found = std::find_if(content_.characters.begin(), content_.characters.end(),
                              [name](const auto &entry) { return entry.name == name; });
    if (found == content_.characters.end())
        throw std::runtime_error("Unknown MPQ character class: " + std::string(name));
    return *found;
}
void GameSessionImpl::grantExperience(uint64_t amount) {
    if (grantCharacterExperience(characterProgressionContext(), amount, experienceThresholds(),
                                 characterDefinition_.statPerLevel)) refreshCharacter(true);
}
bool GameSessionImpl::inventoryDestinationAllowed(const ItemDestination &destination) const {
    if (auto ground = std::get_if<GroundLocation>(&destination))
        return ground->region == region().definition.id && std::isfinite(ground->position.x) &&
               std::isfinite(ground->position.y) && ground->position.x >= 0 && ground->position.y >= 0 &&
               ground->position.x < map().grid.width && ground->position.y < map().grid.height &&
               map().grid.collisionSegment(state().player.movement.pos, ground->position, 0x0801);
    return true;
}
void GameSessionImpl::publishInventory(InventoryResult result, EntityId requested) {
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
} // namespace d2x
