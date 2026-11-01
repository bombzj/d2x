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
inline uint16_t iceNativeRecord(const QuestRecord &q) {
    auto bits=q.nativeRecord.value_or(uint16_t(q.stage==6?0x2001:q.stage==5?0x2002:q.stage>=2?0xC:q.stage==1?4:0));
    return uint16_t((bits&~0x780u)|(q.flags&iceScrollGranted?0x100:0)|
        (q.flags&iceScrollUsed?0x80:0)|(q.flags&iceRareGranted?0x600:0));
}
inline void synchronizeIceRecords(const QuestBook &before,QuestBook &after) {
    for(size_t difficulty=0;difficulty<after.size();++difficulty) {
        const auto &old=before[difficulty][questIndex(QuestId::PrisonOfIce)];
        auto &q=after[difficulty][questIndex(QuestId::PrisonOfIce)];
        uint16_t bits=q.nativeRecord.value_or(iceNativeRecord(old));
        if(q.stage!=old.stage) {
            if(q.stage==1) bits|=4;
            else if(q.stage==2) bits|=8;
            else if(q.stage==5) bits|=0x2002;
            else if(q.stage==6) bits=uint16_t((bits|1)&~2u);
            // Found/Potion are runtime/item facts, not ENTERAREA history.
        }
        q.nativeRecord=bits;q.nativeRecord=iceNativeRecord(q);
    }
}
inline int questResistance(const QuestBook &book) {
    int result = 0;
    for (const auto &difficulty : book) if (difficulty[questIndex(QuestId::PrisonOfIce)].flags & iceScrollUsed) result += 10;
    return result; // A5Q3_ApplyResistanceReward, each elemental resistance.
}
} // namespace d2x
