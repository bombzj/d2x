#include "gameplay/quest/acts/act_two_state.hpp"
#include "client/local_quest_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/log.hpp"
#include "gameplay/quest/catalog.hpp"
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "world/region.hpp"
#include "world/maze.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
const QuestView &LocalQuestClient::read() const {
    if (cached_.revision == session_.viewRevision()) return cached_;
    QuestView view;
    view.revision = session_.viewRevision();
    view.actor = session_.state().player.id;
    int supportedActs = 1;
    for (const auto &definition : questDefinitions) {
        view.acts.at(size_t(definition.act)).push_back(definition.id);
        supportedActs = std::max(supportedActs, definition.act + 1);
    }
    view.currentAct = std::clamp(session_.worldContent().levels().at(int(session_.region().definition.id)).act, 0, supportedActs - 1);
    // Reveal later supported tabs when entered or any of their quests started.
    view.tabCount = view.currentAct + 1;
    for (const auto &definition : questDefinitions)
        if (session_.quest(definition.id).stage) view.tabCount = std::max(view.tabCount, definition.act + 1);
    if (session_.quest(QuestId::SistersToTheSlaughter).stage >= questCompletionStage(QuestId::SistersToTheSlaughter))
        view.tabCount = std::max(view.tabCount, std::min(2, supportedActs));
    view.denRemaining = session_.denMonstersRemaining();
    view.showDenRemaining = session_.quest(QuestId::DenOfEvil).stage == uint32_t(DenStage::Entered);
    const auto &strings = session_.content().questStrings;
    QuestLogFacts facts;
    facts.items.cube = session_.carriesQuestItem(session_.content().cubeCode);
    facts.items.shaft = session_.carriesQuestItem(session_.content().staffRecipe.inputs[0]);
    facts.items.head = session_.carriesQuestItem(session_.content().staffRecipe.inputs[1]);
    facts.items.staff = session_.carriesQuestItem(session_.content().staffRecipe.output);
    facts.denRemaining = view.denRemaining;
    facts.journalRead = session_.quest(QuestId::ArcaneSanctuary).stage >= uint32_t(ArcaneStage::JournalRead);
    for (const auto &definition : questDefinitions) {
        auto &entry = view.entries.at(questIndex(definition.id));
        const auto selection = selectQuestLog(definition.id, session_.quest(definition.id), facts);
        entry.act = definition.act;
        entry.displaySlot = definition.displaySlot;
        entry.icon = definition.icon;
        entry.active = selection.active;
        entry.completed = selection.completed;
        entry.title = session_.content().questContent.at(questIndex(definition.id)).title;
        if (const auto description = strings.find(selection.descriptionKey); description != strings.end()) {
            entry.description = description->second;
            if (selection.appendDenRemaining) *entry.description += std::to_string(view.denRemaining.value_or(0));
            if (selection.tombSymbol) entry.tombSymbol = unsigned(actTwoTombs(session_.state().mapSeed)[0] - 66);
        }
    }
    cached_ = std::move(view);
    return cached_;
}
} // namespace d2x
