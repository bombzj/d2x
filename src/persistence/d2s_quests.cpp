#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
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
#include <algorithm>
#include <utility>

namespace d2x {
namespace {
constexpr std::array<size_t, 4> actCompletedSlots{7, 15, 23, 28};
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
        for (size_t act = 0; act < actCompletedSlots.size(); ++act)
            player.completedActs[difficulty][act] = (word(sections.quests,
                10 + difficulty * 96 + actCompletedSlots[act] * 2) & 1u) != 0;
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
        const auto birdFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::GoldenBird).nativeSlot * 2);
        const auto izualFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::FallenAngel).nativeSlot * 2);
        const auto siegeFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::SiegeOnHarrogath).nativeSlot * 2);
        const auto rescueFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::RescueOnMountArreat).nativeSlot * 2);
        const auto iceFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::PrisonOfIce).nativeSlot * 2);
        const auto betrayalFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::BetrayalOfHarrogath).nativeSlot * 2);
        const auto ancientFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::RiteOfPassage).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::RiteOfPassage)] = {ancientFlags & 1 ? 4u : ancientFlags & 0x10 ? 3u : ancientFlags & 8 ? 2u : ancientFlags & 4 ? 1u : 0u,
            ancientFlags & 1 ? uint32_t(ancientFlags >> 5 & 31u) | (ancientFlags & 0x10 ? 32u : 0u) : 0u};
        const auto baalFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::EveOfDestruction).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::EveOfDestruction)] = {baalFlags & 1 ? 5u : baalFlags & 8 ? 2u : baalFlags & 4 ? 1u : 0u,
            baalFlags & 1 ? uint32_t(baalFlags >> 4 & 127u) : 0u};
        player.quests[difficulty][questIndex(QuestId::BetrayalOfHarrogath)] = {betrayalFlags & 1 ? 5u : betrayalFlags & 2 ? betrayalFlags & 0x10 ? 4u : 3u : betrayalFlags & 8 ? 2u : betrayalFlags & 4 ? 1u : 0u, 0};
        player.quests[difficulty][questIndex(QuestId::PrisonOfIce)] = {iceFlags & 1 ? 6u : iceFlags & 2 ? 5u : iceFlags & 0x10 ? 3u : iceFlags & 8 ? 2u : iceFlags & 4 ? 1u : 0u,
            (iceFlags & 0x181 ? iceScrollGranted : 0u) | (iceFlags & 0x80 ? iceScrollUsed : 0u) | (iceFlags & 0x601 ? iceRareGranted : 0u)};
        player.quests[difficulty][questIndex(QuestId::RescueOnMountArreat)] = {rescueFlags & 1 ? 4u : rescueFlags & 2 ? 3u : rescueFlags & 8 ? 2u : rescueFlags & 4 ? 1u : 0u,
            rescueFlags & 0x20 ? 15u : rescueFlags & 0x40 ? 14u : rescueFlags & 0x80 ? 13u : rescueFlags & 3 ? 15u : 0u};
        player.quests[difficulty][questIndex(QuestId::SiegeOnHarrogath)] = {siegeFlags & 1 ? 5u : siegeFlags & 2 ? siegeFlags & 0x20 ? 4u : 3u : siegeFlags & 8 ? 2u : siegeFlags & 4 ? 1u : 0u, 0};
        const auto forgeFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::HellsForge).nativeSlot * 2);
        const auto terrorFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::TerrorsEnd).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::TerrorsEnd)] = {terrorFlags & 1 ? 4u : terrorFlags & 0x10 ? 3u : terrorFlags & 8 ? 2u : terrorFlags & 4 ? 1u : 0u,
            (terrorFlags & 0x80 ? terrorTyraelPending : 0u) | (terrorFlags & 0x40 ? terrorCainPending : 0u) | (terrorFlags & 0x200 ? terrorPortalOpened : 0u)};
        player.quests[difficulty][questIndex(QuestId::HellsForge)] = {forgeFlags & 1 ? 5u : forgeFlags & 2 ? 4u : forgeFlags & 8 ? 2u : forgeFlags & 0x24 ? 1u : 0u, 0};
        player.quests[difficulty][questIndex(QuestId::FallenAngel)] = {izualFlags & 1 ? 4u : izualFlags & 2 ? 3u : izualFlags & 8 ? 2u : izualFlags & 4 ? 1u : 0u,
            izualFlags & 0x20 ? izualGhostSpoken : 0u};
        auto &bird = player.quests[difficulty][questIndex(QuestId::GoldenBird)];
        bird.stage = birdFlags & 1 ? 6 : birdFlags & 2 ? 5 : birdFlags & 0x10 ? 4 : birdFlags & 4 ? 2 : birdFlags & 0x40 ? 1 : 0;
        bird.flags = (birdFlags & 1) && (birdFlags & 0x20) ? goldenBirdPotionPending : 0;
        const auto bladeFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::BladeOfTheOldReligion).nativeSlot * 2);
        auto &blade = player.quests[difficulty][questIndex(QuestId::BladeOfTheOldReligion)];
        blade.flags = (bladeFlags & 0x100 ? gidbinnRingGranted : 0) | (bladeFlags & 0x80 ? gidbinnHirelingGranted : 0);
        blade.stage = bladeFlags & 1 ? 5 : bladeFlags & 0x40 ? 4 : bladeFlags & 0x20 ? 3 : bladeFlags & 8 ? 2 : bladeFlags & 4 ? 1 : 0;
        const auto khalimFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::KhalimsWill).nativeSlot * 2);
        auto &khalim = player.quests[difficulty][questIndex(QuestId::KhalimsWill)];
        khalim.stage = khalimFlags & 1 ? 4 : khalimFlags & 0x80 ? 3 : khalimFlags & 4 ? 1 : 0;
        khalim.flags = (khalimFlags & 8 ? khalimEyeExplained : 0) | (khalimFlags & 0x10 ? khalimBrainExplained : 0) |
            (khalimFlags & 0x40 ? khalimHeartExplained : 0) | (khalimFlags & 0x20 ? khalimFlailExplained : 0) |
            (khalimFlags & 0x80 ? khalimWillExplained : 0) | (khalimFlags & 4 ? khalimAssigned : 0);
        const auto tomeFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::LamEsensTome).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::LamEsensTome)].stage = tomeFlags & 1 ? 4 : tomeFlags & 8 ? 2 : tomeFlags & 4 ? 1 : 0;
        const auto templeFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::BlackenedTemple).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::BlackenedTemple)].stage = templeFlags & 1 ? 4 : templeFlags & 0x10 ? 3 : templeFlags & 8 ? 2 : templeFlags & 4 ? 1 : 0;
        const auto guardianFlags = word(sections.quests, 10 + difficulty * 96 + questDefinition(QuestId::Guardian).nativeSlot * 2);
        player.quests[difficulty][questIndex(QuestId::Guardian)] = {guardianFlags & 1 ? 4u : guardianFlags & 0x10 ? 3u : guardianFlags & 8 ? 2u : guardianFlags & 4 ? 1u : 0u,
            guardianFlags & 0x800 ? guardianSpeechPending : 0u};
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
        for (size_t act = 0; act < actCompletedSlots.size(); ++act) {
            const auto at = 10 + difficulty * 96 + actCompletedSlots[act] * 2;
            putWord(sections.quests, at, (word(sections.quests, at) & ~0x2001u) |
                (player.completedActs[difficulty][act] ? 0x2001u : 0u));
        }
        for (const auto &prelude : questPreludes) {
            const auto at = 10 + difficulty * 96 + prelude.nativeSlot * 2;
            putWord(sections.quests, at, (word(sections.quests, at) & ~1u) |
                (player.questPreludes[difficulty][size_t(prelude.id)] ? 1u : 0u));
        }
        sections.quests[10 + difficulty * 96 + 0x52] = (player.quests[difficulty][0].flags & denRespecUsed) ? 1 : 0;
        const auto &bird = player.quests[difficulty][questIndex(QuestId::GoldenBird)];
        const auto &izual = player.quests[difficulty][questIndex(QuestId::FallenAngel)];
        const auto &siege = player.quests[difficulty][questIndex(QuestId::SiegeOnHarrogath)];
        const auto &rescue = player.quests[difficulty][questIndex(QuestId::RescueOnMountArreat)];
        const auto &ice = player.quests[difficulty][questIndex(QuestId::PrisonOfIce)];
        const auto &betrayal = player.quests[difficulty][questIndex(QuestId::BetrayalOfHarrogath)];
        const auto &ancients = player.quests[difficulty][questIndex(QuestId::RiteOfPassage)];
        const auto &baal = player.quests[difficulty][questIndex(QuestId::EveOfDestruction)];
        const auto baalOffset = 10 + difficulty * 96 + questDefinition(QuestId::EveOfDestruction).nativeSlot * 2;
        putWord(sections.quests, baalOffset, (word(sections.quests, baalOffset) & ~0x27FDu) |
            (baal.stage == 5 ? 0x2001 | (baal.flags << 4) : baal.stage >= 2 ? 0xC : baal.stage == 1 ? 4 : 0));
        const auto ancientOffset = 10 + difficulty * 96 + questDefinition(QuestId::RiteOfPassage).nativeSlot * 2;
        putWord(sections.quests, ancientOffset, (word(sections.quests, ancientOffset) & ~0x23FDu) |
            (ancients.stage == 4 ? 0x2001 | ((ancients.flags & 31) << 5) | (ancients.flags & 32 ? 0x10 : 0) : ancients.stage >= 2 ? 0xC : ancients.stage == 1 ? 4 : 0));
        const auto betrayalOffset = 10 + difficulty * 96 + questDefinition(QuestId::BetrayalOfHarrogath).nativeSlot * 2;
        putWord(sections.quests, betrayalOffset, (word(sections.quests, betrayalOffset) & ~0x201Fu) |
            (betrayal.stage == 5 ? 0x2001 : betrayal.stage >= 3 ? 0x2002 | (betrayal.stage == 4 ? 0x10 : 0) : betrayal.stage == 2 ? 0xC : betrayal.stage == 1 ? 4 : 0));
        const auto iceOffset = 10 + difficulty * 96 + questDefinition(QuestId::PrisonOfIce).nativeSlot * 2;
        putWord(sections.quests, iceOffset, (word(sections.quests, iceOffset) & ~0x279Fu) |
            (ice.stage == 6 ? 0x2001 : ice.stage == 5 ? 0x2002 : ice.stage >= 3 ? 0x1C : ice.stage == 2 ? 0xC : ice.stage == 1 ? 4 : 0) |
            (ice.flags & iceScrollGranted ? 0x100 : 0) | (ice.flags & iceScrollUsed ? 0x80 : 0) | (ice.flags & iceRareGranted ? 0x600 : 0));
        const auto rescueOffset = 10 + difficulty * 96 + questDefinition(QuestId::RescueOnMountArreat).nativeSlot * 2;
        putWord(sections.quests, rescueOffset, (word(sections.quests, rescueOffset) & ~0x20EFu) |
            (rescue.stage == 4 ? 0x2001 : rescue.stage == 3 ? 0x2002 : rescue.stage == 2 ? 0xC : rescue.stage == 1 ? 4 : 0) |
            (rescue.stage >= 3 ? rescue.flags == 15 ? 0x20 : rescue.flags == 14 ? 0x40 : 0x80 : 0));
        const auto siegeOffset = 10 + difficulty * 96 + questDefinition(QuestId::SiegeOnHarrogath).nativeSlot * 2;
        putWord(sections.quests, siegeOffset, (word(sections.quests, siegeOffset) & ~0x202Fu) |
            (siege.stage == 5 ? 0x2001 : siege.stage >= 3 ? 0x2002 | (siege.stage == 4 ? 0x20 : 0) : siege.stage == 2 ? 0xC : siege.stage == 1 ? 4 : 0));
        const auto &forge = player.quests[difficulty][questIndex(QuestId::HellsForge)];
        const auto &terror = player.quests[difficulty][questIndex(QuestId::TerrorsEnd)];
        const auto terrorOffset = 10 + difficulty * 96 + questDefinition(QuestId::TerrorsEnd).nativeSlot * 2;
        putWord(sections.quests, terrorOffset, (word(sections.quests, terrorOffset) & ~0x22DDu) |
            (terror.stage == 4 ? 0x2001 : terror.stage == 3 ? 0x1C : terror.stage == 2 ? 0xC : terror.stage == 1 ? 4 : 0) |
            (terror.flags & terrorTyraelPending ? 0x80 : 0) | (terror.flags & terrorCainPending ? 0x40 : 0) | (terror.flags & terrorPortalOpened ? 0x200 : 0));
        const auto forgeOffset = 10 + difficulty * 96 + questDefinition(QuestId::HellsForge).nativeSlot * 2;
        putWord(sections.quests, forgeOffset, (word(sections.quests, forgeOffset) & ~0x202Fu) |
            (forge.stage == 5 ? 0x2001 : forge.stage == 4 ? 0x2002 : forge.stage >= 2 ? 0xC : forge.stage == 1 ? 4 : 0));
        const auto izualOffset = 10 + difficulty * 96 + questDefinition(QuestId::FallenAngel).nativeSlot * 2;
        putWord(sections.quests, izualOffset, (word(sections.quests, izualOffset) & ~0x202Fu) |
            (izual.stage == 4 ? 0x2001 : izual.stage == 3 ? 0x2002 : izual.stage == 2 ? 0xC : izual.stage == 1 ? 4 : 0) |
            (izual.flags & izualGhostSpoken ? 0x20 : 0));
        const auto birdOffset = 10 + difficulty * 96 + questDefinition(QuestId::GoldenBird).nativeSlot * 2;
        const unsigned birdBits = bird.stage == 6 ? 0x2001 | (bird.flags & goldenBirdPotionPending ? 0x20 : 0) :
            bird.stage == 5 ? 2 : bird.stage >= 4 ? 0x50 : bird.stage == 3 ? 0x40 : bird.stage == 2 ? 0x44 : bird.stage == 1 ? 0x40 : 0;
        putWord(sections.quests, birdOffset, (word(sections.quests, birdOffset) & ~0x2077u) | birdBits);
        const auto &blade = player.quests[difficulty][questIndex(QuestId::BladeOfTheOldReligion)];
        const auto bladeOffset = 10 + difficulty * 96 + questDefinition(QuestId::BladeOfTheOldReligion).nativeSlot * 2;
        const unsigned bladeBits = (blade.stage == 5 ? 0x2001 : blade.stage == 4 ? 0x2040 : blade.stage == 3 ? 0x220 : blade.stage == 2 ? 0xC : blade.stage == 1 ? 4 : 0) |
            (blade.flags & gidbinnRingGranted ? 0x100 : 0) | (blade.flags & gidbinnHirelingGranted ? 0x80 : 0);
        putWord(sections.quests, bladeOffset, (word(sections.quests, bladeOffset) & ~0x23EDu) | bladeBits);
        const auto &khalim = player.quests[difficulty][questIndex(QuestId::KhalimsWill)];
        const auto khalimOffset = 10 + difficulty * 96 + questDefinition(QuestId::KhalimsWill).nativeSlot * 2;
        const unsigned khalimBits = (khalim.stage >= 4 ? 0x2001 : 0) | (khalim.flags & khalimAssigned ? 4 : 0) |
            (khalim.flags & khalimEyeExplained ? 8 : 0) | (khalim.flags & khalimBrainExplained ? 0x10 : 0) |
            (khalim.flags & khalimHeartExplained ? 0x40 : 0) | (khalim.flags & khalimFlailExplained ? 0x20 : 0) |
            (khalim.flags & khalimWillExplained ? 0x80 : 0);
        putWord(sections.quests, khalimOffset, (word(sections.quests, khalimOffset) & ~0x20FDu) | khalimBits);
        const auto &tome = player.quests[difficulty][questIndex(QuestId::LamEsensTome)];
        const auto tomeOffset = 10 + difficulty * 96 + questDefinition(QuestId::LamEsensTome).nativeSlot * 2;
        putWord(sections.quests, tomeOffset, (word(sections.quests, tomeOffset) & ~0x200Du) |
            (tome.stage == 4 ? 0x2001 : tome.stage >= 2 ? 0xC : tome.stage == 1 ? 4 : 0));
        const auto &temple = player.quests[difficulty][questIndex(QuestId::BlackenedTemple)];
        const auto templeOffset = 10 + difficulty * 96 + questDefinition(QuestId::BlackenedTemple).nativeSlot * 2;
        putWord(sections.quests, templeOffset, (word(sections.quests, templeOffset) & ~0x201Du) |
            (temple.stage == 4 ? 0x2001 : temple.stage == 3 ? 0x2010 : temple.stage == 2 ? 0xC : temple.stage == 1 ? 4 : 0));
        const auto &guardian = player.quests[difficulty][questIndex(QuestId::Guardian)];
        const auto guardianOffset = 10 + difficulty * 96 + questDefinition(QuestId::Guardian).nativeSlot * 2;
        putWord(sections.quests, guardianOffset, (word(sections.quests, guardianOffset) & ~0x281Du) |
            (guardian.stage == 4 ? 0x2001 : guardian.stage == 3 ? 0x1C : guardian.stage == 2 ? 0xC : guardian.stage == 1 ? 4 : 0) |
            (guardian.flags & guardianSpeechPending ? 0x800 : 0));
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
    auto &bird = records.at(questIndex(QuestId::GoldenBird));
    auto &ice = records.at(questIndex(QuestId::PrisonOfIce));
    if (code == "ice" && nativeDifficulty >= unsigned(difficulty) && ice.stage < 5) ice.stage = 4;
    auto &blade = records.at(questIndex(QuestId::BladeOfTheOldReligion));
    if (code == "g33" && nativeDifficulty >= unsigned(difficulty) && blade.stage < 4) blade.stage = 3;
    auto &khalim = records.at(questIndex(QuestId::KhalimsWill));
    auto &tome = records.at(questIndex(QuestId::LamEsensTome));
    if (code == "bbb" && nativeDifficulty >= unsigned(difficulty) && tome.stage < 4) tome.stage = 3;
    if (nativeDifficulty >= unsigned(difficulty) && khalim.stage < 4) {
        if (code == "qf2") khalim.stage = 3;
        else if (code == "qf1" || code == "qey" || code == "qbr" || code == "qhr") khalim.stage = std::max(khalim.stage, 2u);
    }
    if (nativeDifficulty >= unsigned(difficulty) && bird.stage < 5) {
        if (code == "j34") bird.stage = std::max(bird.stage, 1u);
        if (code == "g34") bird.stage = std::max(bird.stage, 3u);
    }
}
int validateD2sQuestRecords(const QuestBook &book) {
    int rewards = 0;
    for (const auto &difficulty : book) {
        const auto &izual = difficulty[questIndex(QuestId::FallenAngel)];
        require(izual.stage <= 4 && !(izual.flags & ~izualGhostSpoken) && (!izual.flags || izual.stage >= 3), "Fallen Angel quest progress");
        rewards += izual.stage == 4 ? izualSkillReward : 0;
        const auto &forge = difficulty[questIndex(QuestId::HellsForge)];
        require(forge.stage <= 5 && !forge.flags, "Hellforge quest progress");
        const auto &terror = difficulty[questIndex(QuestId::TerrorsEnd)];
        require(terror.stage <= 4 && !(terror.flags & ~7u) && (!terror.flags || terror.stage == 4), "Terror's End quest progress");
        const auto &siege = difficulty[questIndex(QuestId::SiegeOnHarrogath)];
        require(siege.stage <= 5 && !siege.flags, "Siege quest progress");
        const auto &rescue = difficulty[questIndex(QuestId::RescueOnMountArreat)];
        require(rescue.stage <= 4 && rescue.flags <= 15 && (rescue.stage < 3 || rescue.flags >= 12), "Rescue quest progress");
        const auto &ice = difficulty[questIndex(QuestId::PrisonOfIce)];
        require(ice.stage <= 6 && !(ice.flags & ~7u) && (!ice.flags || ice.stage >= 5) &&
            (!(ice.flags & iceScrollUsed) || ice.flags & iceScrollGranted), "Prison of Ice quest progress");
        const auto &betrayal = difficulty[questIndex(QuestId::BetrayalOfHarrogath)];
        require(betrayal.stage <= 5 && !betrayal.flags, "Betrayal quest progress");
        const auto &ancients = difficulty[questIndex(QuestId::RiteOfPassage)];
        require(ancients.stage <= 4 && !(ancients.flags & ~63u) && (!ancients.flags || ancients.stage == 4), "Ancients quest progress");
        const auto &baal = difficulty[questIndex(QuestId::EveOfDestruction)];
        require(baal.stage <= 5 && !(baal.flags & ~127u) && (!baal.flags || baal.stage == 5), "Baal quest progress");
        const auto &bird = difficulty[questIndex(QuestId::GoldenBird)];
        require(bird.stage <= 6 && !(bird.flags & ~goldenBirdPotionPending) &&
            (!bird.flags || bird.stage == 6), "Golden Bird quest progress");
        const auto &blade = difficulty[questIndex(QuestId::BladeOfTheOldReligion)];
        require(blade.stage <= 5 && !(blade.flags & ~3u) && (!blade.flags || blade.stage >= 4) &&
            (blade.stage != 5 || blade.flags == 3), "Gidbinn quest progress");
        const auto &khalim = difficulty[questIndex(QuestId::KhalimsWill)];
        require(khalim.stage <= 4 && !(khalim.flags & ~63u), "Khalim quest progress");
        const auto &tome = difficulty[questIndex(QuestId::LamEsensTome)];
        require(tome.stage <= 4 && !tome.flags, "Lam Esen quest progress");
        const auto &temple = difficulty[questIndex(QuestId::BlackenedTemple)];
        require(temple.stage <= 4 && !temple.flags, "Blackened Temple quest progress");
        const auto &guardian = difficulty[questIndex(QuestId::Guardian)];
        require(guardian.stage <= 4 && !(guardian.flags & ~guardianSpeechPending) && (!guardian.flags || guardian.stage == 4), "Guardian quest progress");
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
