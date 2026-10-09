#include "character_content.hpp"
#include "skill_content.hpp"
#include "content/skills/skill_eligibility.hpp"
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
    for (const auto &[name, state] : data.states) {
        if (state.definition.curable || name == "poison" || name == "freeze") character->healerCureStates.insert(state.definition.id);
        if (name == "nomanaregen") character->noManaRegenState = state.definition.id;
    }
    if (character->noManaRegenState < 0) throw std::runtime_error("Missing original no-mana-regeneration state");
    for (const auto &[id, skill] : data.skills.skills)
        character->learning.emplace(id, server::LearningRule{skill.classCode, skill.page, skill.requiredLevel,
            skill.maximumRank, skill.requiredAttributes, skill.prerequisites, skill.passiveContribution, skill.manaRecoveryPerRank, !skill.passive, skill.leftAllowed});
    const size_t levels = character->experience.size();
    if (levels < 3 || character->experience.back() > UINT32_MAX || saved.player.level < 1 || size_t(saved.player.level) >= levels)
        throw std::runtime_error("Missing character progression rules");
    auto equipment = prepareEquipmentRules(data, saved);
    const auto definition = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &entry) { return entry.name == saved.player.characterClass; });
    if (definition == data.characters.end()) throw std::runtime_error("Missing player death class");
    for (auto id : loadInnateSkillIds(data.tables.at("skills"), data.tables.at("charstats"), definition->sourceRow)) character->innateSkills.insert(id);
    const auto &death = data.playerDeath.timings.at(definition->appearance + "dthth");
    character->deathExperiencePenalty = data.playerDeath.experiencePenalty.at(size_t(saved.difficulty));
    character->deathTicks = std::max(1, (death.frames * 256 + death.speed - 1) / death.speed);
    prepareSkillRules(rules, data, *definition);
    rules.equipment = std::move(equipment);
    const auto &skills=data.tables.at("skills"), &books=data.tables.at("books");
    for(size_t row=0;row<books.rows().size();++row) {
        if(!books.number(row,"Completed").value_or(0)) continue;
        const int spell=books.number(row,"pSpell").value_or(0);
        if(spell!=1 && spell!=2) continue; // Other book spells need their own original program.
        for(bool book : {false,true}) {
            const auto code=books.value(row,book?"BookSpellCode":"ScrollSpellCode");
            const auto name=books.value(row,book?"BookSkill":"ScrollSkill");
            if(!data.items.find(code)) throw std::runtime_error("Missing original book/scroll item");
            bool found=false;
            for(size_t skill=0;skill<skills.rows().size();++skill) if(skills.value(skill,"skill")==name) {
                if(skills.number(skill,"srvdofunc")!=113 || !skills.number(skill,"scroll").value_or(0)) throw std::runtime_error("Unsupported original book skill");
                character->itemSkills.emplace(std::string(code),server::ItemSkillRule{skills.number(skill,"Id").value(),book,
                    spell==1?server::ItemSkillAction::Identify:server::ItemSkillAction::Portal,books.number(row,"SpellIcon").value_or(-1)});
                found=true;break;
            }
            if(!found) throw std::runtime_error("Missing original book skill identity");
        }
    }

    for(const auto &[code,action]:std::array<std::pair<std::string,server::QuestConsumable>,4>{{
        {"ass",server::QuestConsumable::SkillBook},{data.goldenBird.potion,server::QuestConsumable::LifePotion},
        {data.prisonOfIce.scroll,server::QuestConsumable::ResistanceScroll},{"toa",server::QuestConsumable::RespecToken}}}) {
        if(!data.items.find(code) || !data.items.find(code)->usable) throw std::runtime_error("Missing original quest consumable: "+code);
        character->questConsumables.emplace(code,action);
    }
    rules.character = std::move(character);
}
}
