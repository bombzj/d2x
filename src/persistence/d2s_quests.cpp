#include "gameplay/quest/acts/act_two_state.hpp"
#include "d2s_quests.hpp"
#include "d2s_fixed_sections.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "gameplay/character/record.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
void require(bool condition, const char *reason) {
    if (!condition) throw std::runtime_error(std::string("D2S: ") + reason);
}
uint16_t word(std::span<const uint8_t> bytes, size_t at) {
    require(at + 2 <= bytes.size(), "truncated quest section");
    return uint16_t(bytes[at] | unsigned(bytes[at + 1]) << 8);
}
void putWord(std::span<uint8_t> bytes, size_t at, unsigned value) {
    require(at + 2 <= bytes.size(), "truncated quest section");
    bytes[at] = uint8_t(value); bytes[at + 1] = uint8_t(value >> 8);
}
unsigned questBits(const QuestRecord &quest, size_t index) {
    const auto stage = quest.stage;
    switch (index) {
    case 0: case 1:
        return stage >= 4 ? 0x2001 : stage == 3 ? 0x2002 : stage == 2 ? 0x1C : stage == 1 ? 4 : 0;
    case 2:
        if (quest.flags & cainRescuedByRogues) return 0x8000;
        return stage >= 8 ? 0x2001 : stage >= 7 ? 0x2002 : stage >= 4 ? 0x1C : stage >= 2 ? 0xC : stage == 1 ? 4 : 0;
    case 3: return stage >= 4 ? 0x2001 : stage == 3 ? 0x1C : stage == 2 ? 0xC : stage == 1 ? 4 : 0;
    case 4: return stage >= 6 ? 0x2001 : stage >= 5 ? 0x2002 : stage >= 2 ? 0xC : stage == 1 ? 4 : 0;
    case 5: return stage >= 5 ? 0x2001 : stage >= 3 ? 0x2002 : stage == 2 ? 0x1C : stage == 1 ? 4 : 0;
    }
    return 0;
}
} // namespace
void importD2sQuests(CharacterRecord &player, const D2sFixedSections &sections, const NpcDialogues &dialogues) {
    for (size_t difficulty = 0; difficulty < 3; ++difficulty) {
        for (size_t index = 0; index < questsForAct(0).size(); ++index) {
            const auto flags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId(index)).nativeSlot * 2);
            auto &quest = player.quests[difficulty][index];
            const bool rewarded = flags & 1, pending = flags & 2, entered = flags & 0x10, started = flags & 0xC;
            switch (index) {
            case 0: case 1: quest.stage = rewarded ? 4 : pending ? 3 : entered ? 2 : started ? 1 : 0; break;
            case 2:
                quest.stage = rewarded ? 8 : pending ? 7 : entered ? 4 : started ? 1 : 0;
                quest.flags = !rewarded && (flags & 0x8000) ? cainRescuedByRogues : 0;
                if (quest.flags) quest.stage = uint32_t(CainStage::Rewarded);
                break;
            case 3: quest.stage = rewarded ? 4 : entered ? 3 : flags & 8 ? 2 : started ? 1 : 0; break;
            case 4: quest.stage = rewarded ? 6 : pending ? 5 : flags & 8 ? 2 : started ? 1 : 0; break;
            case 5: quest.stage = rewarded ? 5 : pending ? 3 : entered ? 2 : started ? 1 : 0; break;
            }
        }
        if (sections.quests[10 + difficulty * 96 + 0x52]) {
            require(player.quests[difficulty][0].stage == uint32_t(DenStage::Rewarded), "respec without Den reward");
            player.quests[difficulty][0].flags |= denRespecUsed;
        }
        for (const auto &prelude : questPreludes)
            player.questPreludes[difficulty][size_t(prelude.id)] =
                (word(sections.quests, 10 + difficulty * 96 + prelude.nativeSlot * 2) & 1u) != 0;
        const auto radamentFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::RadamentsLair).nativeSlot * 2);
        auto &radament = player.quests[difficulty][questIndex(QuestId::RadamentsLair)];
        radament.stage = radamentFlags & 1 ? 4 : radamentFlags & 2 ? 3 : radamentFlags & 8 ? 2 : radamentFlags & 4 ? 1 : 0;
        radament.flags = radamentFlags & 0x20 ? radamentBookPending
            : radament.stage >= uint32_t(RadamentStage::Slain) ? radamentBookUsed : 0;
        const auto staffFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::HoradricStaff).nativeSlot * 2);
        auto &staff = player.quests[difficulty][questIndex(QuestId::HoradricStaff)];
        staff.stage = staffFlags & 1 ? 6 : staffFlags & 0xC ? 1 : 0;
        staff.flags = (staffFlags & 8 ? staffScrollExplained : 0) |
            (staffFlags & 0x40 ? staffCubeExplained : 0) |
            (staffFlags & 0x10 ? staffHeadExplained : 0) |
            (staffFlags & 0x20 ? staffShaftExplained : 0) |
            (staffFlags & 0x400 ? staffAssemblyExplained : 0);
        const auto sunFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::TaintedSun).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::TaintedSun)].stage = sunFlags & 1 ? 4 : sunFlags & 2 ? 3 : sunFlags & 8 ? 2 : sunFlags & 4 ? 1 : 0;
        const auto arcaneFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::ArcaneSanctuary).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::ArcaneSanctuary)].stage = arcaneFlags & 1 ? 4 : arcaneFlags & 0x10 ? 3 : arcaneFlags & 8 ? 2 : arcaneFlags & 4 ? 1 : 0;
        const auto summonerFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::Summoner).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::Summoner)].stage = summonerFlags & 1 ? 3 : summonerFlags & 2 ? 2 : summonerFlags & 4 ? 1 : 0;
        const auto tombsFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::SevenTombs).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::SevenTombs)].stage = tombsFlags & 1 ? 5 : tombsFlags & 0x10 ? 4 : tombsFlags & 8 ? 3 : tombsFlags & 0x20 ? 2 : tombsFlags & 4 ? 1 : 0;
        for (const auto &[bit, key] : dialogues.introductionKeys)
            if (sections.introductions[28 + difficulty * 8 + bit / 8] & (1u << (bit % 8)))
                player.npcIntroductions[difficulty].insert(key);
    }
}
void exportD2sQuests(const CharacterRecord &player, D2sFixedSections &sections, const NpcDialogues &dialogues) {
    for (size_t difficulty = 0; difficulty < 3; ++difficulty) {
        for (const auto &prelude : questPreludes) {
            const auto at = 10 + difficulty * 96 + prelude.nativeSlot * 2;
            putWord(sections.quests, at, (word(sections.quests, at) & ~1u) |
                (player.questPreludes[difficulty][size_t(prelude.id)] ? 1u : 0u));
        }
        sections.quests[10 + difficulty * 96 + 0x52] = (player.quests[difficulty][0].flags & denRespecUsed) ? 1 : 0;
        for (size_t index = 0; index < questsForAct(0).size(); ++index) {
            const auto offset = 10 + difficulty * 96 + questDefinition(QuestId(index)).nativeSlot * 2;
            const unsigned mask = 0x201F | (index == 2 ? 0x8000 : 0);
            putWord(sections.quests, offset, (word(sections.quests, offset) & ~mask) |
                questBits(player.quests[difficulty][index], index));
        }
        const auto &radament = player.quests[difficulty][questIndex(QuestId::RadamentsLair)];
        const auto offset = 10 + difficulty * 96 + questDefinition(QuestId::RadamentsLair).nativeSlot * 2;
        const unsigned bits = radament.stage >= 4 ? 0x2001 : radament.stage == 3 ? 0x2002
            : radament.stage == 2 ? 0xC : radament.stage == 1 ? 4 : 0;
        putWord(sections.quests, offset, (word(sections.quests, offset) & ~0x202Fu) | bits |
            (radament.flags & radamentBookPending ? 0x20 : 0));
        const auto &staff = player.quests[difficulty][questIndex(QuestId::HoradricStaff)];
        const auto staffOffset = 10 + difficulty * 96 + questDefinition(QuestId::HoradricStaff).nativeSlot * 2;
        putWord(sections.quests, staffOffset, (word(sections.quests, staffOffset) & ~0x247Du) |
            (staff.stage >= 6 ? 0x2001 : staff.stage ? 4 : 0) | (staff.flags & staffScrollExplained ? 8 : 0) |
            (staff.flags & staffCubeExplained ? 0x40 : 0) |
            (staff.flags & staffHeadExplained ? 0x10 : 0) |
            (staff.flags & staffShaftExplained ? 0x20 : 0) |
            (staff.flags & staffAssemblyExplained ? 0x400 : 0));
        const auto sun = player.quests[difficulty][questIndex(QuestId::TaintedSun)].stage;
        const auto sunOffset = 10 + difficulty * 96 + questDefinition(QuestId::TaintedSun).nativeSlot * 2;
        putWord(sections.quests, sunOffset, (word(sections.quests, sunOffset) & ~0x200Fu) |
            (sun >= 4 ? 0x2001 : sun == 3 ? 0x2002 : sun == 2 ? 0xC : sun == 1 ? 4 : 0));
        const auto arcane = player.quests[difficulty][questIndex(QuestId::ArcaneSanctuary)].stage;
        const auto arcaneOffset = 10 + difficulty * 96 + questDefinition(QuestId::ArcaneSanctuary).nativeSlot * 2;
        putWord(sections.quests, arcaneOffset, (word(sections.quests, arcaneOffset) & ~0x200Fu & ~0x10u) |
            (arcane >= 4 ? 0x2001 : arcane == 3 ? 0x1C : arcane == 2 ? 0xC : arcane == 1 ? 4 : 0));
        const auto summoner = player.quests[difficulty][questIndex(QuestId::Summoner)].stage;
        const auto summonerOffset = 10 + difficulty * 96 + questDefinition(QuestId::Summoner).nativeSlot * 2;
        putWord(sections.quests, summonerOffset, (word(sections.quests, summonerOffset) & ~0x2007u) |
            (summoner >= 3 ? 0x2001 : summoner == 2 ? 0x2002 : summoner == 1 ? 4 : 0));
        const auto tombs = player.quests[difficulty][questIndex(QuestId::SevenTombs)].stage;
        const auto tombsOffset = 10 + difficulty * 96 + questDefinition(QuestId::SevenTombs).nativeSlot * 2;
        putWord(sections.quests, tombsOffset, (word(sections.quests, tombsOffset) & ~0x203Du) |
            (tombs >= 5 ? 0x2001 : tombs == 4 ? 0x2010 : tombs == 3 ? 0x2008 : tombs == 2 ? 0x20 : tombs == 1 ? 4 : 0));
        for (const auto &[bit, key] : dialogues.introductionKeys)
            if (player.npcIntroductions[difficulty].contains(key))
                sections.introductions[28 + difficulty * 8 + bit / 8] |= uint8_t(1u << (bit % 8));
    }
}
void reconcileD2sQuestItem(CharacterRecord &player, int difficulty, std::string_view code, unsigned nativeDifficulty) {
    auto &records = player.quests.at(size_t(difficulty));
    auto &cain = records.at(questIndex(QuestId::SearchForCain));
    auto &tools = records.at(questIndex(QuestId::ToolsOfTheTrade));
    auto &staff = records.at(questIndex(QuestId::HoradricStaff));
    if (cain.stage < uint32_t(CainStage::Rescued) && code == "bks") cain.stage = uint32_t(CainStage::BarkAcquired);
    if (cain.stage < uint32_t(CainStage::Rescued) && code == "bkd") cain.stage = uint32_t(CainStage::ScrollTranslated);
    if (tools.stage < uint32_t(ToolsStage::RewardReady) && code == "hdm") tools.stage = uint32_t(ToolsStage::MalusAcquired);
    if (staff.stage < uint32_t(StaffStage::Submitted) && code == "hst" && nativeDifficulty >= unsigned(difficulty))
        staff.stage = uint32_t(StaffStage::Assembled);
}
int validateD2sQuestRecords(const QuestBook &book) {
    int rewards = 0;
    for (const auto &difficulty : book) {
        const auto &den = difficulty[0];
        require(den.stage <= uint32_t(DenStage::Rewarded) && !(den.flags & ~denRespecUsed) &&
                (!(den.flags & denRespecUsed) || den.stage == uint32_t(DenStage::Rewarded)),
            "Den of Evil quest progress");
        require(difficulty[1].stage <= uint32_t(BurialStage::Rewarded) && !difficulty[1].flags,
            "Burial Grounds quest progress");
        const auto &cain = difficulty[2];
        require(cain.stage <= uint32_t(CainStage::Rewarded) &&
                !(cain.flags & ~(cainStoneCountMask | cainRescuedByRogues)) &&
                (cain.flags & cainStoneCountMask) <= 5 &&
                (!(cain.flags & cainRescuedByRogues) || cain.stage == uint32_t(CainStage::Rewarded)),
            "Search for Cain quest progress");
        require(difficulty[3].stage <= uint32_t(TowerStage::CountessSlain) && !difficulty[3].flags,
            "Forgotten Tower quest progress");
        require(difficulty[4].stage <= uint32_t(ToolsStage::Imbued) && !difficulty[4].flags,
            "Tools of the Trade quest progress");
        require(difficulty[5].stage <= uint32_t(SlaughterStage::Completed) && !difficulty[5].flags,
            "Sisters to the Slaughter quest progress");
        rewards += den.stage == uint32_t(DenStage::Rewarded);
        const auto &radament = difficulty[questIndex(QuestId::RadamentsLair)];
        require(radament.stage <= uint32_t(RadamentStage::Rewarded) &&
            !(radament.flags & ~(radamentBookPending | radamentBookUsed)) &&
            radament.flags != (radamentBookPending | radamentBookUsed) &&
            (!radament.flags || radament.stage >= uint32_t(RadamentStage::Slain)), "Radament quest progress");
        rewards += bool(radament.flags & radamentBookUsed);
        require(difficulty[questIndex(QuestId::HoradricStaff)].stage <= 6 &&
            !(difficulty[questIndex(QuestId::HoradricStaff)].flags & ~staffExplanationMask), "Horadric Staff quest progress");
        for (const auto &[id, maximum] : {std::pair{QuestId::TaintedSun, 4u},
             std::pair{QuestId::ArcaneSanctuary, 4u}, std::pair{QuestId::Summoner, 3u},
             std::pair{QuestId::SevenTombs, 5u}})
            require(difficulty[questIndex(id)].stage <= maximum && !difficulty[questIndex(id)].flags,
                "Act II quest progress");
    }
    return rewards;
}
} // namespace d2x
