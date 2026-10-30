#include "system.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "world/interaction_geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::npc {
void System::initializeControl(uint64_t &gameRandom) {
    if(state_.controlRandom) throw std::logic_error("NPC control is already initialized");
    state_.controlRandom=childRandom(gameRandom);
}
DomainResult<VendorControlPublication> System::prepareVendorControl(uint64_t expected,uint64_t next,const NpcRule &rule) const {
    if(state_.controlRandom!=expected) return {DomainStatus::Stale,{}};
    VendorControlPublication publication{expected,next,state_.hirelings};
    if(!rule.hirelingNames.empty() && !publication.hirelings.contains(rule.nativeClass)) {
        if(rule.hirelingNames.size()>69) return {DomainStatus::NotImplemented,{}};
        auto roster=prepareHirelings(rule.hirelingNames,1,publication.next);
        publication.hirelings.emplace(rule.nativeClass,std::move(roster));
    }
    return {DomainStatus::Applied,std::move(publication)};
}
void System::publishVendorControl(VendorControlPublication publication) {
    if(state_.controlRandom!=publication.expected) throw std::logic_error("Stale native vendor control publication");
    state_.hirelings.swap(publication.hirelings);state_.controlRandom=publication.next;
}
HirelingRoster System::prepareHirelings(std::span<const uint16_t> names,uint64_t revision,uint64_t &random) const {
    HirelingRoster result;result.revision=revision;
    for(const auto name:names) result.slots.push_back({name,rollRandom(random)});
    for(int count=0;count<10;++count) {
        const auto start=limitedRandom(random,uint32_t(result.slots.size()));
        auto index=start;
        while(result.slots[index].available) {
            index=(index+1)%uint32_t(result.slots.size());
            if(index==start) break;
        }
        if(result.slots[index].available) break;
        result.slots[index].available=true;
    }
    return result;
}
DomainResult<HirelingRoster> System::hirelingRoster(const NpcRule &rule) {
    if(rule.hirelingNames.empty() || rule.hirelingNames.size()>69 || !state_.controlRandom)
        return {DomainStatus::NotImplemented,{}};
    if(const auto found=state_.hirelings.find(rule.nativeClass);found!=state_.hirelings.end())
        return {DomainStatus::Applied,found->second};
    auto random=*state_.controlRandom;
    auto roster=prepareHirelings(rule.hirelingNames,1,random);
    state_.hirelings.emplace(rule.nativeClass,roster);state_.controlRandom=random;
    return {DomainStatus::Applied,std::move(roster)};
}
bool System::hirelingAvailable(int seller,uint64_t revision,uint16_t name,uint32_t seed) const {
    const auto found=state_.hirelings.find(seller);
    if(found==state_.hirelings.end() || found->second.revision!=revision || revision==UINT64_MAX) return false;
    return std::any_of(found->second.slots.begin(),found->second.slots.end(),[&](const auto &slot) {
        return slot.name==name && slot.seed==seed && slot.available && !slot.hired;
    });
}
DomainResult<VendorControlPublication> System::prepareHirelingHired(int seller,uint64_t revision,uint16_t name,uint32_t seed) const {
    if(!state_.controlRandom || !hirelingAvailable(seller,revision,name,seed)) return {DomainStatus::Stale,{}};
    VendorControlPublication publication{*state_.controlRandom,*state_.controlRandom,state_.hirelings};
    auto &roster=publication.hirelings.at(seller);
    for(auto &slot:roster.slots) if(slot.name==name) {slot.hired=true;break;}
    if(std::none_of(roster.slots.begin(),roster.slots.end(),[](const auto &slot){return slot.available && !slot.hired;})) {
        std::vector<uint16_t> names;for(const auto &slot:roster.slots) names.push_back(slot.name);
        roster=prepareHirelings(names,roster.revision+1,publication.next);
    }
    return {DomainStatus::Applied,std::move(publication)};
}
Vec System::position(const ActorContext &actor, const AreaNpc &npc) const {
    const auto *unit=ports_.monsters.find(npc.id);
    if(!unit || !unit->questNpc) throw std::logic_error("NPC has no live actor");
    return unit->position+ports_.areas.at(unit->area).definition.origin-ports_.areas.at(actor.area).definition.origin;
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
    const auto *unit=ports_.monsters.find(id);
    if (!found || !unit || !unit->questNpc || unit->area!=npcArea || !unit->alive() || !found->rule.interactable ||
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
std::pair<Conversation,NpcMessagesFact> System::offer(const ActorContext &actor,const AreaNpc &npc,const CharacterRecord &record) const {
    Conversation next{npc.id,actor.area,state_.next,{}};NpcMessagesFact fact{npc.id,{}};
    next.actor=actor.actor;next.areaGeneration=actor.areaGeneration;
    for(const auto &message:ports_.quests.dialogues(actor,npc.rule)) {
        next.pendingQuests.emplace(message.message.text,message.quest);
        auto offered=message.message;
        // QUESTS_InitScrollTextChain maps authored menu1 to wire menu0.
        if(offered.menu==1) offered.menu=0;
        fact.messages.push_back(offered);
    }
    if(!record.npcIntroductions.at(size_t(ports_.settings.difficulty)).contains(npc.rule.introduction)) {
        const auto intro=npc.rule.introductions.find(record.characterClass);
        if(intro!=npc.rule.introductions.end() && !next.pendingQuests.contains(intro->second)) {
            next.pendingIntroduction=intro->second;
            fact.messages.push_back({0,intro->second});
        }
    }
    return {std::move(next),std::move(fact)};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if(std::holds_alternative<ConfirmNpcVoice>(request.intent)) {
        const auto *player=ports_.players.find(actor.player);
        if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
        const auto nativeClass=std::get<ConfirmNpcVoice>(request.intent).nativeClass;
        if(!player->rules.character || nativeClass>=player->rules.character->nativeMonsterCount) return {DomainStatus::InvalidRequest,{}};
        const auto &indices=player->rules.character->nativeNpcIntroductionBits;
        const auto found=indices.find(int(nativeClass));const auto bit=found==indices.end()?0u:found->second;
        auto record=player->persistent.player;auto &flags=record.npcVoiceFlags.at(size_t(ports_.settings.difficulty));
        flags.at(bit/8)&=uint8_t(~(1u<<(bit%8)));
        auto plan=ports_.transactions.prepare(transactions::CharacterEdit{actor,player->inventoryRevision,player->characterRevision,std::move(record)});
        return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
    }
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
        const auto pending=current->second.pendingQuests.find(*talk.message);
        if(!introduction && pending==current->second.pendingQuests.end()) return {DomainStatus::Stale,{}};
        if(!introduction) {
            auto result=ports_.quests.execute(actor,{quests::Action::Acknowledge,pending->second,npc->id,talk.message});
            if(isDeferred(result.status)) {
                // Native callbacks wait synchronously for content/output. Keep
                // one accepted acknowledgement per player while those inputs
                // arrive; the normal approach continuation revalidates the
                // conversation, actor, area and locomotion before resuming it.
                const auto queued=approaches_.find(actor.player);
                const bool continuing=queued!=approaches_.end() && queued->second.actor.sequence==actor.sequence;
                approaches_.insert_or_assign(actor.player,Approach{actor,request,player.locomotionSequence});
                return continuing?result:DomainResult<>{DomainStatus::Applied,std::monostate{}};
            }
            if(result) {
                std::map<uint16_t,QuestId> next;
                for(const auto &message:ports_.quests.dialogues(actor,npc->rule)) next.emplace(message.message.text,message.quest);
                current->second.refresh=next!=current->second.pendingQuests;
                current->second.pendingQuests.erase(pending);
            }
            return result;
        }
        record.npcIntroductions.at(size_t(ports_.settings.difficulty)).insert(npc->rule.introduction);
        for(const auto &prelude:questPreludes) if(npc->rule.code==prelude.npcClass && ports_.areas.at(actor.area).definition.act==prelude.act) record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(prelude.id))=true;
        auto plan = ports_.transactions.prepare(transactions::CharacterEdit{actor,player.inventoryRevision,player.characterRevision,std::move(record)});
        if (!plan) return {plan.status,{}};
        auto result = ports_.transactions.commit(std::move(*plan.value));
        if (result) current->second.pendingIntroduction.reset();
        return result;
    }
    if (talk.action != TalkToNpc::Action::Talk) return {};
    if (state_.next == UINT64_MAX) return {DomainStatus::Capacity, {}};
    auto [next,fact]=offer(actor,*npc,record);
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
    if (healing) {edit.explicitResources=true;edit.transient = healing->transient;edit.publicFacts=healing->publicFacts;edit.resourceBase=healing->resourceBase;edit.petLimitChanges=healing->petLimitChanges;}
    if (healing && (healing->character.hp!=player.persistent.player.hp || healing->character.mana!=player.persistent.player.mana ||
        healing->character.stamina!=player.persistent.player.stamina || healing->transient.states!=player.transient.states || (healing->units && !healing->units->life.empty())))
        edit.publicFacts.emplace_back(SoundFact{npc->id,1,actor.area,10});
    auto plan = ports_.transactions.prepare(std::move(edit),nullptr,{},healing?healing->units:nullptr); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) { if (healing) ports_.effects.commit(std::move(*healing)); state_.conversations.swap(conversations); ++state_.next; } return result;
}
DomainResult<> System::close(PlayerId player) {
    approaches_.erase(player);
    if(const auto current=state_.conversations.find(player);current!=state_.conversations.end()) {
        ports_.quests.npcDeactivated(current->second.npc);
        state_.conversations.erase(current);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::escape(RegionId region,EntityId id,Vec destination) {
    if(state_.escapes.contains(id)) return {DomainStatus::Applied,std::monostate{}};
    const auto *area=ports_.areas.find(region);if(!area) return {DomainStatus::Unavailable,{}};
    const auto *unit=ports_.monsters.find(id);
    for(const auto &npc:area->definition.npcs) if(npc.id==id && npc.rule.code=="act5pow" && unit && unit->alive() && unit->area==region) {
        // A5Q2 counts the release callback independently of movement or a
        // successful portal allocation. Path failure cannot undo that credit.
        state_.escapes.emplace(id,Escape{region,destination,{},false});return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
DomainResult<> System::finishEscape(EntityId id) {
    const auto found=state_.escapes.find(id);
    if(found==state_.escapes.end()) return {DomainStatus::Stale,{}};
    if(found->second.escaped) return {DomainStatus::Applied,std::monostate{}};
    const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::Stale,{}};
    const auto removed=ports_.world.moveQuestNpc(unit->area,id,unit->position,unit->position,true);
    if(!removed) return removed;
    found->second.escaped=true;found->second.route.clear();
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked=false;
    for(auto &[id,current]:state_.conversations) {
        if(!current.refresh) continue;
        const auto *player=ports_.players.find(id);if(!player) continue;
        const auto *area=ports_.areas.find(player->area);if(!area) continue;
        const ActorContext actor{id,player->actor,player->area,area->generation,0,tick.tick};
        const auto *npc=find(actor,current.npc,true);if(!npc) continue;
        if(state_.next==UINT64_MAX) {blocked=true;continue;}
        auto [next,fact]=offer(actor,*npc,player->persistent.player);
        fact.activate=false;
        if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,id,player->area},{std::move(fact)}})) {blocked=true;continue;}
        current=std::move(next);++state_.next;
    }
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
        if(isDeferred(result.status)) ++it;else it=approaches_.erase(it);
    }
    std::erase_if(state_.conversations,[&](const auto &entry){
        if(conversation(entry.first)) return false;
        ports_.quests.npcDeactivated(entry.second.npc);return true;
    });
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
