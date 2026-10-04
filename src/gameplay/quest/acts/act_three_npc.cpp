#include "act_three_state.hpp"
#include "rules.hpp"
#include <algorithm>

namespace d2x {
QuestNpcQuery actThreeNpc(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    QuestNpcQuery result;
    const auto &bird = records[questIndex(QuestId::GoldenBird)];
    const auto npc = facts.npcClass;
    for (const auto &prelude : questPreludes)
        if (prelude.act == facts.act && prelude.npcClass == npc && !facts.preludes[size_t(prelude.id)])
            result.prelude = prelude.id;
    const bool cain = npc.starts_with("cain");
    std::string_view state;
    bool automatic = false;
    if (bird.stage == uint32_t(GoldenBirdStage::Brewing)) {
        state = "Successful"; automatic = npc == "alkor";
    } else if (bird.stage < 5 && facts.items.goldenBird) {
        state = npc == "alkor" ? "AfterInit" : "Init3";
        automatic = npc == "alkor" || (cain && bird.stage < uint32_t(GoldenBirdStage::BirdExplained));
    } else if (bird.stage < 5 && facts.items.jadeFigurine) {
        state = cain || npc == "asheara" ? "Init1" : "Init2";
        automatic = npc == "meshif2" || (cain && bird.stage < uint32_t(GoldenBirdStage::FigurineExplained));
    }
    if (!state.empty()) {
        const QuestSpeechRequest speech{QuestId::GoldenBird, state, {}};
        result.topics.push_back(speech);
        result.dialogues.push_back({speech, automatic, automatic, automatic});
    }
    const auto &blade = records[questIndex(QuestId::BladeOfTheOldReligion)];
    if (blade.stage < uint32_t(GidbinnStage::Rewarded)) {
        const bool returned = blade.stage == uint32_t(GidbinnStage::Returned);
        const bool ring = returned && npc == "ormus" && !(blade.flags & gidbinnRingGranted);
        const bool merc = returned && npc == "asheara" && !(blade.flags & gidbinnHirelingGranted);
        const bool give = facts.items.gidbinn && npc == "ormus";
        const bool assign = !blade.stage && npc == "hratli";
        const QuestSpeechRequest speech{QuestId::BladeOfTheOldReligion,
            ring ? "Reward" : facts.items.gidbinn || returned ? "Successful" :
            !blade.stage ? "Init" : blade.stage == 1 ? "AfterInit" : "EarlyReturn", {}};
        const bool pending = ring || merc || give || assign;
        result.topics.push_back(speech);
        result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &khalim = records[questIndex(QuestId::KhalimsWill)];
    if (cain && khalim.stage < 4 && (bird.stage >= 5 || khalim.stage ||
        std::any_of(facts.items.khalim.begin(), facts.items.khalim.end(), [](bool held) { return held; }))) {
        std::string_view explanationState;
        uint32_t explanation = 0;
        for (const auto &[part, name] : std::array{std::pair{4, "Successful"}, std::pair{3, "EarlyReturnFlail"},
            std::pair{2, "EarlyReturnHeart"}, std::pair{1, "EarlyReturnBrain"}, std::pair{0, "EarlyReturnEye"}})
            if (facts.items.khalim[size_t(part)] && !(khalim.flags & (1u << part))) {
                explanationState = name; explanation = 1u << part; break;
            }
        if (explanationState.empty()) { explanationState = "Init"; explanation = khalimAssigned; }
        const bool pending = !(khalim.flags & explanation);
        const QuestSpeechRequest speech{QuestId::KhalimsWill, explanationState, {}};
        result.topics.push_back(speech);
        result.dialogues.push_back({speech, pending, pending, pending, false, explanation});
    }
    const auto &tome = records[questIndex(QuestId::LamEsensTome)];
    if (tome.stage < 4 && (facts.lamTomeAvailable || tome.stage || facts.items.lamTome)) {
        const bool pending = npc == "alkor" && (!tome.stage || facts.items.lamTome);
        const QuestSpeechRequest speech{QuestId::LamEsensTome, facts.items.lamTome ? "Successful" :
            !tome.stage ? "Init" : tome.stage == 1 ? "AfterInit" : "EarlyReturn", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &temple = records[questIndex(QuestId::BlackenedTemple)];
    if (temple.stage < 4 && (temple.stage || tome.stage >= 4 || facts.lamTomeAvailable)) {
        const bool pending = (!temple.stage && npc == "ormus") || (temple.stage == 3 && cain);
        const QuestSpeechRequest speech{QuestId::BlackenedTemple, temple.stage == 3 ? "Successful" :
            temple.stage == 2 ? "EarlyReturn" : temple.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &guardian = records[questIndex(QuestId::Guardian)];
    if (guardian.stage || temple.stage >= 3) {
        const bool pending = guardian.flags & guardianSpeechPending;
        const bool assign = guardian.stage < 2 && npc == "ormus";
        const QuestSpeechRequest speech{QuestId::Guardian, pending ? "Successful" :
            guardian.stage >= 2 ? "EarlyReturn" : guardian.stage == 1 ? "AfterInit" : "Init", {}};
        if (guardian.stage < 4 || pending) {
            result.topics.push_back(speech); result.dialogues.push_back({speech, pending || assign, pending || assign, pending || assign});
        }
    }
    std::stable_partition(result.dialogues.begin(), result.dialogues.end(), [](const auto &request) { return request.automatic; });
    return result;
}
} // namespace d2x
