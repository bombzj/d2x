#include "gameplay/simulation/simulation.hpp"
#include "session.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include <algorithm>

namespace d2x {
namespace {
int skillRank(const SkillRecord &skill, const PlayerState &player, const CharacterDefinition &definition,
              const CombatModifiers &mods, const InventoryService &inventory,
              const PlayerContainers &containers, const EquipmentActor &actor) {
    const int id = skill.id;
    int rank = 0;
    if (auto learned = player.skillRanks.find(id);
        learned != player.skillRanks.end()) rank = learned->second;
    for (auto slot : {weaponHandSlot(false, player.weaponSet),
                      weaponHandSlot(true, player.weaponSet)}) {
        const auto *item = inventory.item(inventory.equipped(containers, slot));
        if (item && item->quantity && item->grantedSkill == id &&
            (!inventory.catalog().find(item->definition)->maxDurability || item->durability) &&
            inventory.equipmentRequirements(item->handle(), actor) == InventoryError::None)
            ++rank;
    }
    auto bonus = [](const auto &values, int key) {
        auto found = values.find(key);
        return found == values.end() ? 0 : found->second;
    };
    const bool native = skill.classCode == definition.code;
    if (native) rank += bonus(mods.singleSkills, id);
    const int nonClass = bonus(mods.nonClassSkills, id);
    rank += native ? std::min(3, nonClass) : nonClass;
    if (rank > 0) {
        rank += mods.allSkills;
        if (native) {
            rank += bonus(mods.classSkills, int(definition.sourceRow));
            if (skill.page > 0)
                rank += bonus(mods.tabSkills, int(definition.sourceRow) * 8 + skill.page - 1);
        }
    }
    return std::max(0, rank);
}
}
int GameSession::effectiveSkillRank(int id) const {
    const auto *skill = content_.skills.find(id);
    if (!skill) return 0;
    return skillRank(*skill, state().player, characterDefinition_, characterStats().combat,
                     inventory_, playerContainers_, equipmentActor());
}
bool GameSession::telekinesisTarget(EntityId target, int range, bool operate) {
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
bool GameSession::applySkillCastTiming(SkillCastSpec &cast) const {
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
int GameSession::fireMasteryPercent() const {
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.fireMasteryPerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        if (rank <= 0) return 0;
        const auto [base, perLevel] = *skill.fireMasteryPerRank;
        return base + (rank - 1) * perLevel;
    }
    return 0;
}
int GameSession::lightningMasteryPercent() const {
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.lightningMasteryPerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        if (rank <= 0) return 0;
        const auto [base, perLevel] = *skill.lightningMasteryPerRank;
        return base + (rank - 1) * perLevel;
    }
    return 0;
}
int GameSession::coldPiercePercent() const {
    const int equipment = characterStats().combat.coldPierce;
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.coldPiercePerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        if (rank <= 0) return equipment;
        const auto [base, perLevel] = *skill.coldPiercePerRank;
        return equipment + base + (rank - 1) * perLevel;
    }
    return equipment;
}
void GameSession::applyWarmth(CharacterAttributes &stats, const PlayerState &player,
                              const CharacterDefinition &definition, const InventoryService &inventory,
                              const PlayerContainers &containers, const EquipmentActor &actor) const {
    bool active = false;
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.manaRecoveryPerRank || skill.classCode != definition.code) continue;
        const int rank = skillRank(skill, player, definition, stats.combat, inventory, containers, actor);
        if (rank <= 0) continue;
        active = true;
        const auto [base, perLevel] = *skill.manaRecoveryPerRank;
        const int bonus = base + (rank - 1) * perLevel;
        stats.combat.manaRecovery += bonus;
    }
    if (active)
        stats.manaRegen = manaRecoveryRate(stats.maxMana, definition.manaRegen, stats.combat.manaRecovery);
}
void GameSession::syncPlayerAura() {
    auto &player = simulation_->state_.player;
    const int skill = player.selectedSkills[player.weaponSet * 2 + 1];
    const auto *record = content_.skills.find(skill);
    const int rank = record && record->auraImplemented && !player.dead && skillAvailable(skill)
        ? effectiveSkillRank(skill) : 0;
    auto &activeAura = player.aura;
    if (activeAura && (activeAura->definition.skill != skill || activeAura->definition.rank != rank)) {
        std::vector<EffectHandle> remove;
        for (const auto &effect : player.combatEffects.entries())
            if (effect.spec.state.id == activeAura->definition.ownerState.id &&
                effect.spec.source.entity == player.id && effect.spec.source.definition == activeAura->definition.skill)
                remove.push_back(effect.handle);
        for (auto handle : remove) player.combatEffects.remove(handle);
        player.auraSuppressesManaRegen = false;
        if (activeAura->definition.skill == 114 && !player.dead)
            player.combatEffects.removeState(content_.states.at("shatter").definition.id);
        activeAura.reset();
        refreshCharacter();
    }
    if (rank <= 0) return;
    auto definition = resolveAura(content_, skill, rank, player.skillRanks, fireMasteryPercent(),
        lightningMasteryPercent(), characterStats().combat.coldSkillDamagePercent, effectiveSkillRank(99));
    if (!definition) return;
    if (activeAura) {
        activeAura->definition = *definition;
        return;
    }
    const EffectFrame period = EffectFrame(std::max(5, definition->periodFrames));
    activeAura = ActiveAura{*definition, state().frame + period};
    if (record->auraImmediate) {
        activeAura->nextFrame = state().frame;
        simulation_->updateAuras(true);
    } else {
        CombatEffectSpec effect;
        effect.state = definition->ownerState;
        effect.source = {CombatEffectSource::Skill, player.id, skill, rank};
        effect.stacking = EffectStacking::AuraLevel;
        effect.duration = period + 1;
        const auto applied = player.combatEffects.apply(std::move(effect), state().frame);
        simulation_->combatEffectsChanged(applied.removed);
    }
}
void GameSession::useSkill(const UseSkill &intent) {
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
        simulation_->stopChannel(simulation_->state_.player);
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
        auto resolved = resolveSkill(*entry->spell, rank,
                                     player.skillRanks, fireMasteryPercent(),
                                     lightningMasteryPercent(), characterStats().combat.coldSkillDamagePercent);
        if (entry->spell->summon) {
            const auto &definition = *entry->spell->summon;
            resolved.summon = resolveSummon(definition, rank, effectiveSkillRank(definition.masterySkill),
                effectiveSkillRank(definition.resistSkill), player.level, state().population.difficulty);
        }
        if (resolved.weapon) {
            cancelExit(); cancelPickup(); cancelInteraction();
            simulation_->beginWeaponSkill(resolved, intent.target, intent.enemy);
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
        simulation_->beginSkillCast(simulation_->state_.player, resolved, intent.target, teleportAllowed,
            content_.staticFieldMinimum.at(size_t(state().population.difficulty)), intent.enemy);
        return;
    }
    simulation_->state_.message = "This skill effect is not implemented";
}
bool GameSession::weaponSkillReady(const SkillCastSpec &skill) const {
    if (!skill.weapon) return false;
    const auto &action = *skill.weapon;
    const auto &player = state().player;
    if (player.dead || player.mana < skill.manaCost ||
        (action.delayFrames > 0 && state().frame < player.skillDelayUntil)) return false;
    const auto *weapon = simulation_->attackWeapon(action.thrown, false);
    return weapon && std::find(weapon->types.begin(), weapon->types.end(), action.requiredType) != weapon->types.end() &&
        (!(action.thrown || weapon->ranged) ||
         (simulation_->canSpendProjectile_ && simulation_->canSpendProjectile_(weapon->item, action.thrown)));
}
bool GameSession::skillAvailable(int id) const {
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
int GameSession::nextSkillRequiredLevel(int id) const {
    const auto *entry = content_.skills.find(id);
    if (!entry) return 0;
    const auto rank = state().player.skillRanks.find(id);
    return entry->requiredLevel + (rank == state().player.skillRanks.end() ? 0 : rank->second);
}
bool GameSession::canAllocateSkill(int id) const {
    const auto &player = state().player;
    const auto *entry = content_.skills.find(id);
    if (!entry || entry->classCode != characterDefinition_.code || player.dead ||
        player.unspentSkills <= 0 || player.level < nextSkillRequiredLevel(id)) return false;
    const auto current = player.skillRanks.find(id);
    if (current != player.skillRanks.end() && current->second >= entry->maximumRank) return false;
    for (int prerequisite : entry->prerequisites) {
        const auto learned = player.skillRanks.find(prerequisite);
        if (learned == player.skillRanks.end() || learned->second <= 0) return false;
    }
    return true;
}
} // namespace d2x
