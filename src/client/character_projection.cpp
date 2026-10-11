#include "character_projection.hpp"
#include "content/classic_data.hpp"
#include "content/character/character_display.hpp"
#include "content/character/character_progression.hpp"
#include "content/skills/skill_description.hpp"
#include "content/skills/description_values.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/resolve.hpp"
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

}
CharacterView projectCharacterDisplay(const ClassicData &data, const CharacterProjectionInput &input) {
    CharacterView view;
    view.attackTiming=input.attackTiming;view.missilePierceChance=input.missilePierceChance;
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
    attributes.strength=view.attributes[0];attributes.dexterity=view.attributes[1];
    EquipmentStats equipment=input.combatEquipment?input.combatEquipment->equipment:EquipmentStats{};
    if(input.combatEquipment) {
        attributes.combat=input.combatEquipment->modifiers.combat;
        const auto &mods=input.combatEquipment->modifiers;
        view.defense=equipment.defense;view.unknownStats.erase("armorclass");
        // The panel uses UNITS_GetBlockRate. The run button is a movement
        // preference, not an active running mode or a combat block penalty.
        view.blockChance=equipment.blockChance;
        view.unknownStats.erase("toblock");
        if(input.difficulty && *input.difficulty<data.resistancePenalty.size()) {
            const int bonus[]={mods.fireResist,mods.coldResist,mods.lightningResist,mods.poisonResist};
            const int caps[]={mods.combat.fireMaxResist,mods.combat.coldMaxResist,mods.combat.lightningMaxResist,mods.combat.poisonMaxResist};
            for(size_t index=0;index<4;++index) {
                // The original unit starts with an empty resistance list. Anya
                // explicitly sends its permanent list with 1D/E/F; an absent
                // entry stays zero after a complete unit/equipment/state admission.
                view.resistances[index]=std::clamp(integer(input,resistNames[index])+bonus[index]+data.resistancePenalty[*input.difficulty],
                    -100,std::clamp(75+caps[index],-100,95));
                view.unknownStats.erase(resistNames[index]);
            }
        }
        view.physicalResist=mods.combat.physicalResist;view.magicResist=mods.combat.magicResist;
        view.flatPhysicalReduction=mods.combat.flatPhysicalReduction;view.flatMagicReduction=mods.combat.flatMagicReduction;
        view.poisonLengthResist=mods.combat.poisonLengthResist;view.fireAbsorbPercent=mods.combat.fireAbsorbPercent;
        for(const auto *name:{"damageresist","magicresist","normal_damage_reduction","magic_damage_reduction","item_poisonlengthresist","item_absorbfire_percent"}) view.unknownStats.erase(name);
    }
    CharacterDisplayContext display{attributes, equipment, input.baseRanks,
        input.fireMastery.value_or(0), input.lightningMastery.value_or(0)};
    display.weaponValuesKnown=input.combatEquipment.has_value();
    if(input.combatEquipment) display.concentrationDamagePercent=input.combatEquipment->concentrationDamagePercent;
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
        if (const auto quantity = input.itemSkillQuantities.find(id); quantity != input.itemSkillQuantities.end())
            eligibilityInput.itemQuantity = quantity->second;
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
        // Native picker color uses SKILLS_GetUseState, not town permission or
        // point-allocation eligibility. AURA exits before equipment/mana checks.
        skill.pickerTooltipUnavailable = !entry.inGame || !skill.available || entry.passive || view.dead;
        if (!entry.aura) {
            if (!skill.pickerEnabled) skill.pickerTooltipUnavailable = true;
            if (const auto ready = skillEquipmentReady(entry, input.handEquipment); ready && !*ready)
                skill.pickerTooltipUnavailable = true;
            if (data.skills.descriptions && input.resourceStates) {
                const auto &catalog = *data.skills.descriptions;
                const auto source = catalog.skills.find(id);
                if (source != catalog.skills.end()) {
                    const auto rank = innate ? std::optional{std::max(1, effective)} :
                        skill.effectiveRankKnown ? std::optional{effective} : std::nullopt;
                    SkillDescriptionInput resourceInput{id, rank, input.baseRanks, input.effectiveRanks, input.stats};
                    SkillDescriptionValues values(catalog, resourceInput, source->second);
                    const auto resource = input.resourceStates->bloodMana ?
                        (input.life ? std::optional<double>{*input.life} : stat(input, "hitpoints")) :
                        (eligibilityInput.mana ? std::optional<double>{*eligibilityInput.mana} : std::nullopt);
                    // Native use-state checks the raw cost without MinMana;
                    // item-owned charges bypass it. SrvDo116's transformed
                    // exception needs a shape snapshot not projected here.
                    if (input.chargedSkill != id && entry.serverDoFunction != 116)
                        if (const auto required = values.mana(false); required && resource && *resource * 256 < *required)
                            skill.pickerTooltipUnavailable = true;
                    if (!input.resourceStates->inferno && eligibilityInput.mana && *eligibilityInput.mana < entry.startMana)
                        skill.pickerTooltipUnavailable = true;
                }
            }
        }
        // Native attribute-panel programs and tooltip programs share MPQ
        // formulas, while keeping their distinct display dispatch tables.
        display.spellDamageKnown = input.baseRanksAssigned && effective <= 255 && entry.spell &&
            (!entry.spell->fireDamage || input.fireMastery.has_value()) &&
            (!entry.spell->lightningDamage || input.lightningMastery.has_value()) &&
            (!entry.spell->coldDamage || input.coldDamagePercent.has_value()) && !entry.spell->summon;
        if(data.skills.descriptions) {
            SkillDescriptionInput action{id,innate?std::optional{std::max(1,effective)}:std::optional{effective},input.baseRanks,input.effectiveRanks,input.stats};
            action.baseRanksComplete=input.baseRanksAssigned;action.difficulty=input.difficulty;
            action.fireDamagePercent=input.fireMastery;action.lightningDamagePercent=input.lightningMastery;action.coldDamagePercent=input.coldDamagePercent;
            if(input.combatEquipment) action.weaponDamage=std::pair{equipment.weapons[0].minimum/256,equipment.weapons[0].maximum/256};
            skill.action=describeCharacterAction(display,*data.skills.descriptions,action,entry,skill.available);
        }
        auto hints = [&](std::optional<int> rank, const std::map<int, int> &baseRanks,
                         SkillDescriptionContext context) {
            if (!data.skills.descriptions) return SkillDescriptionResult{};
            SkillDescriptionInput description{id, rank, baseRanks, input.effectiveRanks, input.stats};
            description.baseRanksComplete = input.baseRanksAssigned;
            description.difficulty = input.difficulty;
            description.fireDamagePercent = input.fireMastery;
            description.lightningDamagePercent = input.lightningMastery;
            description.coldDamagePercent = input.coldDamagePercent;
            if (const auto quantity = input.itemSkillQuantities.find(id); quantity != input.itemSkillQuantities.end())
                description.quantity = quantity->second;
            return describeSkill(*data.skills.descriptions, description, context);
        };
        const std::optional<int> descriptionRank = innate ? std::optional{std::max(1,effective)} :
            skill.effectiveRankKnown ? std::optional{effective} : std::nullopt;
        // Native tree calls dsc2/dsc3 with next effective rank, and reads
        // cross-skill/base-rank callbacks from the unchanged character.
        const std::optional<int> treeDescriptionRank = skill.effectiveRankKnown ? std::optional{effective+1} : std::nullopt;
        const auto currentDetails = hints(descriptionRank, input.baseRanks, SkillDescriptionContext::Picker).lines;
        // Native D2Client 33C2E / 33E44 / 34007 distinguishes a learned
        // skill from an item grant by its base and effective levels. This is
        // shared display metadata, independent of the skill or server flavor.
        const bool itemGranted=skill.baseRankKnown && skill.effectiveRankKnown && skill.baseRank==0 && effective>0 && !innate;
        if (itemGranted && data.skills.descriptions)
            skill.treeGrantedHeading=data.skills.descriptions->label("StrSkill118");
        skill.treeTooltip.push_back(entry.name);
        if (!entry.description.empty()) skill.treeTooltip.push_back(entry.description);
        // Native tree RVA 7899C selects the red/requirement branch from node
        // eligibility, not just whether its effective rank is zero. Learned
        // nodes stay normal when no skill points are left; unknown inputs do
        // not establish that a requirement has failed.
        const bool noSkillPoints = stat(input, "newskills") && view.unspentSkills == 0;
        bool treeRequirementsKnown = input.baseRanksAssigned && skill.effectiveRankKnown &&
            stat(input, "level") && stat(input, "newskills");
        for (size_t i = 0; i < entry.requiredAttributes.size(); ++i)
            if (entry.requiredAttributes[i] > 0 && !eligibilityInput.attributes[i]) treeRequirementsKnown = false;
        skill.treeTooltipUnavailable = treeRequirementsKnown && !skill.canAllocate && !(noSkillPoints && effective > 0);
        if (skill.treeTooltipUnavailable && data.skills.descriptions) {
            if (skill.baseRank >= skill.maximumRank)
                skill.treeTooltip.push_back(std::string(data.skills.descriptions->label("StrSkill116")));
            else
                skill.treeTooltip.push_back(std::string(data.skills.descriptions->label("skilldesc3")) +
                    std::to_string(entry.requiredLevel + effective));
        }
        const auto general = hints(treeDescriptionRank, input.baseRanks, SkillDescriptionContext::TreeGeneral).lines;
        if (!general.empty()) skill.treeTooltip.push_back("");
        skill.treeTooltip.insert(skill.treeTooltip.end(), general.begin(), general.end());
        if (skill.effectiveRankKnown && effective > 0) {
            skill.treeTooltip.push_back("");
            auto currentHeading=data.skills.currentLevelLabel + std::to_string(effective);
            if (itemGranted && data.skills.descriptions) currentHeading+=data.skills.descriptions->label("StrSkill117");
            skill.treeTooltip.push_back(std::move(currentHeading));
            const auto current = hints(effective, input.baseRanks, SkillDescriptionContext::TreeLevel).lines;
            skill.treeTooltip.insert(skill.treeTooltip.end(), current.begin(), current.end());
        } else if (!skill.effectiveRankKnown) {
            skill.treeTooltip.push_back("");
            skill.treeTooltip.push_back(data.skills.currentLevelLabel + "?");
        }
        if (skill.baseRankKnown && skill.effectiveRankKnown && skill.baseRank < skill.maximumRank && effective < 255) {
            skill.treeTooltip.push_back("");
            skill.treeTooltip.push_back(effective == 0 ? data.skills.firstLevelLabel :
                data.skills.nextLevelLabel);
            const auto next = hints(effective + 1, input.baseRanks, SkillDescriptionContext::TreeLevel).lines;
            skill.treeTooltip.insert(skill.treeTooltip.end(), next.begin(), next.end());
        }
        const auto bonus = hints(treeDescriptionRank, input.baseRanks, SkillDescriptionContext::TreeBonus);
        skill.treeBonusHeading = bonus.bonusHeading;
        skill.treeBonusTooltip = bonus.lines;
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
    for(const auto &charge:input.chargedSkills) {
        auto source=input;source.chargedSkills.clear();source.effectiveRanks[charge.skill]=charge.rank;source.chargedSkill=charge.skill;
        auto projected=projectCharacterDisplay(data,source);
        if(const auto *entry=projected.skill(charge.skill)) {
            auto skill=*entry;skill.owner=uint32_t(charge.item.id.value);skill.canAllocate=false;
            skill.available=skill.available && charge.charges>0;skill.pickerEnabled=skill.pickerEnabled && charge.charges>0;skill.usableNow=skill.usableNow && charge.charges>0;
            skill.pickerTooltipUnavailable=skill.pickerTooltipUnavailable || charge.charges==0;
            if (data.skills.descriptions)
                skill.pickerTooltip.push_back(describeSkillCharges(*data.skills.descriptions,charge.charges,charge.maximum));
            view.chargedSkills.push_back(std::move(skill));
        }
    }
    return view;
}
} // namespace d2x
