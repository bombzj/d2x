#include "session.hpp"
#include <algorithm>

namespace d2x {
int GameSession::effectiveSkillRank(int id) const {
    int rank = 0;
    if (auto learned = state().player.skillRanks.find(id);
        learned != state().player.skillRanks.end()) rank = learned->second;
    for (auto slot : {EquipmentSlot::RightHand, EquipmentSlot::LeftHand}) {
        const auto *item = inventory_.item(inventory_.equipped(playerContainers_, slot));
        if (item && item->grantedSkill == id &&
            inventory_.equipmentRequirements(item->handle(), equipmentActor()) == InventoryError::None)
            ++rank;
    }
    return rank;
}
bool GameSession::skillAvailable(int id) const {
    const auto *entry = content_.skills.find(id);
    if (!entry) return false;
    const auto *tree = content_.skills.tree(characterDefinition_.code);
    if (!tree) return false;
    if (entry->classCode.empty())
        return entry->sourceName == "Attack" ||
            std::find(tree->commonSkills.begin(), tree->commonSkills.end(), id) != tree->commonSkills.end();
    if (entry->classCode != characterDefinition_.code) return false;
    return effectiveSkillRank(id) > 0;
}
} // namespace d2x
