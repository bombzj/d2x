#include "system.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/world/system.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::npc {
Vec System::position(const ActorContext &actor, const AreaNpc &npc) const {
    for (const auto &[id, area] : ports_.areas.all()) {
        (void)id;
        for (const auto &entry : area.definition.npcs) if (&entry == &npc)
            return npc.position + area.definition.origin - ports_.areas.at(actor.area).definition.origin;
    }
    throw std::logic_error("NPC has no owning area");
}
const AreaNpc *System::find(const ActorContext &actor, EntityId id, bool conversation) const {
    const auto *player = ports_.players.find(actor.player); const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0 || !area || area->generation != actor.areaGeneration) return nullptr;
    // PlrMsg::sub_6FC828D0 resolves the real GUID across active rooms. An
    // adjacent level is not a different act or a different game instance.
    Vec origin; const auto navigation = area->definition.collision.linkedNavigation(origin);
    const AreaNpc *found = nullptr;
    RegionId npcArea = actor.area;
    for (const auto &[region, candidate] : ports_.areas.all()) {
        if (candidate.definition.act != area->definition.act ||
            std::none_of(navigation.navigationSources.begin(), navigation.navigationSources.end(),
                [&](const auto &entry) { return entry.grid == &candidate.definition.collision; })) continue;
        for (const auto &entry : candidate.definition.npcs) if (entry.id == id) { found = &entry; npcArea = region; break; }
        if (found) break;
    }
    if (!found || found->hidden || !found->rule.interactable ||
        !ports_.quests.npcVisible(player->persistent.player,found->rule.code,npcArea,found->rule.questInitFunction) ||
        nativeUnitDistance(player->position,2,position(actor,*found),found->rule.size)>8) return nullptr;
    if (conversation) { const auto current = state_.conversations.find(actor.player); if (current == state_.conversations.end() || current->second.npc != id || current->second.area != actor.area || current->second.actor != actor.actor || current->second.areaGeneration != actor.areaGeneration) return nullptr; }
    return found;
}
std::optional<ServiceAccess> System::service(const ActorContext &actor, EntityId id) const {
    const auto *target = find(actor, id, true);
    if (!target) return std::nullopt;
    return ServiceAccess{target, &state_.conversations.at(actor.player)};
}
const Conversation *System::conversation(PlayerId id) const {
    const auto current = state_.conversations.find(id);
    const auto *player = ports_.players.find(id);
    if (current == state_.conversations.end() || !player) return nullptr;
    const auto *area = ports_.areas.find(player->area);
    if (!area) return nullptr;
    const ActorContext actor{id, player->actor, player->area, area->generation, 0, 0};
    return find(actor, current->second.npc, true) ? &current->second : nullptr;
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
    if(talk.action==TalkToNpc::Action::Talk) {
        if(nativeUnitDistance(player.position,2,position(actor,*npc),npc->rule.size)>6) {
            if(ports_.movement.execute(actor,{MovementAction::ApproachNpc,{},true,npc->id})!=CommandStatus::Applied)
                return {DomainStatus::Unavailable,{}};
            approaches_.insert_or_assign(actor.player,Approach{actor,request,player.locomotionSequence});
            return {DomainStatus::Applied,std::monostate{}};
        }
        ports_.movement.execute(actor,{MovementAction::Stop,{},false});
    }
    // 2F confirms the conversation created by 13; do not resend 27/28 and
    // restart the client's activation/initialization exchange.
    if(talk.action==TalkToNpc::Action::Initialize)
        return {find(actor,npc->id,true)?DomainStatus::Applied:DomainStatus::Stale,{}};
    auto record = player.persistent.player;
    if (talk.action == TalkToNpc::Action::Acknowledge) {
        auto current = state_.conversations.find(actor.player);
        if (!find(actor,npc->id,true) || current == state_.conversations.end() || !talk.message) return {DomainStatus::Stale,{}};
        const bool introduction=current->second.pendingIntroduction==talk.message;
        if(!introduction && current->second.pendingMessage!=talk.message) return {DomainStatus::Stale,{}};
        if(current->second.quest && !introduction) {
            auto result=ports_.quests.execute(actor,{quests::Action::Acknowledge,current->second.questId,npc->id,talk.message});
            if(result) current->second.pendingMessage.reset();
            return result;
        }
        record.npcIntroductions.at(size_t(ports_.settings.difficulty)).insert(npc->rule.introduction);
        for(const auto &prelude:questPreludes) if(npc->rule.code==prelude.npcClass && ports_.areas.at(actor.area).definition.act==prelude.act) record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(prelude.id))=true;
        auto plan = ports_.transactions.prepare(transactions::CharacterEdit{actor,player.inventoryRevision,player.characterRevision,std::move(record)});
        if (!plan) return {plan.status,{}};
        auto result = ports_.transactions.commit(std::move(*plan.value));
        if (result) {if(introduction) current->second.pendingIntroduction.reset();else current->second.pendingMessage.reset();}
        return result;
    }
    if (talk.action != TalkToNpc::Action::Talk) return {};
    if (state_.next == UINT64_MAX) return {DomainStatus::Capacity, {}};
    Conversation next{npc->id,actor.area,state_.next,{}}; NpcMessagesFact fact{npc->id,{}};
    next.actor=actor.actor; next.areaGeneration=actor.areaGeneration;
    if(const auto message=ports_.quests.dialogue(actor,npc->rule)) {
        next.quest=true; next.questId=message->quest; next.pendingMessage=message->message.text;
        auto offered=message->message;
        // QUESTS_InitScrollTextChain converts authored menu 1 to wire menu 0.
        // Wire menu 1 is an urgent interaction, not an ordinary quest offer.
        if(offered.menu==1) offered.menu=0;
        fact.messages.push_back(offered);
    }
    if (!record.npcIntroductions.at(size_t(ports_.settings.difficulty)).contains(npc->rule.introduction)) {
        const auto intro = npc->rule.introductions.find(record.characterClass);
        if (intro != npc->rule.introductions.end() && next.pendingMessage!=intro->second) {
            if(next.pendingMessage) next.pendingIntroduction=intro->second;
            else next.pendingMessage=intro->second;
            fact.messages.push_back({0,intro->second});
        }
    }
    // Ordinary gossip is selected by the client from its MPQ, not appended
    // to the server's quest/intro text chain (27).
    auto conversations = state_.conversations; conversations[actor.player] = next;
    std::optional<effects::PotionPlan> healing;
    if (npc->rule.heal) {
        auto prepared = ports_.effects.heal(actor);
        if (!prepared) return {prepared.status, {}};
        healing = std::move(*prepared.value); record = healing->character;
    }
    transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(record)}; edit.facts.emplace_back(std::move(fact));
    if (healing) {edit.transient = healing->transient;edit.publicFacts=healing->publicFacts;}
    if (healing && (healing->character.hp!=player.persistent.player.hp || healing->character.mana!=player.persistent.player.mana ||
        healing->character.stamina!=player.persistent.player.stamina || healing->transient.states!=player.transient.states))
        edit.publicFacts.emplace_back(SoundFact{npc->id,1,actor.area,10});
    auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) { if (healing) ports_.effects.commit(std::move(*healing)); state_.conversations.swap(conversations); ++state_.next; } return result;
}
DomainResult<> System::close(PlayerId player) { approaches_.erase(player);state_.conversations.erase(player); return {DomainStatus::Applied,std::monostate{}}; }
DomainResult<> System::escape(RegionId region,EntityId id,Vec destination) {
    if(state_.escapes.contains(id)) return {DomainStatus::Applied,std::monostate{}};
    const auto *area=ports_.areas.find(region);if(!area) return {DomainStatus::Unavailable,{}};
    for(const auto &npc:area->definition.npcs) if(npc.id==id && npc.rule.code=="act5pow" && !npc.hidden && npc.rule.walkVelocity>0) {
        const auto point=area->definition.collision.nearest(destination,npc.rule.movement);
        auto route=area->definition.collision.path(npc.position,point,false,npc.rule.movement);
        if(route.empty() && (npc.position-point).length()>1) return {DomainStatus::Unavailable,{}};
        state_.escapes.emplace(id,Escape{region,point,std::move(route),false});return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    for(auto it=approaches_.begin();it!=approaches_.end();) {
        auto pending=it->second;pending.actor.tick=tick.tick;
        const auto *player=ports_.players.find(it->first);
        const auto &talk=std::get<TalkToNpc>(pending.request.intent);
        const auto *npc=find(pending.actor,talk.target);
        if(!player || !npc || player->locomotionSequence!=pending.locomotion) {it=approaches_.erase(it);continue;}
        if(!player->route.empty() || nativeUnitDistance(player->position,2,position(pending.actor,*npc),npc->rule.size)>6) {
            if(player->route.empty()) it=approaches_.erase(it);else ++it;
            continue;
        }
        const auto result=execute(pending.actor,pending.request);
        if(result.status==DomainStatus::Capacity) ++it;else it=approaches_.erase(it);
    }
    for(auto &[id,escape]:state_.escapes) {
        if(escape.escaped) continue;
        const auto *area=ports_.areas.find(escape.area);if(!area) continue;
        const auto npc=std::find_if(area->definition.npcs.begin(),area->definition.npcs.end(),[&](const auto &n){return n.id==id;});
        if(npc==area->definition.npcs.end()) continue;
        auto point=npc->position;
        if(!escape.route.empty()) {
            const auto delta=escape.route.front()-point;const auto distance=delta.length(),stride=float(npc->rule.walkVelocity)/16.f;
            point=distance<=stride?escape.route.front():point+delta*(stride/distance);
            if(distance<=stride) escape.route.pop_front();
        }
        const bool escaped=escape.route.empty() && (point-escape.destination).length()<=1;
        if(ports_.world.moveQuestNpc(escape.area,id,point,escape.route.empty()?escape.destination:escape.route.front(),escaped)) escape.escaped=escaped;
    }
    std::erase_if(state_.conversations,[&](const auto &entry){return !conversation(entry.first);});
    return StepStatus::Complete;
}
}
