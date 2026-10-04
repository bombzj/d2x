#include "gameplay/session/session_impl.hpp"
#include "gameplay/quest/npc_query.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
namespace {
const NpcSpeech *resolveSpeech(const ClassicData &content, const QuestSpeechRequest &request,
                              const WorldObject &npc, std::string_view characterClass) {
    const auto &dialogues = content.npcDialogues;
    if (request.introduction) return introSpeech(dialogues, npc.name, characterClass, npc.act);
    const auto &key = content.questContent.at(questIndex(request.quest)).speechKey;
    const auto *speech = questSpeech(dialogues, key, request.state, npc.name);
    if (!speech && !request.fallbackState.empty()) speech = questSpeech(dialogues, key, request.fallbackState, npc.name);
    return speech;
}
std::string unreadKey(const ClassicData &content, const QuestSpeechRequest &request, const WorldObject &npc) {
    return content.questContent.at(questIndex(request.quest)).speechKey + "/" + std::string(request.state) + "/" + npc.name;
}
} // namespace
QuestNpcFacts GameSessionImpl::questNpcFacts(const WorldObject &npc) const {
    QuestNpcFacts facts;
    const auto &character = state().player.character;
    const auto difficulty = size_t(state().population.difficulty);
    facts.act = npc.act;
    facts.level = int(region().definition.id);
    facts.npcClass = npc.npcClass;
    facts.characterLevel = character.level;
    facts.introduced = character.npcIntroductions.at(difficulty).contains(npcIntroductionKey(npc.name, npc.act));
    facts.hasIntroduction = introSpeech(content_.npcDialogues, npc.name, character.characterClass, npc.act) != nullptr;
    facts.preludes = character.questPreludes.at(difficulty);
    facts.items.scroll = carriesQuestItem(content_.staffRecipe.scroll);
    facts.items.cube = carriesQuestItem(content_.cubeCode);
    facts.items.shaft = carriesQuestItem(content_.staffRecipe.inputs[0]);
    facts.items.head = carriesQuestItem(content_.staffRecipe.inputs[1]);
    facts.items.staff = carriesQuestItem(content_.staffRecipe.output);
    // Act I rewards require the original backpack/equipment locations.
    for (auto container : {playerContainers_.backpack, playerContainers_.equipment})
        for (auto id : inventory_.contents(container)) {
            const auto *item = inventory_.item(id);
            if (item) {
                facts.items.bark |= item->definition == "bks";
                facts.items.malus |= item->definition == "hdm";
            }
        }
    return facts;
}
std::vector<std::pair<QuestId, const NpcSpeech *>> GameSessionImpl::npcQuestTopics(EntityId npc) const {
    std::vector<std::pair<QuestId, const NpcSpeech *>> result;
    const auto *target = object(npc);
    if (!target || target->npcClass.empty()) return result;
    const auto query = queryNpcQuests(state().player.character.quests.at(size_t(state().population.difficulty)), questNpcFacts(*target));
    for (const auto &request : query.topics)
        if (const auto *speech = resolveSpeech(content_, request, *target, state().player.character.characterClass))
            result.emplace_back(request.quest, speech);
    return result;
}
NpcQuestDialogue GameSessionImpl::npcQuestDialogue(EntityId npc) const {
    const auto *target = object(npc);
    if (!target || target->npcClass.empty()) return {};
    const auto query = queryNpcQuests(state().player.character.quests.at(size_t(state().population.difficulty)), questNpcFacts(*target));
    if (query.prelude) {
        if (const auto *speech = arrivalSpeech(content_.npcDialogues, target->name, target->act)) {
            NpcQuestDialogue result;
            result.speech = speech;
            result.automatic = result.alert = true;
            result.prelude = query.prelude;
            return result;
        }
    }
    for (const auto &request : query.dialogues) {
        auto key = request.unread ? unreadKey(content_, request.speech, *target) : std::string{};
        if (request.unread && !pendingNpcQuestMessages_.contains(key)) continue;
        const auto *speech = resolveSpeech(content_, request.speech, *target, state().player.character.characterClass);
        if (!speech) continue;
        NpcQuestDialogue result;
        result.speech = speech;
        if (request.advances) result.advancesQuest = request.speech.quest;
        result.automatic = request.automatic;
        result.readKey = std::move(key);
        result.alert = request.alert;
        result.staffExplanation = request.staffExplanation;
        return result;
    }
    return {};
}
bool GameSessionImpl::npcQuestAlert(const WorldObject &npc) const {
    if (npc.questHidden || npc.npcClass.empty() || engagedNpc_ == npc.id || state().player.actions.dead ||
        (!region().definition.safe && !(int(region().definition.id) == 73 && npc.npcClass == "tyrael1"))) return false;
    const auto query = queryNpcQuests(state().player.character.quests.at(size_t(state().population.difficulty)), questNpcFacts(npc));
    if (query.introductionAlert) return true;
    if (query.prelude) {
        if (arrivalSpeech(content_.npcDialogues, npc.name, npc.act)) return true;
    }
    // The active filter is independent of the default conversation selection.
    for (const auto &request : query.dialogues) {
        if (!request.alert || (request.unread && !pendingNpcQuestMessages_.contains(unreadKey(content_, request.speech, npc)))) continue;
        if (resolveSpeech(content_, request.speech, npc, state().player.character.characterClass)) return true;
    }
    return false;
}

std::optional<unsigned> GameSessionImpl::denMonstersRemaining() const {
    if (!denRegion_) return {};
    const AreaState *den = state().area.region == *denRegion_ ? &state().area : nullptr;
    if (!den)
        for (const auto &area : areas_.parked())
            if (area.region == *denRegion_) { den = &area; break; }
    if (!den || !den->initialized) return {};
    return unsigned(den->pendingSpawns.size()) + unsigned(std::count_if(
        den->enemies.begin(), den->enemies.end(), [](const Enemy &enemy) { return enemy.hp > 0; }));
}
} // namespace d2x
