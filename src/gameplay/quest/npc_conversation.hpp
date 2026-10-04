#pragma once
#include "gameplay/quest/state.hpp"
#include <optional>
#include <string_view>

namespace d2x {
enum class QuestReward { None, SkillPoint, Rogue, TranslateScroll, CainRing, ReturnMalus, TyraelPortal,
    ExchangeGoldenBird, DeliverGoldenBird, LifePotion, ReturnGidbinn, GidbinnRing, IronWolf, LamTome, IzualSkills, Soulstone, ActFivePortal, RescueRunes,
    DefrostPotion, ResistanceScroll, AnyaRare, AnyaTemplePortal, FinalPortal };
struct NpcQuestFacts {
    std::string_view npcClass;
    bool denRewarded = false;
    uint32_t questExplanation = 0;
};
// A decision is private to the authority, never a client permission or command.
// The host performs the reward first, then commits this record and publishes it.
struct NpcQuestPlan {
    QuestRecord next;
    QuestReward reward = QuestReward::None;
};
std::optional<NpcQuestPlan> planNpcQuest(QuestId quest, const QuestRecord &record,
                                       const NpcQuestFacts &facts);
} // namespace d2x
