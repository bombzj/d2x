#include "scene_assets.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadHeroEquipment(const GameSession &session) {
    const auto &appearance = session.characterAppearance();
    const auto &armorTypes = session.content().armorTypes;
    if (armorTypes.empty())
        throw std::runtime_error("MPQ ArmType has no base character appearance");
    std::array<std::string, 16> baseParts;
    baseParts.fill(armorTypes.front());
    baseParts[5] = baseParts[6] = baseParts[7] = "nil";
    // The original Necromancer COF uses its class-specific S3 component in the base body.
    if (appearance == "ne") baseParts[10] = "ne1";
    auto parts = baseParts;
    std::string appearanceIssue;
    const auto &inventory = session.inventory();
    const auto &containers = session.playerContainers();
    auto equipped = [&](EquipmentSlot slot) -> const ItemDefinition * {
        const auto *item = inventory.item(inventory.equipped(containers, slot));
        return item ? inventory.catalog().find(item->definition) : nullptr;
    };
    if (const auto *head = equipped(EquipmentSlot::Head)) {
        if (head->appearance.component != 0 || head->appearance.token.empty())
            appearanceIssue = "Unverified equipped head appearance: " + head->code;
        else
            parts[0] = head->appearance.token;
    }
    if (const auto *torso = equipped(EquipmentSlot::Torso)) {
        if (torso->appearance.component != 1 ||
            std::any_of(torso->appearance.body.begin(), torso->appearance.body.end(),
                        [](const auto &token) { return token.empty(); })) {
            if (appearanceIssue.empty())
                appearanceIssue = "Unverified equipped torso appearance: " + torso->code;
        } else {
            constexpr std::array<size_t, 6> components{3, 4, 1, 2, 8, 9};
            for (size_t index = 0; index < components.size(); ++index)
                parts[components[index]] = torso->appearance.body[index];
        }
    }
    std::string weapon = "hth";
    int weapons = 0;
    std::array<std::string, 2> weaponClasses;
    for (auto slot : {EquipmentSlot::RightHand, EquipmentSlot::LeftHand}) {
        const auto *definition = equipped(slot);
        if (!definition || definition->appearance.component == 16)
            continue;
        const auto &appearance = definition->appearance;
        if (appearance.component < 5 || appearance.component > 7 || appearance.token.empty()) {
            if (appearanceIssue.empty())
                appearanceIssue = "Unverified equipped hand appearance: " + definition->code;
            continue;
        }
        int index = appearance.component;
        if (definition->equipment.isType("weap")) {
            weaponClasses[weapons++] = definition->base.weaponClass;
            index = slot == EquipmentSlot::LeftHand ? 6 : 5;
            weapon = definition->base.weaponClass;
            if (definition->equipment.twoHanded &&
                (!definition->equipment.oneOrTwoHanded ||
                 !equipped(slot == EquipmentSlot::RightHand ? EquipmentSlot::LeftHand
                                                            : EquipmentSlot::RightHand)))
                weapon = definition->equipment.twoHandWeaponClass;
        }
        parts[index] = appearance.token;
    }
    if (weapons == 2)
        weapon = weaponClasses[0] == "1ht" ? (weaponClasses[1] == "1ht" ? "1jt" : "1st")
                                           : (weaponClasses[1] == "1ht" ? "1js" : "1ss");
    std::string key = appearance + ":" + weapon;
    for (const auto &part : parts)
        key += ":" + part;
    key += ":" + appearanceIssue;
    if (heroKey_ == key)
        return;
    auto pointers = [](const std::array<std::string, 16> &tokens) {
        std::array<const char *, 16> values;
        for (size_t index = 0; index < tokens.size(); ++index)
            values[index] = tokens[index].c_str();
        return values;
    };
    heroFailure_ = appearanceIssue;
    auto cached = heroCache_.find(key);
    if (cached == heroCache_.end()) {
        std::map<std::string, GpuAnimation> animations;
        const auto baseEquipment = pointers(baseParts);
        const auto equipment = pointers(parts);
        for (auto mode : {"nu", "wl", "rn", "a1", "sc", "gh", "dt"}) {
            const bool death = std::string_view(mode) == "dt";
            auto animation = graphics_.composite("chars", appearance, mode, death ? "hth" : weapon,
                                                  death ? &baseEquipment : &equipment);
            if (animation.frames.empty() || !animation.completeComposite) {
                if (!death && heroFailure_.empty())
                    heroFailure_ = "Equipment appearance unavailable: " + std::string(mode) + weapon;
                animation = graphics_.composite("chars", appearance, mode, "hth", &baseEquipment);
                if (animation.frames.empty() || !animation.completeComposite) {
                    if (heroFailure_.empty())
                        heroFailure_ = "Original character mode unavailable: " +
                                       session.characterName() + " " + mode;
                    animation = graphics_.composite("chars", appearance, "nu", "hth", &baseEquipment);
                    if (animation.frames.empty() || !animation.completeComposite)
                        throw std::runtime_error("Base character animation incomplete: " + session.characterName());
                }
            }
            animations.emplace(mode, std::move(animation));
        }
        cached = heroCache_.emplace(key, std::move(animations)).first;
        heroErrors_[key] = heroFailure_;
        if (!heroFailure_.empty())
            std::cerr << heroFailure_ << '\n';
        graphics_.releaseDecoded();
    }
    hero = cached->second;
    heroFailure_ = heroErrors_.at(key);
    heroKey_ = key;
}
} // namespace d2x
