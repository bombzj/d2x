#include "act_four_state.hpp"
#include "rules.hpp"
#include <algorithm>
namespace d2x {
QuestNpcQuery actFourNpc(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    QuestNpcQuery result;
    const auto &izual = records[questIndex(QuestId::FallenAngel)];
    if (izual.stage < 4) {
        const bool ghost = facts.npcClass == "izualghost" && izual.stage == 3 && !(izual.flags & izualGhostSpoken);
        const bool tyrael = facts.npcClass == "tyrael2" && (izual.stage == 0 || izual.stage == 3);
        const QuestSpeechRequest speech{QuestId::FallenAngel, izual.stage == 3 ? "Successful" : izual.stage == 2 ? "EarlyReturn" : izual.stage == 1 ? "AfterInit" : "Init", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, ghost || tyrael, ghost || tyrael, ghost || tyrael});
    }
    std::stable_partition(result.dialogues.begin(), result.dialogues.end(), [](const auto &r) { return r.automatic; });
    const auto &forge = records[questIndex(QuestId::HellsForge)];
    if (forge.stage < 5 && facts.npcClass.starts_with("cain") && records[questIndex(QuestId::Guardian)].stage >= 4) {
        const bool pending = forge.stage == 4 || (forge.stage < 3 && (!forge.stage || !facts.items.soulstone));
        const QuestSpeechRequest speech{QuestId::HellsForge, forge.stage == 4 ? "Successful" : facts.items.soulstone ? "InitHasStone" : "InitNoStone", {}};
        result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending, false, !facts.items.soulstone && forge.stage < 3 ? 1u : 0u});
    }
    const auto &terror = records[questIndex(QuestId::TerrorsEnd)];
    if (terror.stage || izual.stage >= 3 || forge.stage >= 4) {
        const bool pending = facts.npcClass == "tyrael2" ? (terror.stage == 4 && !(terror.flags & terrorPortalOpened)) || !terror.stage :
            facts.npcClass.starts_with("cain") && (terror.flags & terrorCainPending);
        const QuestSpeechRequest speech{QuestId::TerrorsEnd, terror.stage == 4 ? "ExpansionSuccess" : terror.stage ? "AfterInit" : "Init", "Successful"};
        if (terror.stage < 4 || pending) { result.topics.push_back(speech); result.dialogues.push_back({speech, pending, pending, pending}); }
    }
    std::stable_partition(result.dialogues.begin(), result.dialogues.end(), [](const auto &r) { return r.automatic; });
    return result;
}
} // namespace d2x
