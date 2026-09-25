#include "session.hpp"
#include <algorithm>

namespace d2x {
int GameSession::effectiveSkillRank(int id) const {
    int rank = 0;
    if (auto learned = state().player.skillRanks.find(id);
        learned != state().player.skillRanks.end()) rank = learned->second;
    for (auto slot : {weaponHandSlot(false, state().player.weaponSet),
                      weaponHandSlot(true, state().player.weaponSet)}) {
        const auto *item = inventory_.item(inventory_.equipped(playerContainers_, slot));
        if (item && item->quantity && item->grantedSkill == id &&
            (!inventory_.catalog().find(item->definition)->maxDurability || item->durability) &&
            inventory_.equipmentRequirements(item->handle(), equipmentActor()) == InventoryError::None)
            ++rank;
    }
    const auto &mods = characterStats().combat;
    const auto *skill = content_.skills.find(id);
    if (!skill) return rank;
    auto bonus = [](const auto &values, int key) {
        auto found = values.find(key);
        return found == values.end() ? 0 : found->second;
    };
    const bool native = skill->classCode == characterDefinition_.code;
    if (native) rank += bonus(mods.singleSkills, id);
    const int nonClass = bonus(mods.nonClassSkills, id);
    rank += native ? std::min(3, nonClass) : nonClass;
    if (rank > 0) {
        rank += mods.allSkills;
        if (native) {
            rank += bonus(mods.classSkills, int(characterDefinition_.sourceRow));
            if (skill->page > 0)
                rank += bonus(mods.tabSkills, int(characterDefinition_.sourceRow) * 8 + skill->page - 1);
        }
    }
    return std::max(0, rank);
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
