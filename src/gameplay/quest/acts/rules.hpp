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
QuestNpcQuery actThreeNpc(const DifficultyQuests &, const QuestNpcFacts &);
std::optional<NpcQuestPlan> actThreeConversation(QuestId, const QuestRecord &, const NpcQuestFacts &);
void actThreeEntry(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
void actThreeDeath(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
QuestLogSelection actThreeLog(QuestId, const QuestRecord &, const QuestLogFacts &);
QuestNpcQuery actFourNpc(const DifficultyQuests &, const QuestNpcFacts &);
std::optional<NpcQuestPlan> actFourConversation(QuestId, const QuestRecord &, const NpcQuestFacts &);
void actFourEntry(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
void actFourDeath(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
QuestLogSelection actFourLog(QuestId, const QuestRecord &, const QuestLogFacts &);
QuestNpcQuery actFiveNpc(const DifficultyQuests &, const QuestNpcFacts &);
std::optional<NpcQuestPlan> actFiveConversation(QuestId, const QuestRecord &, const NpcQuestFacts &);
void actFiveEntry(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
void actFiveDeath(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
QuestLogSelection actFiveLog(QuestId, const QuestRecord &, const QuestLogFacts &);
} // namespace d2x
