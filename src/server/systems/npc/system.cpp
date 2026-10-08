#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
namespace d2x::server::npc {
const AreaNpc *System::find(const ActorContext &actor, EntityId id, bool conversation) const {
    const auto *player = ports_.players.find(actor.player); const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0 || !area || area->generation != actor.areaGeneration || !area->definition.town) return nullptr;
    const auto &npcs = area->definition.npcs;
    const auto found = std::find_if(npcs.begin(),npcs.end(),[&](const auto &entry){return entry.id == id;});
    if (found == npcs.end() || (found->position-player->position).length() > 8 || !area->definition.collision.segment(player->position,found->position,id,{0x0801,1})) return nullptr;
    if (conversation) { const auto current = state_.conversations.find(actor.player); if (current == state_.conversations.end() || current->second.npc != id || current->second.area != actor.area) return nullptr; }
    return &*found;
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if (const auto *end=std::get_if<EndNpcConversation>(&request.intent)) {
        const auto current=state_.conversations.find(actor.player);
        if(current!=state_.conversations.end() && current->second.npc!=end->target) return {DomainStatus::Stale,{}};
        return close(actor.player);
    }
    const auto &talk = std::get<TalkToNpc>(request.intent);
    const auto *npc = find(actor,talk.target); if (!npc) return {DomainStatus::InvalidRequest,{}};
    const auto &player = *ports_.players.find(actor.player);
    auto record = player.persistent.player;
    if (talk.action == TalkToNpc::Action::Acknowledge) {
        auto current = state_.conversations.find(actor.player);
        if (current == state_.conversations.end() || current->second.npc != npc->id || !talk.message || current->second.pendingMessage != talk.message) return {DomainStatus::Stale,{}};
        if(current->second.quest) {
            auto result=ports_.quests.execute(actor,{quests::Action::Acknowledge,QuestId::DenOfEvil,npc->id,talk.message});
            if(result) current->second.pendingMessage.reset();
            return result;
        }
        record.npcIntroductions.at(size_t(ports_.settings.difficulty)).insert(npc->rule.introduction);
        auto plan = ports_.transactions.prepare(transactions::CharacterEdit{actor,player.inventoryRevision,player.characterRevision,std::move(record)});
        if (!plan) return {plan.status,{}};
        auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) current->second.pendingMessage.reset(); return result;
    }
    if (talk.action != TalkToNpc::Action::Talk) return {};
    if (state_.next == UINT64_MAX) return {DomainStatus::Capacity, {}};
    Conversation next{npc->id,actor.area,state_.next,{}}; NpcMessagesFact fact{npc->id,{}};
    if(const auto message=ports_.quests.dialogue(actor,npc->rule)) { next.quest=true; next.pendingMessage=message->text; fact.messages.push_back(*message); }
    if (!next.pendingMessage && !record.npcIntroductions.at(size_t(ports_.settings.difficulty)).contains(npc->rule.introduction)) {
        const auto intro = npc->rule.introductions.find(record.characterClass);
        if (intro != npc->rule.introductions.end()) { next.pendingMessage=intro->second; fact.messages.push_back({0,intro->second}); }
    }
    for (const auto text : npc->rule.gossip) { if (fact.messages.size() == 8) break; fact.messages.push_back({1,text}); }
    auto conversations = state_.conversations; conversations[actor.player] = next;
    if (npc->rule.heal) { record.hp=float(player.totals.character.maxLife); record.mana=float(player.totals.character.maxMana); record.stamina=float(player.totals.character.maxStamina); }
    transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(record)}; edit.facts.emplace_back(std::move(fact));
    auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) { state_.conversations.swap(conversations); ++state_.next; } return result;
}
DomainResult<> System::close(PlayerId player) { state_.conversations.erase(player); return {DomainStatus::Applied,std::monostate{}}; }
StepStatus System::step(TickContext, FrameFacts &) {
    std::erase_if(state_.conversations,[&](const auto &entry){const auto *p=ports_.players.find(entry.first); return !p || !p->entered || p->area!=entry.second.area || p->persistent.player.hp<=0;});
    return StepStatus::Complete;
}
}
