#include "character_creation.hpp"
#include "content/classic_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ActorAppearance characterAppearance(const ClassicData &content, const PersistentCharacter &save) {
    const auto character = std::find_if(content.characters.begin(), content.characters.end(), [&](const auto &entry) {
        return entry.name == save.player.characterClass;
    });
    if (character == content.characters.end() || content.armorTypes.empty())
        throw std::runtime_error("Character appearance definition unavailable");
    ActorAppearance result;
    result.token = character->appearance;
    result.weapon = "hth";
    auto &parts = result.components;
    parts.fill(content.armorTypes.front());
    parts[5] = parts[6] = parts[7] = "nil";
    if (result.token == "ne") parts[10] = "ne1";
    auto equipped = [&](EquipmentSlot slot) -> const ItemDefinition * {
        for (const auto &[id, item] : save.inventory.items)
            if (const auto *location = std::get_if<ContainerLocation>(&item.location);
                location && location->container == save.containers.equipment && location->cell == Cell{int(slot), 0})
                return content.items.find(item.definition);
        return nullptr;
    };
    if (const auto *head = equipped(EquipmentSlot::Head); head && !head->appearance.token.empty()) parts[0] = head->appearance.token;
    if (const auto *torso = equipped(EquipmentSlot::Torso)) {
        constexpr std::array<size_t, 6> components{3, 4, 1, 2, 8, 9};
        for (size_t i = 0; i < components.size(); ++i)
            if (!torso->appearance.body[i].empty()) parts[components[i]] = torso->appearance.body[i];
    }
    std::vector<std::string> weapons;
    for (bool left : {false, true}) {
        const auto *item = equipped(weaponHandSlot(left, save.player.weaponSet));
        if (!item || item->appearance.token.empty()) continue;
        const auto &weapon = item->base.weaponClass;
        int component = item->appearance.component;
        if (item->equipment.isType("weap")) {
            if (weapon == "1hs" || weapon == "1ht" || weapon == "ht1") component = left ? 6 : 5;
            weapons.push_back(weapon);
            result.weapon = weapon;
            if (usesTwoHandedWeapon(item->equipment, character->code,
                                   equipped(weaponHandSlot(!left, save.player.weaponSet)) != nullptr))
                result.weapon = item->equipment.twoHandWeaponClass;
        }
        if (component >= 5 && component <= 7) parts[size_t(component)] = item->appearance.token;
    }
    if (weapons.size() == 2)
        result.weapon = weapons[0] == "1ht" ? (weapons[1] == "1ht" ? "1jt" : "1st") : (weapons[1] == "1ht" ? "1js" : "1ss");
    return result;
}
} // namespace d2x
