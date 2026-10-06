#include "quest_projection.hpp"
#include "content/classic_data.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>

namespace d2x {
QuestView projectQuestDisplay(const ClassicData &data, const QuestProjectionInput &input) {
    QuestView questView; questView.revision=input.revision; questView.actor=input.actor;
    questView.currentAct=std::clamp(input.currentAct,0,int(questActCount)-1);
    questView.tabCount=int(questActCount);
    for(const auto &def:questDefinitions) {
        questView.acts[size_t(def.act)].push_back(def.id);
        auto &entry=questView.entries[questIndex(def.id)]; entry.act=def.act; entry.displaySlot=def.displaySlot; entry.icon=def.icon;
        entry.title=data.questContent[questIndex(def.id)].title;
        const auto &facts=input.entries[questIndex(def.id)];
        const auto status=facts.status;
        const auto flags=facts.playerFlags;
        entry.known=flags.has_value() || status.has_value();
        entry.completed=(flags && (*flags & 1)) || status==13; // Native completed log status; never shared flags.
        entry.active=!entry.completed && ((status && *status) || (flags && (*flags & 0x001E)));
        std::string key;
        if (entry.completed) key="qstsComplete";
        else if (status && *status==12) key="qstsThankYouComeAgain";
        else if (status && *status>0 && *status<12) {
            key=(def.id==QuestId::SiegeOnHarrogath ? "qsta" : "qstsa") +
                std::to_string(def.act+1)+"q"+std::to_string(def.nativeQuest)+std::to_string(*status);
            if (def.id==QuestId::DenOfEvil && *status==4 && input.denRemaining==1) key+="0";
        }
        if (const auto text=data.questStrings.find(key); text!=data.questStrings.end()) {
            entry.description=text->second;
            if (def.id==QuestId::DenOfEvil)
                if (const auto at=entry.description->find("%d"); at!=std::string::npos)
                    entry.description->replace(at,2,input.denRemaining ? std::to_string(*input.denRemaining) : "?");
        } else if (!entry.known || entry.active)
            entry.description="Native quest description is not available yet.";
    }
    const auto den=input.entries[questIndex(QuestId::DenOfEvil)].status;
    questView.showDenRemaining=den==4 && !questView.entry(QuestId::DenOfEvil).completed;
    if (questView.showDenRemaining) questView.denRemaining=input.denRemaining;
    return questView;
}
} // namespace d2x
