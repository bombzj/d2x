#include "session.hpp"
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
bool GameSession::applyOriginalCastTiming(OriginalSkillCast &cast) const {
    if (cast.effect == Skill::Inferno) {
        cast.castDuration = 15.f / 25.f;
        cast.castImpact = 10.f / 25.f;
        cast.castRate = 25;
        return true;
    }
    std::string weapon = "hth";
    const auto weaponSet = state().player.weaponSet;
    for (auto slot : {weaponHandSlot(false, weaponSet), weaponHandSlot(true, weaponSet)}) {
        const auto *item = inventory_.item(inventory_.equipped(playerContainers_, slot));
        const auto *definition = item ? inventory_.catalog().find(item->definition) : nullptr;
        if (!definition || !definition->equipment.isType("weap")) continue;
        weapon = definition->base.weaponClass;
        if (definition->equipment.twoHanded && (!definition->equipment.oneOrTwoHanded ||
            !inventory_.equipped(playerContainers_, slot == weaponHandSlot(false, weaponSet) ?
                weaponHandSlot(true, weaponSet) : weaponHandSlot(false, weaponSet))))
            weapon = definition->equipment.twoHandWeaponClass;
    }
    auto timing = content_.skills.castTimings.find(characterAppearance() + "sc" + weapon);
    if (timing == content_.skills.castTimings.end())
        timing = content_.skills.castTimings.find(characterAppearance() + "schth");
    if (timing == content_.skills.castTimings.end()) return false;
    const auto &animation = timing->second;
    const int faster = std::max(0, characterStats().combat.fasterCast);
    const int rate = std::min(175, 100 + int(int64_t(120) * faster / (120 + faster)));
    const int speed = std::max(1, animation.speed * rate / 100);
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
    for (const auto &[id, skill] : content_.skills.skills) {
        if (!skill.coldPiercePerRank || skill.classCode != characterDefinition_.code) continue;
        const int rank = effectiveSkillRank(id);
        if (rank <= 0) return 0;
        const auto [base, perLevel] = *skill.coldPiercePerRank;
        return base + (rank - 1) * perLevel;
    }
    return 0;
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
} // namespace d2x
