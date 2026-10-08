#include "character_projection.hpp"
#include "content/classic_data.hpp"
#include "content/character/character_display.hpp"
#include "content/character/character_progression.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/spec.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
namespace {
std::optional<double> stat(const CharacterProjectionInput &input, std::string_view name) {
    const auto found = input.stats.find(name);
    return found == input.stats.end() ? std::nullopt : std::optional{found->second};
}
int integer(const CharacterProjectionInput &input, std::string_view name) {
    return int(std::clamp(stat(input, name).value_or(0),
        double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max())));
}
void passiveDetails(std::vector<std::string> &lines, const ClassicData &data, const SkillRecord &entry, int rank) {
    if (rank <= 0) return;
    if (entry.manaRecoveryPerRank)
        lines.push_back("Mana regeneration: +" + std::to_string(skillRankBonus(*entry.manaRecoveryPerRank,rank)) + "%");
    if (entry.fireMasteryPerRank)
        lines.push_back("Fire skill damage: +" + std::to_string(skillRankBonus(*entry.fireMasteryPerRank,rank)) + "%");
    if (entry.lightningMasteryPerRank)
        lines.push_back("Lightning skill damage: +" + std::to_string(skillRankBonus(*entry.lightningMasteryPerRank,rank)) + "%");
    if (entry.coldPiercePerRank)
        lines.push_back("Enemy cold resistance: -" + std::to_string(skillRankBonus(*entry.coldPiercePerRank,rank)) + "%");
    if (entry.passiveContribution.amazon) {
        const auto &passive = *entry.passiveContribution.amazon;
        const int value = amazonPassiveValue(passive, rank);
        lines.push_back(passive.stat == AmazonPassiveStat::Rating ?
            "Attack rating: +" + std::to_string(value) + "%" : "Chance: " + std::to_string(value) + "%");
    }
    if (!entry.passive || entry.classCode != "nec") return;
    const auto &table = data.tables.at("skills");
    for (size_t row = 0; row < table.rows().size(); ++row) {
        if (table.number(row, "Id") != entry.id) continue;
        auto param = [&](int index) { return table.number(row, "Param" + std::to_string(index)).value_or(0); };
        if (entry.sourceName == "Golem Mastery") {
            lines.push_back("Golem life: +" + std::to_string(param(1) + (rank - 1) * param(2)) + "%");
            lines.push_back("Golem attack rating: +" + std::to_string(param(5) + (rank - 1) * param(6)));
            lines.push_back("Golem movement speed: +" + std::to_string(std::min(param(4),
                param(3) + (param(4) - param(3)) * (110 * rank / (rank + 6)) / 100)) + "%");
        } else if (entry.sourceName == "Skeleton Mastery") {
            lines.push_back("Skeleton life: +" + std::to_string(rank * param(1)) + " / Damage: +" + std::to_string(rank * param(2)));
            lines.push_back("Revive life: +" + std::to_string(rank * param(3)) + "% / Damage: +" + std::to_string(rank * param(4)) + "%");
        } else if (entry.sourceName == "Summon Resist")
            lines.push_back("Summon elemental resistance: +" + std::to_string(std::min(param(2),
                param(1) + (param(2) - param(1)) * (110 * rank / (rank + 6)) / 100)) + "%");
        break;
    }
}
}
CharacterView projectCharacterDisplay(const ClassicData &data, const CharacterProjectionInput &input) {
    CharacterView view;
    view.revision = input.revision; view.actor = input.actor; view.name = input.name;
    view.dead = input.dead; view.running = input.running; view.weaponSet = std::min(input.weaponSet, 1u);
    view.selectedSkills = input.selectedSkills; view.skillHotkeys = input.hotkeys;
    const CharacterDefinition *definition = input.characterClass && *input.characterClass < data.characters.size()
        ? &data.characters[*input.characterClass] : nullptr;
    if (definition) { view.className = definition->name; view.classCode = definition->code; }
    view.level = integer(input, "level");
    view.experience = uint64_t(std::max(0., stat(input, "experience").value_or(0)));
    if (definition && stat(input, "level")) {
        const auto &characters = data.tables.at("charstats");
        const auto thresholds = experienceThresholds(data.tables.at("experience"), characters.value(definition->sourceRow, "class"));
        if (view.level >= 0 && size_t(view.level) < thresholds.size()) {
            view.nextLevelKnown = true;
            view.currentLevelExperience = thresholds[size_t(view.level)];
            if (size_t(view.level + 1) < thresholds.size()) view.nextLevelExperience = thresholds[size_t(view.level + 1)];
        }
        if (!thresholds.empty()) view.maximumExperience = thresholds.back();
    }
    constexpr std::array attributeNames{"strength", "dexterity", "vitality", "energy"};
    constexpr std::array resistNames{"fireresist", "coldresist", "lightresist", "poisonresist"};
    for (size_t i = 0; i < attributeNames.size(); ++i) {
        view.attributes[i] = integer(input, attributeNames[i]);
        view.resistances[i] = integer(input, resistNames[i]);
    }
    view.unspentAttributes = integer(input, "statpts"); view.unspentSkills = integer(input, "newskills");
    view.hp = input.life.value_or(float(stat(input, "hitpoints").value_or(0)));
    view.mana = input.mana.value_or(float(stat(input, "mana").value_or(0)));
    view.stamina = input.stamina.value_or(float(stat(input, "stamina").value_or(0)));
    view.maxLife = integer(input, "maxhp"); view.maxMana = integer(input, "maxmana"); view.maxStamina = integer(input, "maxstamina");
    if (view.dead) view.hp = 0;
    else if (input.aliveAfterDeathSave && view.hp < 1 && view.maxLife > 0) view.hp = 1;
    view.defense = integer(input, "armorclass");
    view.physicalResist = integer(input, "damageresist"); view.magicResist = integer(input, "magicresist");
    view.flatPhysicalReduction = integer(input, "normal_damage_reduction"); view.flatMagicReduction = integer(input, "magic_damage_reduction");
    view.poisonLengthResist = integer(input, "item_poisonlengthresist"); view.fireAbsorbPercent = integer(input, "item_absorbfire_percent");
    if (stat(input,"item_fastercastrate")) view.fasterCast=integer(input,"item_fastercastrate");
    for (std::string name : {"strength", "dexterity", "vitality", "energy", "level", "experience", "statpts", "newskills",
        "hitpoints", "maxhp", "mana", "maxmana", "stamina", "maxstamina", "armorclass", "toblock",
        "fireresist", "coldresist", "lightresist", "poisonresist", "damageresist", "magicresist",
        "normal_damage_reduction", "magic_damage_reduction", "item_poisonlengthresist", "item_absorbfire_percent"})
        if (!stat(input, name) && !(name == "hitpoints" && (input.life || view.dead)) &&
            !(name == "mana" && input.mana) && !(name == "stamina" && input.stamina)) view.unknownStats.insert(std::move(name));
    view.unknownStats.insert("toblock"); // Final block needs shield/dexterity/level, not just native toblock.
    view.attackUsable = !view.dead && !input.town;
    CharacterAttributes attributes;
    EquipmentStats equipment;
    CharacterDisplayContext display{attributes, equipment, input.baseRanks,
        input.fireMastery.value_or(0), input.lightningMastery.value_or(0)};
    attributes.combat.coldSkillDamagePercent = input.coldDamagePercent.value_or(0);
    view.attack = describeCharacterAction(display, nullptr);
    const auto *tree = data.skills.tree(view.classCode);
    if (tree) { view.hasSkillTree = true; view.pageNames = tree->pageNames; }
    for (auto &choices : view.choices) choices.push_back(std::nullopt);
    const auto baseKnown = [&](int id) { return input.baseRanksAssigned || input.baseRanks.contains(id); };
    const auto effectiveKnown = [&](int id) { return input.effectiveRanks.contains(id) ||
        (input.baseRanksAssigned && !input.baseRanks.contains(id)); };
    const auto effectiveRank = [&](int id) {
        const auto found = input.effectiveRanks.find(id);
        return found == input.effectiveRanks.end() ? 0 : found->second;
    };
    for (const auto &[id, entry] : data.skills.skills) {
        const int effective = effectiveRank(id);
        if (entry.classCode != view.classCode && !entry.classCode.empty() && effective <= 0) continue;
        CharacterSkillView skill;
        skill.id = id; skill.page = entry.page; skill.row = entry.row; skill.column = entry.column;
        skill.listRow = entry.listRow; skill.listPool = entry.listPool; skill.iconCell = entry.iconCell;
        skill.classCode = entry.classCode; skill.name = entry.name;
        skill.baseRankKnown = baseKnown(id); skill.effectiveRankKnown = effectiveKnown(id);
        const auto learned = input.baseRanks.find(id);
        skill.baseRank = learned == input.baseRanks.end() ? 0 : learned->second;
        skill.effectiveRank = effective; skill.maximumRank = entry.maximumRank;
        skill.nextRequiredLevel = entry.requiredLevel + skill.baseRank;
        skill.passive = entry.passive; skill.leftAllowed = entry.leftAllowed;
        if (!skill.passive) skill.action = {"?", ""};
        const bool innate = tree && std::find(tree->commonSkills.begin(), tree->commonSkills.end(), id) != tree->commonSkills.end();
        SkillEligibilityInput eligibilityInput;
        eligibilityInput.classCode = view.classCode;
        if (skill.baseRankKnown) eligibilityInput.baseRank = skill.baseRank;
        if (skill.effectiveRankKnown) eligibilityInput.effectiveRank = effective;
        if (stat(input, "level")) eligibilityInput.level = view.level;
        if (stat(input, "newskills")) eligibilityInput.skillPoints = view.unspentSkills;
        for (size_t i = 0; i < attributeNames.size(); ++i)
            if (stat(input, attributeNames[i])) eligibilityInput.attributes[i] = view.attributes[i];
        for (const int required : entry.prerequisites)
            if (const auto rank = input.baseRanks.find(required); rank != input.baseRanks.end())
                eligibilityInput.prerequisiteRanks.emplace(required, rank->second);
        eligibilityInput.innate = innate; eligibilityInput.dead = view.dead; eligibilityInput.town = input.town;
        if (entry.basicAction == BasicSkillAction::Throw || entry.basicAction == BasicSkillAction::LeftHandThrow)
            eligibilityInput.equipmentReady = input.throwReady[entry.basicAction == BasicSkillAction::LeftHandThrow ? 1 : 0];
        if (stat(input, "mana") || input.mana) eligibilityInput.mana = view.mana;
        std::optional<SkillCastSpec> currentCast;
        if (!entry.passive && entry.spell && effective > 0 && effective <= 255) {
            currentCast = resolveSkill(entry.spell->rules(), {effective, input.baseRanks, display.fireMastery,
                display.lightningMastery, attributes.combat.coldSkillDamagePercent});
            eligibilityInput.requiredMana = std::max(float(entry.spell->startMana), currentCast->manaCost);
        }
        const auto eligibility = evaluateSkillEligibility(entry, eligibilityInput);
        skill.available = eligibility.available; skill.canAllocate = eligibility.canAllocate;
        skill.usableNow = eligibility.usableNow; skill.pickerEnabled = eligibility.pickerEnabled;
        auto hints = [&](int rank, const std::map<int, int> &baseRanks) {
            std::vector<std::string> lines;
            if (entry.passive) { passiveDetails(lines, data, entry, rank); return lines; }
            bool known = input.baseRanksAssigned && rank <= 255;
            bool damageKnown = known;
            std::optional<SkillCastSpec> cast;
            std::optional<AuraDefinition> aura;
            if (entry.spell && rank > 0 && rank <= 255) {
                const auto &spec = *entry.spell;
                damageKnown &= (!spec.fireDamage || input.fireMastery.has_value()) &&
                    (!spec.lightningDamage || input.lightningMastery.has_value()) &&
                    (!spec.coldDamage || input.coldDamagePercent.has_value());
                known &= !spec.summon; // Pet attributes need a separate complete input.
                cast = rank == effective ? currentCast : std::optional{resolveSkill(spec.rules(),
                    {rank, baseRanks, display.fireMastery, display.lightningMastery,
                     attributes.combat.coldSkillDamagePercent})};
            }
            // Aura damage can include mastery/equipment and Prayer. Keep its
            // numbers unknown until those inputs exist rather than defaulting them.
            if (entry.auraImplemented && rank > 0 && input.baseRanksAssigned && input.fireMastery &&
                input.lightningMastery && input.coldDamagePercent && effectiveKnown(99))
                aura = resolveAura(data, id, rank, baseRanks, display.fireMastery, display.lightningMastery,
                    attributes.combat.coldSkillDamagePercent, id == 99 ? rank : effectiveRank(99));
            display.spellDamageKnown = known && damageKnown;
            if (rank == effective) skill.action = describeCharacterAction(display, &entry, rank, skill.available);
            return describeSkillPicker(entry, cast ? &*cast : nullptr, aura ? &*aura : nullptr,
                skill.baseRank, input.difficulty ? data.tables.at("difficultylevels").number(*input.difficulty, "AiCurseDiv").value_or(1) : 1,
                known && (!cast || !cast->curse || input.difficulty.has_value()), damageKnown);
        };
        if (skill.effectiveRankKnown && effective > 0) skill.pickerTooltip = hints(effective, input.baseRanks);
        else if (innate) {
            skill.action = describeCharacterAction(display, &entry, effective, true);
            skill.pickerTooltip = describeSkillPicker(entry, nullptr, nullptr, skill.baseRank, 1);
        }
        else skill.pickerTooltip.clear();
        const auto currentDetails = skill.pickerTooltip;
        skill.treeTooltip.push_back(entry.name);
        if (!entry.description.empty()) skill.treeTooltip.push_back(entry.description);
        if (skill.baseRankKnown && stat(input, "level") && view.level < skill.nextRequiredLevel)
            skill.treeTooltip.push_back("Requires level " + std::to_string(skill.nextRequiredLevel));
        for (int required : entry.prerequisites)
            if (baseKnown(required) && (!input.baseRanks.contains(required) || input.baseRanks.at(required) <= 0))
                if (const auto *prerequisite = data.skills.find(required)) skill.treeTooltip.push_back("Requires " + prerequisite->name);
        if (skill.effectiveRankKnown && effective > 0) {
            skill.treeTooltip.push_back("");
            skill.treeTooltip.push_back(data.skills.currentLevelLabel + std::to_string(effective));
            if (effective > 0) skill.treeTooltip.insert(skill.treeTooltip.end(), currentDetails.begin(), currentDetails.end());
        } else if (!skill.effectiveRankKnown) {
            skill.treeTooltip.push_back("");
            skill.treeTooltip.push_back(data.skills.currentLevelLabel + "?");
        }
        if (skill.baseRankKnown && skill.effectiveRankKnown && skill.baseRank < skill.maximumRank && effective < 255) {
            auto nextRanks = input.baseRanks; ++nextRanks[id];
            skill.treeTooltip.push_back("");
            skill.treeTooltip.push_back(effective == 0 ? data.skills.firstLevelLabel :
                data.skills.nextLevelLabel);
            const auto next = hints(effective + 1, nextRanks);
            skill.treeTooltip.insert(skill.treeTooltip.end(), next.begin(), next.end());
        }
        skill.treeBonusHeading = entry.bonusHeading;
        skill.treeBonusTooltip = entry.bonusDescriptions;
        skill.pickerTooltip.clear();
        if (!entry.shortDescription.empty()) skill.pickerTooltip.push_back(entry.shortDescription);
        if (!entry.classCode.empty()) {
            skill.pickerTooltip.push_back("");
            skill.pickerTooltip.push_back(data.skills.currentLevelLabel +
                (skill.effectiveRankKnown ? std::to_string(effective) : "?"));
        }
        skill.pickerTooltip.insert(skill.pickerTooltip.end(), currentDetails.begin(), currentDetails.end());
        if (skill.available && !skill.passive && id != 0) {
            view.choices[1].push_back(id); if (skill.leftAllowed) view.choices[0].push_back(id);
        }
        view.skills.emplace(id, std::move(skill));
    }
    return view;
}
} // namespace d2x
