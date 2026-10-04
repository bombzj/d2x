#include "act_five_state.hpp"
#include "rules.hpp"
#include <algorithm>
namespace d2x {
QuestNpcQuery actFiveNpc(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    QuestNpcQuery result;
    const auto &baal = records[questIndex(QuestId::EveOfDestruction)];
    if (baal.stage) {
        const bool pending = baal.stage == 5 && (baalNpcAcknowledgement(facts.npcClass) & ~baal.flags);
        const QuestSpeechRequest speech{QuestId::EveOfDestruction, baal.stage == 5 ? "Successful" : "EarlyReturn", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &ancients = records[questIndex(QuestId::RiteOfPassage)];
    if (ancients.stage || records[questIndex(QuestId::SiegeOnHarrogath)].stage >= 3) {
        const bool pending = (!ancients.stage && facts.npcClass == "qual-kehk") ||
            (ancients.stage == 4 && (ancientNpcAcknowledgement(facts.npcClass) & ~ancients.flags));
        const QuestSpeechRequest speech{QuestId::RiteOfPassage, ancients.stage == 4 ? "Successful" : ancients.stage >= 2 ? "EarlyReturn" : ancients.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &betrayal = records[questIndex(QuestId::BetrayalOfHarrogath)];
    if (betrayal.stage < 5 && (betrayal.stage || records[questIndex(QuestId::PrisonOfIce)].stage >= 5)) {
        const bool pending = facts.npcClass == "drehya" && (!betrayal.stage || betrayal.stage == 3);
        const QuestSpeechRequest speech{QuestId::BetrayalOfHarrogath, betrayal.stage >= 3 ? "Successful" : betrayal.stage == 2 ? "EarlyReturn" : betrayal.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    const auto &ice = records[questIndex(QuestId::PrisonOfIce)];
    if (ice.stage < 6 || (ice.stage >= 5 && !(ice.flags & iceScrollUsed) && !facts.items.resistanceScroll)) {
        const bool scroll = facts.npcClass == "malah" && ice.stage >= 5 && !(ice.flags & iceScrollUsed) && !facts.items.resistanceScroll;
        const bool rare = facts.npcClass == "drehya" && ice.stage >= 5 && !(ice.flags & iceRareGranted);
        const bool potion = facts.npcClass == "malah" && ice.stage >= 3 && ice.stage < 5 && !facts.items.defrostPotion;
        const bool assign = facts.npcClass == "malah" && !ice.stage;
        const QuestSpeechRequest speech{QuestId::PrisonOfIce, ice.stage >= 5 ? "Successful" : ice.stage >= 3 ? "FoundAnya" : ice.stage == 2 ? "EarlyReturn" : ice.stage == 1 ? "AfterInit" : "Init", {}};
        const bool pending = scroll || rare || potion || assign;
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending, false, scroll ? 1u : potion ? 2u : 0u});
    }
    const auto &siege = records[questIndex(QuestId::SiegeOnHarrogath)];
    const auto &rescue = records[questIndex(QuestId::RescueOnMountArreat)];
    if (rescue.stage < 4) {
        const bool pending = facts.npcClass == "qual-kehk" && (!rescue.stage || rescue.stage == 3);
        const QuestSpeechRequest speech{QuestId::RescueOnMountArreat, rescue.stage == 3 ? "Successful" : rescue.stage == 2 ? "EarlyReturn" : rescue.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    if (siege.stage < 5) {
        const bool pending = facts.npcClass == "larzuk" && (!siege.stage || siege.stage == 3);
        const QuestSpeechRequest speech{QuestId::SiegeOnHarrogath, siege.stage >= 3 ? "Successful" : siege.stage == 2 ? "EarlyReturn" : siege.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending});
    }
    std::stable_partition(result.dialogues.begin(), result.dialogues.end(), [](const auto &r) { return r.automatic; });
    return result;
}
} // namespace d2x
