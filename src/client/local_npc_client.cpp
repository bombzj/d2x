#include "client/local_npc_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/npc/store.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "world/region.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
const NpcConversationView &LocalNpcClient::read(EntityId npc) const {
    if (cached_.revision == session_.viewRevision() && cached_.npc == npc) return cached_;
    NpcConversationView view;
    view.revision = session_.viewRevision();
    view.actor = session_.state().player.id;
    view.npc = npc;
    auto service = [&](std::string label, NpcMenuAction action, std::string hint = {}) {
        view.services.push_back({std::move(label), {action, std::nullopt}});
        if (!hint.empty()) view.serviceHints.emplace(action, std::move(hint));
    };
    const auto *object = session_.object(npc);
    if (object && !object->npcClass.empty()) {
        view.valid = true;
        view.position = object->pos;
        view.speaker = object->name;
        const auto &npcClass = object->npcClass;
        const auto &content = session_.content();
        const auto &dialogues = content.npcDialogues;
        if (const auto *intro = introSpeech(dialogues, view.speaker,
            session_.state().player.character.characterClass, object->act)) view.introduction = intro->text;
        for (auto [id, speech] : session_.npcQuestTopics(npc)) {
            const auto &title = content.questContent.at(questIndex(id)).title;
            view.topics.push_back({id, title, speech->text});
            view.talkEntries.push_back({title, {NpcMenuAction::QuestTopic, id}});
        }
        // Preserve gossipSpeech's catalog order and cyclic selection without
        // exposing the full NPC catalog or another player's quest context.
        const auto speaker = dialogues.speakers.find(npcIntroductionKey(view.speaker, object->act));
        if (speaker != dialogues.speakers.end())
            for (const auto &[group, speeches] : dialogues)
                for (const auto &speech : speeches)
                    if (speech.act == object->act && speech.speaker == speaker->second && speech.gossip)
                        view.gossip.push_back(speech.text);
        if (view.introduction)
            view.talkEntries.insert(view.talkEntries.begin(), {"Introduction", {NpcMenuAction::Introduction, std::nullopt}});
        if (!view.gossip.empty()) view.talkEntries.push_back({"Gossip", {NpcMenuAction::Gossip, std::nullopt}});
        if (view.introduction || !view.topics.empty() || !view.gossip.empty()) service("Talk", NpcMenuAction::Talk);
        if (session_.vendorStock(npc)) service(npcCanRepair(npcClass) ? "Trade / Repair" : "Trade", NpcMenuAction::Trade);
        if (npcCanGamble(npcClass)) service("Gamble", NpcMenuAction::Gamble);
        if (session_.canResurrectHireling(npc)) service("Resurrect: " + std::to_string(session_.hirelingResurrectionCost()), NpcMenuAction::Resurrect);
        if (session_.canHireFrom(npc)) service("Hire", NpcMenuAction::Hire);
        if (npcCanIdentify(npcClass)) service("Identify Items", NpcMenuAction::Identify);
        if (npcClass == "akara") {
            const auto &quest = session_.quest(QuestId::DenOfEvil);
            if (quest.stage == uint32_t(DenStage::Rewarded) && !(quest.flags & denRespecUsed)) service("Reset Stat/Skill Points", NpcMenuAction::Respec);
        }
        if (npcClass == "charsi" && session_.quest(QuestId::ToolsOfTheTrade).stage == uint32_t(ToolsStage::RewardReady)) service("Imbue", NpcMenuAction::Imbue);
        if (npcClass == "larzuk" && session_.quest(QuestId::SiegeOnHarrogath).stage == 4)
            service(content.itemStrings.at("Addsocketsui"), NpcMenuAction::Socket, content.itemStrings.at("Addsocketsui2"));
        if (npcClass == "drehya" && session_.quest(QuestId::BetrayalOfHarrogath).stage == 4)
            service(content.itemStrings.at("Personalizeui"), NpcMenuAction::Personalize, content.itemStrings.at("Rename Instruct"));
        if (npcClass == "warriv1" && session_.quest(QuestId::SistersToTheSlaughter).stage >= uint32_t(SlaughterStage::PassageReady)) service("Go East", NpcMenuAction::GoEast);
        if (npcClass == "meshif1" && session_.quest(QuestId::SevenTombs).stage >= 5) service("Sail East", NpcMenuAction::Sail);
        if (npcClass == "meshif2") service("Sail West", NpcMenuAction::Sail);
    }
    service("Cancel", NpcMenuAction::Cancel);
    view.talkEntries.push_back({"Cancel", {NpcMenuAction::Cancel, std::nullopt}});
    cached_ = std::move(view);
    return cached_;
}
const NpcSceneView &LocalNpcClient::scene() const {
    if (scene_.revision == session_.viewRevision()) return scene_;
    NpcSceneView view;
    view.revision = session_.viewRevision();
    for (const auto &[index, offset] : session_.sceneRegions())
        for (const auto &npc : session_.regions().at(size_t(index)).objects) {
            if (npc.npcClass.empty() || npc.questHidden || !session_.roomVisible(index, npc.pos)) continue;
            NpcPublicView entry;
            entry.questAlert = session_.npcQuestAlert(npc);
            if (const auto *record = session_.monsterContent().find(npc.npcClass)) entry.overlayHeight = record->overlayHeight - 1;
            view.npcs.emplace(npc.id, entry);
        }
    scene_ = std::move(view);
    return scene_;
}
void LocalNpcClient::submit(NpcIntent intent) {
    std::visit([&](auto value) { session_.submit(GameCommand{std::move(value)}); }, std::move(intent));
}
} // namespace d2x
