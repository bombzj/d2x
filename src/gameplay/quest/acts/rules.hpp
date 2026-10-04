#pragma once
#include "gameplay/quest/module.hpp"

namespace d2x {
QuestNpcQuery actOneNpc(const DifficultyQuests &, const QuestNpcFacts &);
QuestNpcQuery actTwoNpc(const DifficultyQuests &, const QuestNpcFacts &);
std::optional<NpcQuestPlan> actOneConversation(QuestId, const QuestRecord &, const NpcQuestFacts &);
std::optional<NpcQuestPlan> actTwoConversation(QuestId, const QuestRecord &, const NpcQuestFacts &);
void actOneEntry(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
void actTwoEntry(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
void actOneDeath(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
void actTwoDeath(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
QuestLogSelection actOneLog(QuestId, const QuestRecord &, const QuestLogFacts &);
QuestLogSelection actTwoLog(QuestId, const QuestRecord &, const QuestLogFacts &);
} // namespace d2x
