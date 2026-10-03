#include "gameplay/skills/aura_owner.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/summon_resolve.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "session_impl.hpp"
#include "content/skills/aura_data.hpp"
#include <algorithm>

namespace d2x {
namespace {
int skillRank(const SkillRecord &skill, const PlayerState &player, const CharacterDefinition &definition,
              const CombatModifiers &mods, const InventoryService &inventory,
              const PlayerContainers &containers, const EquipmentActor &actor) {
    const int id = skill.id;
    CharacterSkillRankInput input;
    if (auto learned = player.skillRanks.find(id);
        learned != player.skillRanks.end()) input.learned = learned->second;
    for (auto slot : {weaponHandSlot(false, player.weaponSet),
                      weaponHandSlot(true, player.weaponSet)}) {
        const auto *item = inventory.item(inventory.equipped(containers, slot));
        if (item && item->quantity && item->grantedSkill == id &&
            (!inventory.catalog().find(item->definition)->maxDurability || item->durability) &&
            inventory.equipmentRequirements(item->handle(), actor) == InventoryError::None)
            ++input.granted;
    }
    auto bonus = [](const auto &values, int key) {
        auto found = values.find(key);
        return found == values.end() ? 0 : found->second;
    };
    input.native = skill.classCode == definition.code;
    input.singleSkill = bonus(mods.singleSkills, id);
    input.nonClassSkill = bonus(mods.nonClassSkills, id);
    input.allSkills = mods.allSkills;
    input.classSkills = bonus(mods.classSkills, int(definition.sourceRow));
    if (skill.page > 0) input.tabSkills = bonus(mods.tabSkills, int(definition.sourceRow) * 8 + skill.page - 1);
    return resolveCharacterSkillRank(input);
}
}
int GameSessionImpl::effectiveSkillRank(int id) const {
    const auto *skill = content_.skills.find(id);
    if (!skill) return 0;
    return skillRank(*skill, state().player, characterDefinition_, characterStats().combat,
                     inventory_, playerContainers_, equipmentActor());
}
bool GameSessionImpl::telekinesisTarget(EntityId target, int range, bool operate) {
    if (!target || state().player.dead || cursorItem()) return false;
    auto within = [&](Vec position) {
        const int deltaX = int(position.x) - int(state().player.pos.x);
        const int deltaY = int(position.y) - int(state().player.pos.y);
        return deltaX * deltaX + deltaY * deltaY <= range * range;
    };
    const auto unit = simulation_->combatUnit(target);
    if (unit) return unit.alive() && !region().definition.safe && simulation_->canAttack(state().player.id, target) && within(*unit.position);
    if (const auto *item = inventory_.item(target)) {
        const auto *ground = std::get_if<GroundLocation>(&item->location);
        const auto *definition = inventory_.catalog().find(item->definition);
        if (!ground || !definition || ground->region != region().definition.id || !within(ground->position)) return false;
        if (!operate) return true;
        const auto &equipment = definition->equipment;
        if (!equipment.isType("scro") && !equipment.isType("gold") && !equipment.isType("tpot") &&
            !equipment.isType("misl") && !equipment.isType("poti") && !equipment.isType("key")) return true;
        auto access = inventoryAccess();
        access.reach = float(range);
        const auto handle = item->handle();
        const auto code = item->definition;
        if (equipment.isType("gold")) {
            auto &player = simulation_->state_.player;
            const unsigned capacity = unsigned(player.level) * 10000;
            const unsigned quantity = std::min(item->quantity, capacity - player.gold);
            if (!quantity) return true;
            auto result = inventory_.consume(handle, quantity, access);
            if (result) { player.gold += quantity; simulation_->emit(ItemPickedUp{target, code, quantity}); }
            publishInventory(std::move(result), target);
        } else {
            auto result = inventory_.collect(handle, playerContainers_, access);
            if (result) simulation_->emit(ItemPickedUp{target, code, result.transferred});
            publishInventory(std::move(result), target);
        }
        return true;
    }
    const auto *object = this->object(target);
    if (!object || !object->npcClass.empty() || !within(object->pos)) return false;
    if (operate) completeInteraction(*object);
    return true;
}
bool GameSessionImpl::applySkillCastTiming(SkillCastSpec &cast) const {
    if (cast.effect == SkillBehavior::Inferno) {
        cast.castDuration = 15.f / 25.f;
        cast.castImpact = 10.f / 25.f;
        cast.castRate = 25;
        return true;
    }
    auto timing = content_.skills.castTimings.find(characterAppearance() + "sc" +
                                                  equipmentStats().animationClass);
    if (timing == content_.skills.castTimings.end()) return false;
    const auto &animation = timing->second;
    const int faster = std::max(0, characterStats().combat.fasterCast);
    const int rate = std::clamp(100 + int(int64_t(120) * faster / (120 + faster)) +
                               characterStats().otherAnimationRate, 15, 175);
    const int speed = std::max(1, animation.speed * rate / 100);
    if (cast.arc) {
        cast.castDuration = float((19 * 256 + speed - 1) / speed) / 25.f;
        cast.castImpact = float((7 * 256 + speed - 1) / speed) / 25.f;
        cast.castRate = float(speed) * 25.f / 256.f;
        return true;
    }
    const int frames = std::max(1, (animation.frames * 256 + speed - 1) / speed - 1);
    cast.castDuration = float(frames) / 25.f;
    cast.castImpact = float(std::min(frames, (animation.actionFrame * 256 + speed - 1) / speed)) / 25.f;
    cast.castRate = float(speed) * 25.f / 256.f;
    return true;
}
int GameSessionImpl::fireMasteryPercent() const {
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.fireMasteryPerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        return skillRankBonus(*skill.fireMasteryPerRank, rank);
    }
    return 0;
}
int GameSessionImpl::lightningMasteryPercent() const {
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.lightningMasteryPerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        return skillRankBonus(*skill.lightningMasteryPerRank, rank);
    }
    return 0;
}
int GameSessionImpl::coldPiercePercent() const {
    const int equipment = characterStats().combat.coldPierce;
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.coldPiercePerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        return equipment + skillRankBonus(*skill.coldPiercePerRank, rank);
    }
    return equipment;
}
void GameSessionImpl::applyWarmth(CharacterAttributes &stats, const PlayerState &player,
                              const CharacterDefinition &definition, const InventoryService &inventory,
                              const PlayerContainers &containers, const EquipmentActor &actor) const {
    bool active = false;
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.manaRecoveryPerRank || skill.classCode != definition.code) continue;
        const int rank = skillRank(skill, player, definition, stats.combat, inventory, containers, actor);
        if (rank <= 0) continue;
        active = true;
        const int bonus = skillRankBonus(*skill.manaRecoveryPerRank, rank);
        stats.combat.manaRecovery += bonus;
    }
    if (active)
        stats.manaRegen = manaRecoveryRate(stats.maxMana, definition.manaRegen, stats.combat.manaRecovery);
}
void GameSessionImpl::syncPlayerAura() {
    auto &player = simulation_->state_.player;
    const int skill = player.selectedSkills[player.weaponSet * 2 + 1];
    const auto *record = content_.skills.find(skill);
    const int rank = record && record->auraImplemented && !player.dead && skillAvailable(skill)
        ? effectiveSkillRank(skill) : 0;
    const SkillAuraOwner owner{player.id, player.dead, player.aura};
    if (simulation_->skills().clearAuraIfChanged(owner, skill, rank)) refreshCharacter();
    if (rank <= 0) return;
    auto definition = resolveAura(content_, skill, rank, player.skillRanks, fireMasteryPercent(),
        lightningMasteryPercent(), characterStats().combat.coldSkillDamagePercent, effectiveSkillRank(99));
    if (definition) simulation_->skills().prepareAura(owner, *definition, record->auraImmediate);
}
void GameSessionImpl::useSkill(const UseSkill &intent) {
    const auto *entry = content_.skills.find(intent.id);
    const auto &player = state().player;
    if (!entry || entry->passive || player.dead || !skillAvailable(intent.id)) return;
    if (!entry->executable()) {
        simulation_->state_.message = "This skill effect is not implemented";
        return;
    }
    if (region().definition.safe && !entry->allowedInTown) {
        simulation_->state_.message = "This skill cannot be used in town";
        return;
    }
    if (entry->auraImplemented) {
        simulation_->state_.player.selectedSkills[player.weaponSet * 2 + 1] = intent.id;
        simulation_->skills().stopChannel(simulation_->skillCaster(simulation_->state_.player.id));
        return;
    }
    if (entry->basicAction != BasicSkillAction::None) {
        cancelExit(); cancelPickup(); cancelInteraction();
        const bool thrown = entry->basicAction == BasicSkillAction::Throw ||
                            entry->basicAction == BasicSkillAction::LeftHandThrow;
        const bool leftHand = entry->basicAction == BasicSkillAction::LeftHandSwing ||
                              entry->basicAction == BasicSkillAction::LeftHandThrow;
        simulation_->execute(Attack{intent.enemy, thrown, leftHand, intent.target, intent.stationary});
        return;
    }
    if (entry->spell) {
        const int rank = effectiveSkillRank(intent.id);
        auto resolved = skillSources_.resolve(*entry->spell, player.id, rank);
        if (entry->spell->summon) {
            const auto &definition = *entry->spell->summon;
            resolved.summon = resolveSummon(definition, rank, effectiveSkillRank(definition.masterySkill),
                effectiveSkillRank(definition.resistSkill), player.level, state().population.difficulty);
        }
        if (resolved.weapon) {
            if (resolved.weapon->chargeVelocity > 0) {
                resolved.missileVelocity = float(characterDefinition_.runVelocity * 256) * 25.f / 4096.f;
                resolved.staticPercent = float(state().player.combatEffects.modifiers(state().frame).velocityPercent);
            }
            cancelExit(); cancelPickup(); cancelInteraction();
            simulation_->skills().beginWeaponSkill(simulation_->skillWeaponCaster(simulation_->state_.player.id), resolved, intent.target, intent.enemy);
            return;
        }
        if (!applySkillCastTiming(resolved)) {
            simulation_->state_.message = "Original cast animation timing is unavailable";
            return;
        }
        const int levelId = int(region().definition.id);
        const bool teleportAllowed = content_.teleportByLevel.contains(levelId) &&
            content_.teleportByLevel.at(levelId) != 0;
        cancelExit(); cancelPickup(); cancelInteraction();
        simulation_->skills().beginSkillCast(simulation_->skillCaster(simulation_->state_.player.id), resolved, intent.target, teleportAllowed,
            content_.staticFieldMinimum.at(size_t(state().population.difficulty)), intent.enemy);
        return;
    }
    simulation_->state_.message = "This skill effect is not implemented";
}
bool GameSessionImpl::weaponSkillReady(const SkillCastSpec &skill) const {
    if (!skill.weapon) return false;
    const auto &action = *skill.weapon;
    const auto &player = state().player;
    if (player.dead || (player.mana < skill.manaCost && action.chargeVelocity == 0) ||
        (action.delayFrames > 0 && state().frame < player.skillDelayUntil)) return false;
    const auto *weapon = simulation_->attackWeapon(action.thrown, false);
    return weapon && (action.smite ? bool(player.equipment.shield) :
        std::find(weapon->types.begin(), weapon->types.end(), action.requiredType) != weapon->types.end()) &&
        (action.smite || !(action.thrown || weapon->ranged) ||
         (simulation_->canSpendProjectile_ && simulation_->canSpendProjectile_(weapon->item, action.thrown)));
}
bool GameSessionImpl::skillAvailable(int id) const {
    const auto *entry = content_.skills.find(id);
    if (!entry) return false;
    const auto *tree = content_.skills.tree(characterDefinition_.code);
    if (!tree) return false;
    if (entry->classCode.empty())
        return entry->sourceName == "Attack" ||
            std::find(tree->commonSkills.begin(), tree->commonSkills.end(), id) != tree->commonSkills.end();
    if (entry->classCode != characterDefinition_.code &&
        !characterStats().combat.nonClassSkills.contains(id)) return false;
    return effectiveSkillRank(id) > 0;
}
} // namespace d2x
