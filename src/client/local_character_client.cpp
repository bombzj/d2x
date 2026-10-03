#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/resolve.hpp"
#include "client/local_character_client.hpp"
#include "content/character/character_display.hpp"
#include "content/classic_data.hpp"
#include "content/skills/aura_data.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/session/session.hpp"
#include "world/region.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
const CharacterView &LocalCharacterClient::read() const {
    if (cached_.revision == session_.viewRevision()) return cached_;
    const auto &p = session_.state().player;
    const auto &stats = session_.characterStats();
    const auto &equipment = session_.equipmentStats();
    const auto &content = session_.content();
    const auto &thresholds = session_.experienceThresholds();
    CharacterView view;
    view.revision = session_.viewRevision();
    view.actor = p.id;
    view.name = p.name;
    view.classCode = session_.characterCode();
    view.className = session_.characterName();
    view.level = p.level;
    view.experience = p.experience;
    view.currentLevelExperience = thresholds.at(size_t(p.level));
    if (size_t(p.level + 1) < thresholds.size()) view.nextLevelExperience = thresholds[size_t(p.level + 1)];
    view.maximumExperience = session_.maximumExperience();
    view.unspentAttributes = p.unspentAttributes;
    view.unspentSkills = p.unspentSkills;
    view.attributes = {stats.strength, stats.dexterity, stats.vitality, stats.energy};
    view.resistances = {stats.fireResist, stats.coldResist, stats.lightningResist, stats.poisonResist};
    view.hp = p.hp; view.mana = p.mana; view.stamina = p.stamina;
    view.maxLife = stats.maxLife; view.maxMana = stats.maxMana; view.maxStamina = stats.maxStamina;
    view.defense = equipment.defense; view.blockChance = equipment.blockChance;
    const auto &combat = stats.combat;
    view.physicalResist = std::clamp(combat.physicalResist, -100, 50);
    view.magicResist = std::clamp(combat.magicResist, -100, 75);
    view.flatPhysicalReduction = combat.flatPhysicalReduction; view.flatMagicReduction = combat.flatMagicReduction;
    view.poisonLengthResist = std::clamp(combat.poisonLengthResist, 0, 100);
    view.fireAbsorbPercent = std::clamp(combat.fireAbsorbPercent, 0, 40);
    view.dead = p.dead; view.running = p.running; view.weaponSet = p.weaponSet;
    view.selectedSkills = p.selectedSkills; view.skillHotkeys = p.skillHotkeys;
    view.blueStamina = std::ranges::any_of(p.combatEffects.entries(), [&](const auto &effect) {
        return effect.activeAt(session_.state().frame) && effect.spec.state.staminaBarBlue;
    });
    const bool safe = session_.region().definition.safe;
    view.attackUsable = !p.dead && !safe;
    const int fireMastery = session_.fireMasteryPercent(), lightningMastery = session_.lightningMasteryPercent();
    CharacterDisplayContext display{stats, equipment, p.skillRanks, fireMastery, lightningMastery};
    view.attack = describeCharacterAction(display, nullptr);
    const auto *tree = content.skills.tree(view.classCode);
    view.hasSkillTree = bool(tree);
    if (tree) view.pageNames = tree->pageNames;
    const int aiCurseDivisor = content.tables.at("difficultylevels").number(
        size_t(session_.state().population.difficulty), "AiCurseDiv").value_or(1);
    for (const auto &[id, entry] : content.skills.skills) {
        if (entry.classCode != view.classCode && !entry.classCode.empty() && !session_.skillAvailable(id)) continue;
        CharacterSkillView skill;
        skill.id = id; skill.page = entry.page; skill.row = entry.row; skill.column = entry.column;
        skill.classCode = entry.classCode; skill.name = entry.name;
        const auto learned = p.skillRanks.find(id);
        skill.baseRank = learned == p.skillRanks.end() ? 0 : learned->second;
        skill.effectiveRank = session_.effectiveSkillRank(id);
        skill.maximumRank = entry.maximumRank;
        skill.nextRequiredLevel = session_.nextSkillRequiredLevel(id);
        skill.passive = entry.passive; skill.leftAllowed = entry.leftAllowed;
        skill.available = session_.skillAvailable(id); skill.canAllocate = session_.canAllocateSkill(id);
        skill.usableNow = !p.dead && entry.executable() && skill.available && (!safe || entry.allowedInTown);
        std::optional<AuraDefinition> aura;
        if (entry.auraImplemented && skill.effectiveRank > 0) {
            // The HUD cost previously resolved the aura without mastery/synergy arguments.
            if (const auto cost = resolveAura(content, id, skill.effectiveRank))
                skill.usableNow &= p.mana >= cost->manaPerPulse;
            aura = resolveAura(content, id, skill.effectiveRank, p.skillRanks, fireMastery, lightningMastery,
                               combat.coldSkillDamagePercent, session_.effectiveSkillRank(99));
        }
        std::optional<SkillCastSpec> cast;
        if (entry.spell && skill.effectiveRank > 0) {
            cast = resolveSkill(*entry.spell, {skill.effectiveRank, p.skillRanks, fireMastery, lightningMastery,
                                combat.coldSkillDamagePercent});
            skill.usableNow &= p.mana >= std::max(p.channelSkill() == id ? 0.f : float(entry.spell->startMana), cast->manaCost);
            if (cast->delayFrames > 0) skill.usableNow &= session_.state().frame >= p.skillDelayUntil;
            if (cast->requiresShield) skill.usableNow &= bool(equipment.shield);
            if (cast->weapon) skill.usableNow &= session_.weaponSkillReady(*cast);
        }
        skill.action = describeCharacterAction(display, &entry, skill.effectiveRank, skill.available);
        skill.pickerTooltip = describeSkillPicker(entry, cast ? &*cast : nullptr, aura ? &*aura : nullptr,
                                                 skill.baseRank, aiCurseDivisor);
        std::string title = entry.name + "  " + std::to_string(skill.baseRank) + "/" + std::to_string(entry.maximumRank);
        if (skill.effectiveRank > skill.baseRank) title += "  Item +" + std::to_string(skill.effectiveRank - skill.baseRank);
        skill.treeTooltip.push_back(std::move(title));
        if (!entry.description.empty()) skill.treeTooltip.push_back(entry.description);
        if (p.level < skill.nextRequiredLevel) skill.treeTooltip.push_back("Requires level " + std::to_string(skill.nextRequiredLevel));
        for (int required : entry.prerequisites)
            if (!p.skillRanks.contains(required) || p.skillRanks.at(required) <= 0)
                if (const auto *prerequisite = content.skills.find(required)) skill.treeTooltip.push_back("Requires " + prerequisite->name);
        if (entry.auraImplemented) {
            if (skill.effectiveRank > 0) {
                skill.treeTooltip.push_back("Current level " + std::to_string(skill.effectiveRank));
                if (aura) {
                    auto lines = describeAuraSkill(entry, *aura, skill.baseRank);
                    skill.treeTooltip.insert(skill.treeTooltip.end(), lines.begin(), lines.end());
                }
            }
            if (skill.baseRank < entry.maximumRank) {
                const int next = std::max(1, skill.effectiveRank + 1);
                auto nextLearned = p.skillRanks;
                ++nextLearned[id];
                skill.treeTooltip.push_back("Next level " + std::to_string(next));
                if (const auto nextAura = resolveAura(content, id, next, nextLearned, fireMastery, lightningMastery,
                    combat.coldSkillDamagePercent, session_.effectiveSkillRank(99) + (id == 99 ? 1 : 0))) {
                    auto lines = describeAuraSkill(entry, *nextAura, nextLearned[id]);
                    skill.treeTooltip.insert(skill.treeTooltip.end(), lines.begin(), lines.end());
                }
            }
        }
        view.skills.emplace(id, std::move(skill));
    }
    for (bool right : {false, true}) {
        auto &choices = view.choices[unsigned(right)];
        choices.push_back(std::nullopt);
        if (!tree) continue;
        for (int id : tree->commonSkills) {
            const auto *entry = content.skills.find(id);
            if (!entry || (!right && !entry->leftAllowed)) continue;
            if (entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw" || entry->sourceName == "Unsummon" ||
                entry->basicAction == BasicSkillAction::Attack || entry->basicAction == BasicSkillAction::LeftHandSwing) choices.push_back(id);
        }
        for (const auto &[id, skill] : view.skills)
            if (skill.classCode == view.classCode && !skill.passive && (right || skill.leftAllowed) && skill.available) choices.push_back(id);
    }
    cached_ = std::move(view);
    return cached_;
}
void LocalCharacterClient::submit(CharacterIntent intent) {
    std::visit([&](auto value) { session_.submit(GameCommand{std::move(value)}); }, std::move(intent));
}
} // namespace d2x
