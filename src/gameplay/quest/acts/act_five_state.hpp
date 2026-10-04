#pragma once
#include "gameplay/quest/state.hpp"
#include <string_view>
namespace d2x {
enum class SiegeStage : uint32_t { Unstarted, Assigned, LeftTown, Slain, SocketReady, Rewarded };
enum class RescueStage : uint32_t { Unstarted, Assigned, Searching, Rescued, Rewarded };
inline constexpr unsigned rescueCountMask = 15;
enum class IceStage : uint32_t { Unstarted, Assigned, Searching, Found, Potion, Thawed, Rewarded };
enum class BetrayalStage : uint32_t { Unstarted, Assigned, Searching, Slain, PersonalizeReady, Rewarded };
enum class AncientsStage : uint32_t { Unstarted, Assigned, Searching, Summit, Rewarded };
enum class BaalStage : uint32_t { Unstarted, ConsultAncients, Searching, Throne, Chamber, Completed };
inline constexpr uint32_t baalTyraelSpoken = 8, baalFinalPortalUsed = 64;
inline uint32_t baalNpcAcknowledgement(std::string_view npc) {
    if (npc == "larzuk") return 1;
    if (npc.starts_with("cain")) return 2;
    if (npc == "malah") return 4;
    if (npc == "tyrael3") return baalTyraelSpoken;
    if (npc == "qual-kehk") return 16;
    if (npc == "drehya") return 32;
    return 0;
}
inline constexpr std::array<std::string_view, 3> ancientIdentities{"Ancient Barbarian 1", "Ancient Barbarian 2", "Ancient Barbarian 3"};
inline uint32_t ancientNpcAcknowledgement(std::string_view npc) {
    if (npc == "larzuk") return 1;
    if (npc.starts_with("cain")) return 2;
    if (npc == "drehya") return 4;
    if (npc == "malah") return 8;
    if (npc == "qual-kehk") return 16;
    return 0;
}
inline constexpr uint32_t iceScrollGranted = 1, iceScrollUsed = 2, iceRareGranted = 4;
inline int questResistance(const QuestBook &book) {
    int result = 0;
    for (const auto &difficulty : book) if (difficulty[questIndex(QuestId::PrisonOfIce)].flags & iceScrollUsed) result += 10;
    return result; // A5Q3_ApplyResistanceReward, each elemental resistance.
}
} // namespace d2x
