#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class RadamentStage : uint32_t { Unstarted, Assigned, LeftTown, Slain, Rewarded };
enum class StaffStage : uint32_t { Unstarted, ArtifactsFound = 1, Assembled = 5, Submitted = 6 };
enum class SunStage : uint32_t { Unstarted, Darkness, Explained, AltarDestroyed, Confirmed };
enum class ArcaneStage : uint32_t { Unstarted, PalaceOpened, JerhynBriefed, Entered, JournalRead };
enum class SummonerStage : uint32_t { Unstarted, Encountered, Slain, Confirmed };
enum class TombsStage : uint32_t { Unstarted, Assigned, DurielSlain, TyraelRescued, JerhynConfirmed, PassageGranted };
inline constexpr uint32_t staffScrollExplained = 1;
inline constexpr uint32_t staffCubeExplained = 2;
inline constexpr uint32_t staffHeadExplained = 4;
inline constexpr uint32_t staffShaftExplained = 8;
inline constexpr uint32_t staffAssemblyExplained = 16;
inline constexpr uint32_t staffExplanationMask = staffScrollExplained | staffCubeExplained |
    staffHeadExplained | staffShaftExplained | staffAssemblyExplained;
inline constexpr uint32_t radamentBookPending = 1;
inline constexpr uint32_t radamentBookUsed = 2;
inline constexpr uint32_t arcaneCommentPending = 1;
inline constexpr std::array actTwoQuests{QuestId::RadamentsLair,QuestId::HoradricStaff,QuestId::TaintedSun,
    QuestId::ArcaneSanctuary,QuestId::Summoner,QuestId::SevenTombs};
struct ActTwoQuestRuntime {uint8_t state{},last{},init{};bool active=true;};
// D2Common QUESTRECORD flags; retained personal history is separate from the
// per-game quest's fState/fLastState and from its status notifications.
inline uint16_t actTwoLegacyRecord(QuestId id,const QuestRecord &q) {
    switch(id) {
    case QuestId::RadamentsLair:return uint16_t(q.stage>=4?0x2001:q.stage==3?0x2002:q.stage==2?0xC:q.stage==1?4:0);
    case QuestId::HoradricStaff:return uint16_t((q.stage>=6?0x2001:0)|(q.stage>=5?0x800:0)|
        (q.flags&staffScrollExplained?8:0)|(q.flags&staffCubeExplained?0x40:0)|(q.flags&staffHeadExplained?0x10:0)|
        (q.flags&staffShaftExplained?0x20:0)|(q.flags&staffAssemblyExplained?0x400:0));
    case QuestId::TaintedSun:return uint16_t(q.stage>=4?0x2001:q.stage==3?0x2002:q.stage==2?0xC:q.stage==1?4:0);
    case QuestId::ArcaneSanctuary:return uint16_t(q.stage>=4?0x2001|(q.flags&arcaneCommentPending?2:0):q.stage==3?0x1C:q.stage==2?0xC:q.stage==1?4:0);
    case QuestId::Summoner:return uint16_t(q.stage>=3?0x2001:q.stage==2?0x2002:q.stage==1?4:0);
    case QuestId::SevenTombs:return uint16_t(q.stage>=5?0x2001:q.stage==4?0x2010:q.stage==3?0x2008:q.stage==2?0x20:q.stage==1?4:0);
    default:return 0;
    }
}
inline void synchronizeActTwoRecords(const QuestBook &before,QuestBook &after) {
    for(size_t difficulty=0;difficulty<after.size();++difficulty) for(const auto id:actTwoQuests) {
        const auto &old=before[difficulty][questIndex(id)];auto &q=after[difficulty][questIndex(id)];
        const bool explicitRecord=q.nativeRecord && q.nativeRecord!=old.nativeRecord;
        uint16_t bits=q.nativeRecord.value_or(old.nativeRecord.value_or(actTwoLegacyRecord(id,old)));
        if(!explicitRecord && q.stage!=old.stage) switch(id) {
        case QuestId::RadamentsLair:
            if(q.stage==1) bits|=4;else if(q.stage==2) bits|=8;else if(q.stage==3) bits|=0x2002;
            else if(q.stage==4) bits=uint16_t((bits|1)&~2u);
            break;
        case QuestId::TaintedSun:
            if(q.stage==1) bits|=4;else if(q.stage==2) bits|=0xC;
            else if(q.stage==3) bits=uint16_t((bits|0x2002)&~0x4000u);
            else if(q.stage==4) bits=uint16_t((bits|1)&~2u);
            break;
        case QuestId::ArcaneSanctuary:
            if(q.stage==1) bits|=4;else if(q.stage==2) bits|=8;else if(q.stage==3) bits|=0x10;
            else if(q.stage==4) bits=uint16_t((bits&~0x0FFCu)|0x2183);
            break;
        case QuestId::Summoner:
            if(q.stage==1) bits|=4;else if(q.stage==2) bits|=0x2002;
            else if(q.stage==3) bits=uint16_t((bits|1)&~2u);
            break;
        case QuestId::HoradricStaff:
            if(q.stage>=5) bits|=0x800;
            if(q.stage>=6) bits|=0x2001;
            break;
        case QuestId::SevenTombs:
            if(q.stage==1) bits|=4;else if(q.stage==2) bits|=0x20;else if(q.stage==3) bits|=0x2008;
            else if(q.stage==4) bits=uint16_t((bits|0x10)&~8u);
            else if(q.stage==5) bits=uint16_t((bits|1)&~0x10u);
            break;
        default:break;
        }
        if(id==QuestId::RadamentsLair && q.flags!=old.flags)
            bits=uint16_t((bits&~0x20u)|(q.flags&radamentBookPending?0x20:0));
        if(id==QuestId::ArcaneSanctuary && q.stage==uint32_t(ArcaneStage::JournalRead) && q.flags!=old.flags)
            bits=uint16_t((bits&~2u)|(q.flags&arcaneCommentPending?2:0));
        if(id==QuestId::HoradricStaff && q.flags!=old.flags)
            bits=uint16_t((bits&~0x478u)|(q.flags&staffScrollExplained?8:0)|(q.flags&staffCubeExplained?0x40:0)|
                (q.flags&staffHeadExplained?0x10:0)|(q.flags&staffShaftExplained?0x20:0)|(q.flags&staffAssemblyExplained?0x400:0));
        q.nativeRecord=bits;
    }
}
} // namespace d2x
