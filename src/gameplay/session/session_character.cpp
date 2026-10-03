#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <type_traits>

namespace d2x {
namespace {
SkillLearningRule learningRule(const SkillRecord &skill) {
    return {skill.id, skill.classCode, skill.requiredLevel, skill.maximumRank, skill.prerequisites};
}
}
CharacterProgressionContext GameSessionImpl::characterProgressionContext() {
    auto &p = simulation_->state_.player;
    return {p.id, !p.actions.dead, p.character.experience, p.character.level, p.character.unspentAttributes, p.character.unspentSkills, p.character.allocated};
}
CharacterSkillContext GameSessionImpl::characterSkillContext() const {
    auto &p = simulation_->state_.player;
    return {p.id, characterDefinition_.code, p.character.level, !p.actions.dead, p.character.unspentSkills,
            p.character.skillRanks, p.character.skillHotkeys, p.character.selectedSkills, p.character.weaponSet};
}
int GameSessionImpl::nextSkillRequiredLevel(int id) const {
    const auto *skill = content_.skills.find(id);
    return skill ? d2x::nextSkillRequiredLevel(learningRule(*skill), state().player.character.skillRanks) : 0;
}
bool GameSessionImpl::canAllocateSkill(int id) const {
    const auto *skill = content_.skills.find(id);
    return skill && canLearnCharacterSkill(characterSkillContext(), learningRule(*skill));
}
void GameSessionImpl::applyCharacterIntent(const CharacterIntent &intent) {
    std::visit([&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, AllocateAttribute>) {
            if (allocateCharacterAttribute(characterProgressionContext(), value.attribute)) refreshCharacter(true);
        } else if constexpr (std::is_same_v<T, AllocateSkill>) {
            const auto *skill = content_.skills.find(value.id);
            if (skill && learnCharacterSkill(characterSkillContext(), learningRule(*skill))) refreshCharacter();
        } else {
            const auto *skill = content_.skills.find(value.skill);
            SkillChoiceEligibility eligibility{value.skill, bool(skill), skillAvailable(value.skill),
                                               skill && skill->passive, skill && skill->leftAllowed};
            auto context = characterSkillContext();
            if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                bindCharacterSkill(context, value.index, value.right, eligibility);
            } else if (selectCharacterSkill(context, value.right, eligibility)) {
                auto &p = simulation_->state_.player;
                if (value.right && value.skill != p.skills.channelSkill()) simulation_->skills().stopChannel(simulation_->skillCaster(p.id));
            }
        }
    }, intent);
}
void GameSessionImpl::resetCharacterAttributePoints() {
    if (resetCharacterAttributes(characterProgressionContext())) refreshCharacter();
}
void GameSessionImpl::resetCharacterSkillPoints() {
    auto &p = simulation_->state_.player;
    if (p.actions.dead) return;
    simulation_->skills().stopChannel(simulation_->skillCaster(p.id));
    int rewarded = 0;
    for (const auto &difficulty : p.character.actOneQuests) {
        if (difficulty.at(questIndex(ActOneQuest::DenOfEvil)).stage == uint32_t(DenStage::Rewarded)) ++rewarded;
        if (difficulty.at(questIndex(QuestId::RadamentsLair)).flags & radamentBookUsed) ++rewarded;
    }
    resetCharacterSkills(characterSkillContext(), rewarded);
    refreshCharacter();
}
} // namespace d2x
