#include "gameplay/quest/acts/act_two_state.hpp"
#include "rules.hpp"

namespace d2x {
QuestNpcQuery actTwoNpc(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    QuestNpcQuery result;
    const auto npc = facts.npcClass;
    for (const auto &prelude : questPreludes)
        if (prelude.act == facts.act && prelude.npcClass == npc && !facts.preludes[size_t(prelude.id)])
            result.prelude = prelude.id;
    auto add = [&](QuestId id, std::string_view state, bool automatic = false, uint32_t explanation = 0) {
        const QuestSpeechRequest speech{id, state, {}};
        result.topics.push_back(speech);
        if (automatic) result.dialogues.push_back({speech, true, true, true, false, explanation});
    };
    const auto tombs = records.at(questIndex(QuestId::SevenTombs)).stage;
    if (facts.level == 73) {
        if (npc == "tyrael1" && tombs >= uint32_t(TombsStage::DurielSlain)) {
            const QuestSpeechRequest speech{QuestId::SevenTombs, {}, {}, true};
            result.topics.push_back(speech);
            const bool pending = tombs == uint32_t(TombsStage::DurielSlain);
            result.dialogues.push_back({speech, pending, pending, pending});
        }
        return result;
    }
    const auto radament = records.at(questIndex(QuestId::RadamentsLair)).stage;
    if (radament < uint32_t(RadamentStage::Rewarded))
        add(QuestId::RadamentsLair, radament == 0 ? "Init" : radament == 1 ? "AfterInit" : radament == 2 ? "EarlyReturn" : "Successful",
            npc == "atma" && (radament == 0 || radament == uint32_t(RadamentStage::Slain)));
    const auto &staff = records.at(questIndex(QuestId::HoradricStaff));
    if (staff.stage < uint32_t(StaffStage::Submitted)) {
        std::string_view state;
        uint32_t explanation = 0;
        // A2Q2_CheckItemsAndState checks unacknowledged artifacts in this order.
        if (facts.items.staff) { state = "SuccessfulStaff"; explanation = staffAssemblyExplained; }
        else if (facts.items.cube && !(staff.flags & staffCubeExplained)) { state = "EarlyReturnCube"; explanation = staffCubeExplained; }
        else if (facts.items.scroll && !(staff.flags & staffScrollExplained)) { state = "EarlyReturnScroll"; explanation = staffScrollExplained; }
        else if (facts.items.head && !(staff.flags & staffHeadExplained)) { state = "EarlyReturnCap"; explanation = staffHeadExplained; }
        else if (facts.items.shaft && !(staff.flags & staffShaftExplained)) { state = "EarlyReturnStave"; explanation = staffShaftExplained; }
        else if (facts.items.shaft) state = "EarlyReturnStave";
        else if (facts.items.head) state = "EarlyReturnCap";
        else if (facts.items.scroll) state = "EarlyReturnScroll";
        else if (facts.items.cube) state = "EarlyReturnCube";
        if (!state.empty()) add(QuestId::HoradricStaff, state,
            npc.starts_with("cain") && explanation && !(staff.flags & explanation), explanation);
    }
    const auto sun = records.at(questIndex(QuestId::TaintedSun)).stage;
    if (sun > 0 && sun < uint32_t(SunStage::Confirmed))
        add(QuestId::TaintedSun, sun == 3 ? "Successful" : sun == 1 ? "AfterInit" : "EarlyReturn",
            sun == uint32_t(SunStage::AltarDestroyed) || (sun == uint32_t(SunStage::Darkness) && npc == "drognan"));
    const auto arcane = records.at(questIndex(QuestId::ArcaneSanctuary)).stage;
    const auto summoner = records.at(questIndex(QuestId::Summoner)).stage;
    if (arcane >= uint32_t(ArcaneStage::Entered) && summoner < uint32_t(SummonerStage::Confirmed))
        add(QuestId::Summoner, summoner == 2 ? "Successful" : "EarlyReturn", summoner == uint32_t(SummonerStage::Slain));
    if (sun >= uint32_t(SunStage::AltarDestroyed) || arcane > 0)
        add(QuestId::ArcaneSanctuary, arcane == 0 ? "Init" : arcane < 3 ? "AfterInit" : arcane == 3 ? "EarlyReturn" : "Successful",
            (arcane == 0 && npc == "drognan") || (arcane == 1 && npc == "jerhyn"));
    // A2Q6 assignment is distinct from A2Q0's arrival welcome.
    if (arcane > 0 || tombs > 0)
        add(QuestId::SevenTombs, tombs == 0 ? "Init" : tombs >= 3 ? "Successful" : "AfterInit",
            (tombs == 0 && npc == "jerhyn") || (tombs == 3 && npc == "jerhyn") || (tombs == 4 && npc == "meshif1"));
    for (auto it = result.topics.rbegin(); it != result.topics.rend(); ++it) result.dialogues.push_back({*it});
    return result;
}
} // namespace d2x
