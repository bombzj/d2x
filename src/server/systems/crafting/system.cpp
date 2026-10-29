#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/maps/system.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::crafting {
namespace {
EntityId serviceNpc(const Intent &intent) {
    return std::visit([](const auto &request) -> EntityId {
        if constexpr (requires { request.npc; }) return request.npc;
        else return {};
    }, intent);
}
}
bool System::pendingTransmute(PlayerId player,uint64_t sequence) const {
    const auto found=state_.pending.find(player);const auto *p=ports_.players.find(player);
    return found!=state_.pending.end() && p && p->rules.skills && !p->rules.skills->pd2 &&
        (!sequence || found->second.actor.sequence==sequence) &&
        std::holds_alternative<TransmuteCube>(found->second.request.intent);
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *p = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area ||
        !area || area->generation != actor.areaGeneration || p->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    const auto *transmogrify=std::get_if<TransmogrifyItem>(&request.intent);
    if(transmogrify) {
        const ItemInstance *item=nullptr;const ItemDefinition *definition=nullptr;
        if(transmogrify->source) {
            const auto found=p->persistent.inventory.items.find(transmogrify->source->id);
            if(found==p->persistent.inventory.items.end() || found->second.revision!=transmogrify->source->revision) return {DomainStatus::Stale,{}};
            item=&found->second;definition=ports_.definitions?ports_.definitions->find(item->definition):nullptr;
            if(!definition) return {DomainStatus::Unavailable,{}};
        }
        const auto *stored=item?std::get_if<ContainerLocation>(&item->location):nullptr;
        const bool inStorage=stored && (stored->container==p->persistent.containers.backpack || stored->container==p->persistent.containers.cube || stored->container==p->persistent.containers.stash);
        if(!item || !definition->transmogrify || !inStorage) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{ItemTargetingFact{{},-1,-1}}});
            return {sent.status,sent?std::optional{std::monostate{}}:std::nullopt};
        }
    }
    uint64_t conversation=0;
    auto normalized=request;
    const auto *sacrifice=std::get_if<Pd2SacrificeItem>(&request.intent);
    if(sacrifice) {
        if(!sacrifice->input.id) return ports_.maps.closeSacrifice(actor,sacrifice->altar);
        if(!ports_.maps.sacrificeAccess(actor,sacrifice->altar,sacrifice->input)) return {DomainStatus::InvalidRequest,{}};
    }
    if (!transmogrify && !sacrifice && !std::holds_alternative<TransmuteCube>(request.intent) && !std::holds_alternative<SocketItem>(request.intent)) {
        const auto target=serviceNpc(request.intent);
        const auto access=ports_.npc.service(actor,target);
        if(!access) return {DomainStatus::InvalidRequest,{}};
        conversation=access->conversation->revision;
        if(const auto *reward=std::get_if<RewardItem>(&request.intent)) {
            const auto *npc=access->npc;
            if(npc->rule.code=="charsi") normalized.intent=ImbueItem{target,reward->item};
            else if(npc->rule.code=="larzuk") normalized.intent=SocketQuestItem{target,reward->item};
            else if(npc->rule.code=="drehya") normalized.intent=PersonalizeQuestItem{target,reward->item};
            else return {DomainStatus::InvalidRequest,{}};
        }
    }
    if(std::holds_alternative<TransmuteCube>(request.intent) && !ports_.inventory.cubeAccess(actor.player)) return {DomainStatus::InvalidRequest,{}};
    if (state_.pending.contains(actor.player)) return {DomainStatus::Conflict, {}};
    if (state_.pending.size() >= 8 || state_.next == UINT64_MAX) return {DomainStatus::Capacity, {}};
    auto random = ports_.random;
    state_.pending.emplace(actor.player, Pending{actor, normalized, state_.next,transmogrify?0:sacrifice?random:initialRandom(rollRandom(random)),p->inventoryRevision,p->characterRevision,conversation});
    ++state_.next; ports_.random = random;
    return {DomainStatus::Applied, std::monostate{}};
}
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for (const auto &[id, pending] : state_.pending) {
        if(const auto ready=state_.retained.find(id);ready!=state_.retained.end()) {
            auto source=ready->second.source;source.retained=true;result.push_back(std::move(source));continue;
        }
        Preparation source; source.pending = pending; source.difficulty = ports_.settings.difficulty;
        source.pd2Random=ports_.pd2Random;
        if(std::holds_alternative<Pd2SacrificeItem>(pending.request.intent)) {
            source.pending.seed=ports_.random;
            if(ports_.maps.read().instance) source.pd2BossDifficulty=ports_.maps.read().instance->bossDifficulty;
        }
        if (const auto *p = ports_.players.find(id)) {
            source.unitRandom=p->combatRandom;
            source.character = p->persistent; source.classCode = p->definition.code;
            source.inventoryRevision = pending.inventoryRevision;
            // The character snapshot is read now, after this frame's resource
            // callbacks. Bind its actual revision; the request-time revision
            // would reject even unchanged ingredients after regeneration.
            source.characterRevision = p->characterRevision;
            source.storage = ports_.inventory.storageAccess(id);
            source.cube = ports_.inventory.cubeAccess(id);
            if(pending.conversation) {
                const auto target=serviceNpc(pending.request.intent);
                const auto access=ports_.npc.service(pending.actor,target);
                if(!access || access->conversation->revision!=pending.conversation) continue;
                source.npc=access->npc->rule.code;
            }
        }
        result.push_back(std::move(source));
    }
    return result;
}
DomainResult<> System::install(Prepared prepared) {
    const auto retained=state_.retained.find(prepared.source.pending.actor.player);
    if(retained!=state_.retained.end()) {
        if(retained->second.source.pending.token!=prepared.source.pending.token) return {DomainStatus::Stale,{}};
        prepared=retained->second;
        if(const auto *current=ports_.players.find(prepared.source.pending.actor.player)) prepared.source.characterRevision=current->characterRevision;
    }
    const auto &source = prepared.source; const auto &actor = source.pending.actor;
    const auto queued = state_.pending.find(actor.player);
    if (queued == state_.pending.end() || queued->second.token != source.pending.token) return {DomainStatus::Stale, {}};
    const auto fail = [&](DomainStatus status) -> DomainResult<> {
        if(const auto *sacrifice=std::get_if<Pd2SacrificeItem>(&source.pending.request.intent)) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{sacrifice->altar,7}}});
            if(!sent) return {sent.status,{}};
        }
        if(std::holds_alternative<TransmogrifyItem>(source.pending.request.intent)) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{ItemTargetingFact{{},-1,-1}}});
            if(!sent) return {sent.status,{}};
        }
        if(source.pending.conversation) {
            const auto npc=serviceNpc(source.pending.request.intent);
            const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{npc,7}}});
            if(!output) return {output.status,{}};
        }
        state_.retained.erase(actor.player);state_.pending.erase(queued);return {status,{}};
    };
    const auto *p = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area ||
        !area || area->generation != actor.areaGeneration || p->persistent.player.hp <= 0) return fail(DomainStatus::InvalidActor);
    if(source.pending.conversation) {
        const auto target=serviceNpc(source.pending.request.intent);
        const auto access=ports_.npc.service(actor,target);
        if(!access || access->conversation->revision!=source.pending.conversation) return fail(DomainStatus::Stale);
    }
    if (p->inventoryRevision != source.inventoryRevision || p->characterRevision != source.characterRevision ||
        source.storage != ports_.inventory.storageAccess(actor.player) || source.cube!=ports_.inventory.cubeAccess(actor.player)) return fail(DomainStatus::Stale);
    if(prepared.retailPortalNoop) {
        if(!std::holds_alternative<TransmuteCube>(source.pending.request.intent) || !source.cube ||
            !p->rules.skills || p->rules.skills->pd2) return fail(DomainStatus::InvalidRequest);
        state_.retained.erase(actor.player);state_.pending.erase(queued);state_.deferred.clear();
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (!prepared.deferred.empty()) { state_.deferred = std::move(prepared.deferred); return fail(DomainStatus::Unavailable); }
    if(prepared.pd2Random && ports_.pd2Random!=source.pd2Random) return fail(DomainStatus::Stale);
    const auto *sacrifice=std::get_if<Pd2SacrificeItem>(&source.pending.request.intent);
    if(sacrifice && (!ports_.maps.sacrificeAccess(actor,sacrifice->altar,sacrifice->input) ||
        (prepared.nativeGameRandom && ports_.random!=source.pending.seed))) return fail(DomainStatus::Stale);
    if(sacrifice && retained==state_.retained.end()) {
        auto cached=prepared;cached.pd2Random.reset();cached.nativeGameRandom.reset();
        state_.retained.emplace(actor.player,std::move(cached));
        // Reserve this invocation's draws once. Reliable-output pressure
        // retains the actual result rather than rolling the altar again.
        if(prepared.pd2Random) ports_.pd2Random=*prepared.pd2Random;
        if(prepared.nativeGameRandom) ports_.random=*prepared.nativeGameRandom;
        prepared.pd2Random.reset();prepared.nativeGameRandom.reset();
    }
    // Check reliable output before allocating identities or preparing the
    // transaction. Queue pressure must retain the request and its random seed.
    if (!ports_.transactions.hasOutputCapacity(1)) return {DomainStatus::Capacity, {}};
    if (!ports_.definitions || !prepared.equipment || !p->rules.character || !p->rules.equipment ||
        (prepared.outputs.empty() && !prepared.portal) || prepared.outputs.size() > 16) return fail(DomainStatus::InvalidRequest);
    std::optional<travel::SpecialPortalPlan> portal;
    if(prepared.portal) {auto ready=ports_.travel.prepareSpecialPortal(actor,*prepared.portal,source.pending.seed);if(!ready) return isDeferred(ready.status)?DomainResult<>{ready.status,{}}:fail(ready.status);portal=std::move(*ready.value);}
    size_t identities=0;
    for(const auto &out:prepared.outputs) if(!out.replacement) identities+=1+out.item.socketedItems.size();
    if(!ports_.items.identityCapacity(identities)) return {DomainStatus::Capacity,{}};
    auto rules = std::make_shared<EquipmentRules>(*p->rules.equipment);
    inventory::detail::Draft draft(*p, *ports_.definitions, *rules, *p->rules.character); draft.storage = source.storage;
    for (const auto handle : prepared.consumed) {
        const auto *item = draft.resolve(handle);
        if (!item || item->revision == UINT64_MAX) return fail(DomainStatus::Stale);
        draft.edit.changes.push_back({item->id, item->revision + 1, ItemChangeKind::Removed, item->location, {}, 0});
        draft.edit.inventory.items.erase(item->id); rules->items.erase(item->id);
    }
    for (auto &output : prepared.outputs) {
        const auto temporary = output.item.id;
        if (!prepared.equipment->items.contains(temporary)) return fail(DomainStatus::Unavailable);
        auto item = std::move(output.item); std::optional<ItemLocation> before;
        if (output.replacement) {
            const auto *original = draft.resolve(*output.replacement);
            if (!original || original->revision == UINT64_MAX) return fail(DomainStatus::Stale);
            before = original->location; item.id = original->id; item.revision = original->revision + 1;
            draft.edit.inventory.items.erase(item.id);
        } else { item.id = ports_.items.reserveIdentity(); item.revision = 1; }
        for (size_t index = 0; index < item.socketedItems.size(); ++index) {
            if(!output.replacement) {item.socketedItems[index].id=ports_.items.reserveIdentity();item.socketedItems[index].revision=1;}
            item.socketedItems[index].location = SocketLocation{item.id, unsigned(index)};
        }
        auto desired = std::get_if<ContainerLocation>(&item.location) ? std::get<ContainerLocation>(item.location) : ContainerLocation{output.container, {}};
        desired.container = output.container;
        item.location = ContainerLocation{p->persistent.containers.cursor, {}};
        const auto id = item.id; draft.edit.inventory.items.emplace(id, item);
        rules->items.insert_or_assign(id, prepared.equipment->items.at(temporary));
        if (!draft.fits(id, desired)) {
            const auto space = draft.space(id, output.container);
            if (!space) {
                if(!source.pending.conversation && !sacrifice && !std::holds_alternative<TransmogrifyItem>(source.pending.request.intent)) return fail(DomainStatus::Conflict);
                draft.edit.spilled.push_back(item);draft.edit.inventory.items.erase(id);continue;
            }
            desired = *space;
        }
        draft.edit.inventory.items.at(id).location = desired;
        rules->items.insert_or_assign(id, prepared.equipment->items.at(temporary));
        if (before && *before != ItemLocation{desired}) draft.edit.changes.push_back({id,item.revision,ItemChangeKind::Moved,before,ItemLocation{desired},item.quantity});
        draft.edit.changes.push_back({id,item.revision,before ? ItemChangeKind::PropertiesChanged : ItemChangeKind::Created,before,ItemLocation{desired},item.quantity});
    }
    rules->includeSets(*prepared.equipment);
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),p->persistent.player.weaponSet};
    if(!draft.edit.spilled.empty()) {
        auto world=inventory::detail::prepareSpill(*p,draft.edit,*rules,ports_.items);if(!world) return isDeferred(world.status)?DomainResult<>{world.status,{}}:fail(world.status);
        for(const auto &item:draft.edit.spilled) {
            const auto &ground=world.value->next.world.items.at(item.id);
            if(const auto original=p->persistent.inventory.items.find(item.id);original!=p->persistent.inventory.items.end())
                edit.changes.push_back({item.id,item.revision,ItemChangeKind::Moved,original->second.location,ground.location,item.quantity});
            edit.publicFacts.emplace_back(GroundDropFact{ground});
        }
        edit.world=std::move(*world.value);
    }
    edit.equipment = std::move(rules);
    if(sacrifice) edit.facts.emplace_back(NpcServiceFact{sacrifice->altar,7});
    if(prepared.character) {
        edit.character=std::move(prepared.character);
        edit.facts.emplace_back(ports_.quests.projection(actor.player,*edit.character,actor.area,false));
        if(source.pending.conversation) edit.facts.emplace_back(NpcServiceFact{serviceNpc(source.pending.request.intent),6});
    }
    if(std::holds_alternative<TransmogrifyItem>(source.pending.request.intent)) {
        edit.random=prepared.unitRandom;
        edit.facts.emplace_back(SoundFact{p->actor,0,p->area,4});
        edit.facts.emplace_back(ItemTargetingFact{{},-1,-1});
    }
    if(std::holds_alternative<TransmuteCube>(source.pending.request.intent)) edit.facts.emplace_back(SoundFact{p->actor,0,p->area,4});
    auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return isDeferred(plan.status)?DomainResult<>{plan.status,{}}:fail(plan.status);
    auto result = ports_.transactions.commit(std::move(*plan.value));
    if (!result) return result; // Backpressure preserves the seed and inputs.
    if(prepared.pd2Random) ports_.pd2Random=*prepared.pd2Random;
    if(prepared.nativeGameRandom) ports_.random=*prepared.nativeGameRandom;
    if(sacrifice) ports_.maps.completeSacrifice(actor,sacrifice->altar);
    if(portal) ports_.travel.commitSpecialPortal(std::move(*portal));
    state_.retained.erase(actor.player);state_.pending.erase(queued); state_.deferred.clear(); return result;
}
StepStatus System::step(TickContext,FrameFacts &) {
    std::erase_if(state_.pending,[&](const auto &entry) {
        const auto &pending=entry.second;const auto *p=ports_.players.find(entry.first);const auto *area=ports_.areas.find(pending.actor.area);
        if(!p || !p->entered || p->actor!=pending.actor.actor || p->area!=pending.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=pending.actor.areaGeneration) return true;
        const auto *lease=ports_.npc.conversation(entry.first);
        return pending.conversation && (!lease || lease->revision!=pending.conversation);
    });
    std::erase_if(state_.retained,[&](const auto &entry){return !state_.pending.contains(entry.first);});
    return StepStatus::Complete;
}
}
