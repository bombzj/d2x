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
uint32_t baalNpcAcknowledgement(std::string_view npc);
uint32_t ancientNpcAcknowledgement(std::string_view npc);
inline constexpr uint32_t iceScrollGranted = 1, iceScrollUsed = 2, iceRareGranted = 4;
uint16_t iceNativeRecord(const QuestRecord &q);
void synchronizeIceRecords(const QuestBook &before,QuestBook &after);
int questResistance(const QuestBook &book);
} // namespace d2x
