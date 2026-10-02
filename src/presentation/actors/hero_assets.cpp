#include "gameplay/session/session.hpp"
#include "presentation/scene_assets.hpp"
#include "equipment_appearance.hpp"
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
    const auto &attack = session.state().player.weaponAttack;
    auto equipped = [&](EquipmentSlot slot) -> const ItemDefinition * {
        if (attack) return inventory.catalog().find(attack->appearanceDefinitions[size_t(slot)]);
        const auto *item = session.usableEquipment(slot);
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
    const auto &weapon = attack ? attack->weaponClass : session.equipmentStats().animationClass;
    const auto weaponSet = session.state().player.weaponSet;
    for (auto slot : {weaponHandSlot(false, weaponSet), weaponHandSlot(true, weaponSet)}) {
        const auto *definition = equipped(slot);
        if (!definition || definition->appearance.component == 16)
            continue;
        const auto &appearance = definition->appearance;
        if (appearance.component < 5 || appearance.component > 7 || appearance.token.empty()) {
            if (appearanceIssue.empty())
                appearanceIssue = "Unverified equipped hand appearance: " + definition->code;
            continue;
        }
        const int index = equippedHandComponent(*definition, slot == weaponHandSlot(true, weaponSet));
        parts[index] = appearance.token;
    }
    std::string key = appearance + ":" + weapon;
    for (const auto &part : parts)
        key += ":" + part;
    key += ":" + appearanceIssue;
    const auto *primary = equipped(weaponHandSlot(false, weaponSet));
    const auto *secondary = equipped(weaponHandSlot(true, weaponSet));
    if (!primary || !primary->equipment.isType("weap")) primary = secondary;
    const bool normalAttack = !primary || !primary->equipment.isType("tpot");
    const bool throwAttack = primary && primary->equipment.throwable;
    const bool leftSwing = secondary && secondary->equipment.isType("weap") && !secondary->equipment.isType("tpot");
    const bool leftThrow = secondary && secondary->equipment.throwable;
    key += ":" + std::to_string(normalAttack) + std::to_string(throwAttack) +
           std::to_string(leftSwing) + std::to_string(leftThrow);
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
        for (auto mode : {"nu", "wl", "rn", "a1", "th", "s1", "s3", "s4", "sc", "bl", "gh", "dt"}) {
            if (std::string_view(mode) == "bl" && !session.content().skills.attackTimings.contains(appearance + mode + weapon)) continue;
            const bool attackMode = std::string_view(mode) == "a1" || std::string_view(mode) == "th" ||
                                std::string_view(mode) == "s1" || std::string_view(mode) == "s3" || std::string_view(mode) == "s4";
            if ((std::string_view(mode) == "a1" && !normalAttack) ||
                (std::string_view(mode) == "th" && !throwAttack) ||
                (std::string_view(mode) == "s3" && !leftSwing) ||
                (std::string_view(mode) == "s4" && !leftThrow)) continue;
            if (attackMode && !session.content().skills.attackTimings.contains(appearance + mode + weapon)) continue;
            const bool death = std::string_view(mode) == "dt";
            auto animation = graphics_.composite("chars", appearance, mode, death ? "hth" : weapon,
                                                  death ? &baseEquipment : &equipment);
            if (animation.frames.empty() || !animation.completeComposite) {
                if (attackMode) throw std::runtime_error("Original equipped attack art is incomplete: " +
                                                     appearance + mode + weapon);
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
