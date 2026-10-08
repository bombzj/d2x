#include "character_content.hpp"
#include "skill_content.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace d2x {
std::shared_ptr<const server::EquipmentRules> prepareEquipmentRules(const ClassicData &data, const PersistentCharacter &saved) {
    const auto levels = data.experienceByClass.at(saved.player.characterClass).size();
    auto equipment = std::make_shared<server::EquipmentRules>();
    std::set<std::string> sets;
    for (const auto &[id, original] : saved.inventory.items) {
        auto item = original;
        // Decode properties even when hidden; qualification decides activation.
        item.identified = true;
        server::EquipmentValues values;
        if (item.quality == ItemQuality::Unique && item.specialRow >= 0) values.singleCarry = data.tables.at("uniqueitems").number(size_t(item.specialRow), "carry1").value_or(0) != 0;
        values.levels.resize(levels);
        for (size_t level = 1; level < levels; ++level) {
            auto &value = values.levels[level];
            value.stats = resolveItemStats(data, item, int(level));
            value.maximumDurability = itemMaximumDurability(data, item, value.stats);
            for (size_t index = 0; index < item.savedSetStats.size(); ++index) {
                if (item.savedSetStats[index].empty()) continue;
                auto bonus = item;
                bonus.savedStats = item.savedSetStats[index];
                // A conditional list never includes the host's socket/runeword stats again.
                bonus.socketedItems.clear(); bonus.runewordStats.clear(); bonus.runewordRow = -1;
                value.setStats[index] = resolveOwnItemStats(data, bonus, int(level));
            }
        }
        equipment->items.emplace(id, std::move(values));
        if (item.quality == ItemQuality::Set)
            for (const auto &piece : data.equipmentSets) if (piece.row == item.specialRow) sets.insert(piece.set);
    }
    for (const auto &piece : data.equipmentSets) {
        if (!sets.contains(piece.set)) continue;
        equipment->sets.push_back(piece);
        for (const auto &bonus : piece.bonuses) {
            if (!bonus.fixedValue) continue; // Activation explicitly rejects an unresolved roll.
            const auto &property = data.setItems.at(piece.instruction).setBonuses.at(bonus.instruction).property;
            if (property.directRoll && !property.minimum) throw std::runtime_error("Missing fixed set property roll");
            auto &values = equipment->setBonuses[{piece.instruction, bonus.instruction}];
            values.resize(levels);
            for (size_t level = 1; level < levels; ++level)
                values[level] = resolvePropertyStats(data, property, property.minimum.value_or(0), int(level));
        }
    }
    return equipment;
}
void prepareCharacterRules(server::PreparedRules &rules, const ClassicData &data, const PersistentCharacter &saved) {
    rules.potions = std::make_shared<const server::PotionRules>(data.potions);
    auto character = std::make_shared<server::CharacterRules>();
    character->experience = data.experienceByClass.at(saved.player.characterClass);
    character->resistancePenalty = data.resistancePenalty.at(size_t(saved.difficulty));
    for (const auto &[id, skill] : data.skills.skills)
        character->learning.emplace(id, server::LearningRule{skill.classCode, skill.page, skill.requiredLevel,
            skill.maximumRank, skill.requiredAttributes, skill.prerequisites, skill.passiveContribution, skill.manaRecoveryPerRank, !skill.passive, skill.leftAllowed});
    const size_t levels = character->experience.size();
    if (levels < 3 || character->experience.back() > UINT32_MAX || saved.player.level < 1 || size_t(saved.player.level) >= levels)
        throw std::runtime_error("Missing character progression rules");
    auto equipment = prepareEquipmentRules(data, saved);
    const auto definition = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &entry) { return entry.name == saved.player.characterClass; });
    if (definition == data.characters.end()) throw std::runtime_error("Missing player death class");
    const auto &death = data.playerDeath.timings.at(definition->appearance + "dthth");
    character->deathExperiencePenalty = data.playerDeath.experiencePenalty.at(size_t(saved.difficulty));
    character->deathTicks = std::max(1, (death.frames * 256 + death.speed - 1) / death.speed);
    prepareSkillRules(rules, data, *definition);
    rules.equipment = std::move(equipment);
    const auto &skills=data.tables.at("skills");
    for(size_t row=0;row<skills.rows().size();++row) {
        const auto name=skills.value(row,"skill"); const bool book=name=="Book of Townportal";
        if(!book && name!="Scroll of Townportal") continue;
        const auto id=skills.number(row,"Id"); if(!id) continue;
        const auto &misc=data.tables.at("misc");
        for(size_t itemRow=0;itemRow<misc.rows().size();++itemRow) {
            const std::string code(misc.value(itemRow,"code")); const auto *def=data.items.find(code);
            if(def && (book?data.isPortalScroll(def->bookScroll):data.isPortalScroll(code))) character->portalItems.emplace(code,server::PortalItemRule{*id,book});
        }
    }
    for (const auto &[code, item] : data.items.entries()) {
        if (data.isIdentifyScroll(code)) character->identificationItems.emplace(code, false);
        else if (data.isIdentifyScroll(item.bookScroll)) character->identificationItems.emplace(code, true);
    }
    rules.character = std::move(character);
}
}
