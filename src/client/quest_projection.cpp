#include "quest_projection.hpp"
#include "content/classic_data.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace d2x {
namespace {
// Retail 1.13c D2Client: quest descriptors RVA F8198..F8408 point to
// status/text pairs at RVA F79FC..F80BC. Numeric states do not imply
// numbered TBL key suffixes; all actual words still come from the MPQ.
std::string_view nativeQuestDescription(unsigned slot,unsigned status) {
    if(!status || status>11) return {};
    struct Texts {unsigned slot;std::array<std::string_view,11> keys;};
    static constexpr Texts rows[]{
        {1,{"qstsa1q11","qstsa1q12","qstsa1q13","qstsa1q14","qstsa1q15","qstsnull","qstsnull","qstsnull","qstsnull","qstsa1q15","qstsprevious"}},
        {2,{"qstsa1q21","qstsa1q22","qstsa1q23","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa1q23","qstsprevious"}},
        {3,{"qstsa1q31","qstsa1q32","qstsa1q3x","Qstsyouarenot8","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa1q32b","qstsprevious"}},
        {4,{"qstsa1q41","qstsa1q42","qstsa1q43","qstsa1q44","qstsa1q45","qstsa1q46","qstsa1q4x","qstsnull","qstsnull","qstsa1q46b","qstsprevious"}},
        {5,{"qstsa1q51","qstsa1q52","qstsa1q51a","qstsa1q51b","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsprevious"}},
        {6,{"qstsa1q61","qstsa1q62","qstsa1q63","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa1q62b","qstsprevious"}},
        {9,{"qstsa2q11","qstsa2q12","qstsa2q13","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa2q13","qstsprevious"}},
        {10,{"qstsa2q21","qstsa2q22","qstsa2q23","qstsa2q25","qstsa2q24","qstsa2q24","qstsnull","qstsnull","qstsThankYouComeAgainMulti","qstsa2q24","qstsprevious"}},
        {11,{"qstsa2q31a","qstsa2q32","qstsa2q33","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa2q33","qstsprevious"}},
        {12,{"qstsa2q41a","qstsa2q41","qstsa2q41","qstsa2q42","qstsa2q43","qstsnull","qstsnull","qstsnull","qstsnull","qstsa2q43","qstsprevious"}},
        {13,{"qstsa2q51","qstsa2q52","qstsComplete","qstsa2q53","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa2q53","qstsprevious"}},
        {14,{"qstsa2q61","qstsa2q62","qstsa2q63f","qstsa2q63a","qstsa2q64","qstsa2q65","qstsa2q61a","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsa2q64","qstsprevious"}},
        {17,{"qstsa3q11","qstsa3q12","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsThankYouComeAgainMulti","qstsThankYouComeAgain","qstsnull","qstsprevious"}},
        {18,{"qstsa3q21","qstsa3q22","qstsa3q23","qstsa3q24","qstsa3q25","qstsa3q26","qstsa3q21a","qstsnull","qstsnull","qstsnull","qstsprevious"}},
        {19,{"qstsa3q31a","qstsa3q31","qstsa3q32","qstsa3q33","qstsa3q34","qstsa3q35","qstsThankYouComeAgainMulti","qstsThankYouComeAgain","qstsnull","qstsnull","qstsnull"}},
        {20,{"qstsa3q41","qstsa3q42","qstsa3q43","qstsa3q44","qstsa3q45","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsa3q45","qstsprevious"}},
        {21,{"qstsa3q51a","qstsa3q51","qstsa3q52","qstsa3q53","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa3q53","qstsprevious"}},
        {22,{"qstsa3q61a","qstsa3q61","qstsa3q62","qstsa3q63","qstsa3q6","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsprevious","qstsprevious"}},
        {25,{"qstsa4q11","qstsa4q12","qstsa4q13a","qstsa4q13","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsa4q13","qstsprevious"}},
        {26,{"qstsa4q21","qstsa4q22","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsnull","qstsprevious"}},
        {27,{"qstsa4q31","qstsa4q32","qstsa4q33","qstsa4q31","qstsnull","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsprevious","qstsprevious"}},
        {35,{"qsta5q11","qsta5q12","qsta5q13","qsta5q14","qstsnull","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qsta5q14","qstsprevious"}},
        {36,{"qstsa5q21","qstsa5q22","qstsa5q23","qstsa5q24","qstsa5q21a","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsa5q23","qstsprevious"}},
        {37,{"qstsa5q31","qstsa5q31a","qstsa5q32","qstsa5q33","qstsa5q34","qstsa5q35","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsa5q34","qstsprevious"}},
        {38,{"qstsa5q41","qstsa5q42a","qstsa5q42","qstsa5q43","qstsa5q43a","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsa5q43a","qstsprevious"}},
        {39,{"qstsa5q51","qstsa5q52","qstsa5q53","qstsa5q52","qstsnull","qstsThankYouComeAgain","qstsThankYouComeAgainMulti","qstsnull","qstsnull","qstsa5q52","qstsprevious"}},
        {40,{"qstsa5q61a","qstsa5q62b","qstsa5q62","qstsa5q63","qstsa5q64","qstsnull","qstsnull","qstsnull","qstsnull","qstsprevious","qstsprevious"}},
    };
    for(const auto &row:rows) if(row.slot==slot) return row.keys[status-1];
    return {};
}
} // namespace
QuestView projectQuestDisplay(const ClassicData &data, const QuestProjectionInput &input) {
    QuestView questView; questView.revision=input.revision; questView.actor=input.actor;
    questView.currentAct=std::clamp(input.currentAct,0,int(questActCount)-1);
    questView.tabCount=int(questActCount);
    const bool native=data.profile=="lod-named-txt-v1";
    const auto arcane=input.entries[questIndex(QuestId::ArcaneSanctuary)].playerFlags;
    for(const auto &def:questDefinitions) {
        questView.acts[size_t(def.act)].push_back(def.id);
        auto &entry=questView.entries[questIndex(def.id)]; entry.act=def.act; entry.displaySlot=def.displaySlot; entry.icon=def.icon;
        entry.title=data.questContent[questIndex(def.id)].title;
        const auto &facts=input.entries[questIndex(def.id)];
        const auto status=facts.status;
        const auto flags=facts.playerFlags;
        entry.known=flags.has_value() || status.has_value() || facts.available.has_value();
        const bool missed=flags && (*flags&0x4000) && !(*flags&1);
        entry.completed=(flags && (*flags & 1)) || (!missed && status==13); // Owner facts only.
        entry.completionPending=entry.completed && flags && !(*flags&0x1000);
        const bool closed=facts.available==false && (!status || !*status) && !(flags && (*flags&2));
        entry.unavailable=!entry.completed && (missed || status==12 || closed);
        entry.active=!entry.completed && !entry.unavailable && ((status && *status) || (flags && (*flags & 0x001E)));
        unsigned displayStatus=status.value_or(0);bool symbolText=false;
        if(native && def.id==QuestId::SevenTombs && !entry.completed && !entry.unavailable && flags && !(*flags&3)) {
            // RVA 1CF1D..1D046: the symbol is a separate display state;
            // saved Tyrael/Jerhyn flags override stale generic status text.
            if(displayStatus==1 && arcane && (*arcane&0x2001)) {displayStatus=7;symbolText=true;}
            else if(*flags&8) displayStatus=5;
            else if(*flags&0x10) displayStatus=6;
        }
        std::string key;
        if (entry.completed) key=flags && !(*flags&0x2000) ? "qstsprevious" : "qstsComplete";
        else if (entry.unavailable) key="qstsThankYouComeAgain";
        else if (displayStatus>0 && displayStatus<12) {
            if(native) key=nativeQuestDescription(def.nativeSlot,displayStatus);
            else key=(def.id==QuestId::SiegeOnHarrogath ? "qsta" : "qstsa") +
                std::to_string(def.act+1)+"q"+std::to_string(def.nativeQuest)+std::to_string(displayStatus);
            if (def.id==QuestId::DenOfEvil && displayStatus==4 && input.denRemaining==1) key+="0";
        }
        if (const auto text=data.questStrings.find(key); text!=data.questStrings.end()) {
            entry.description=text->second;
            if (def.id==QuestId::DenOfEvil)
                if (const auto at=entry.description->find("%d"); at!=std::string::npos)
                    entry.description->replace(at,2,input.denRemaining ? std::to_string(*input.denRemaining) : "?");
        }
        if(native && symbolText && input.staffTombOffset && *input.staffTombOffset<7)
            entry.tombSymbol=*input.staffTombOffset;
    }
    const auto den=input.entries[questIndex(QuestId::DenOfEvil)].status;
    questView.showDenRemaining=den==4 && !questView.entry(QuestId::DenOfEvil).completed;
    if (questView.showDenRemaining) questView.denRemaining=input.denRemaining;
    // QUESTS_UpdatePlayerFlags shares packet50/id1 with Den/Barb counters.
    // Its tomb field is meaningful only after A2Q4 reward/primary-goal flags.
    if(!native && input.staffTombOffset && *input.staffTombOffset<7 && arcane && (*arcane&0x2001))
        questView.entries[questIndex(QuestId::SevenTombs)].tombSymbol=*input.staffTombOffset;
    return questView;
}
} // namespace d2x
