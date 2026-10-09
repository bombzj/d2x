#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/npc/system.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::crafting {
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *p = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area ||
        !area || area->generation != actor.areaGeneration || p->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    uint64_t conversation=0;
    auto normalized=request;
    if (!std::holds_alternative<TransmuteCube>(request.intent) && !std::holds_alternative<SocketItem>(request.intent)) {
        const auto *lease=ports_.npc.conversation(actor.player);
        const auto target=std::visit([](const auto &r)->EntityId { if constexpr(requires {r.npc;}) return r.npc; else return {}; },request.intent);
        if(!lease || lease->npc!=target) return {DomainStatus::InvalidRequest,{}};
        conversation=lease->revision;
        if(const auto *reward=std::get_if<RewardItem>(&request.intent)) {
            const auto *npc=ports_.npc.find(actor,target,true);if(!npc) return {DomainStatus::InvalidRequest,{}};
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
    state_.pending.emplace(actor.player, Pending{actor, normalized, state_.next, initialRandom(rollRandom(random)),p->inventoryRevision,p->characterRevision,conversation});
    ++state_.next; ports_.random = random;
    return {DomainStatus::Applied, std::monostate{}};
}
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for (const auto &[id, pending] : state_.pending) {
        Preparation source; source.pending = pending; source.difficulty = ports_.settings.difficulty;
        if (const auto *p = ports_.players.find(id)) {
            source.character = p->persistent; source.classCode = p->definition.code;
            source.inventoryRevision = pending.inventoryRevision; source.characterRevision = pending.characterRevision;
            source.storage = ports_.inventory.storageAccess(id);
            source.cube = ports_.inventory.cubeAccess(id);
            if(pending.conversation) {
                const auto *lease=ports_.npc.conversation(id);
                if(!lease || lease->revision!=pending.conversation) continue;
                const auto *npc=ports_.npc.find(pending.actor,lease->npc,true);
                if(!npc) continue;
                source.npc=npc->rule.code;
            }
        }
        result.push_back(std::move(source));
    }
    return result;
}
DomainResult<> System::install(Prepared prepared) {
    const auto &source = prepared.source; const auto &actor = source.pending.actor;
    const auto queued = state_.pending.find(actor.player);
    if (queued == state_.pending.end() || queued->second.token != source.pending.token) return {DomainStatus::Stale, {}};
    const auto fail = [&](DomainStatus status) -> DomainResult<> {
        if(source.pending.conversation) {
            const auto npc=std::visit([](const auto &r)->EntityId {if constexpr(requires {r.npc;}) return r.npc;else return {};},source.pending.request.intent);
            const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{npc,7}}});
            if(!output) return {output.status,{}};
        }
        state_.pending.erase(queued);return {status,{}};
    };
    const auto *p = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area ||
        !area || area->generation != actor.areaGeneration || p->persistent.player.hp <= 0) return fail(DomainStatus::InvalidActor);
    if(source.pending.conversation) {
        const auto *lease=ports_.npc.conversation(actor.player);
        if(!lease || lease->revision!=source.pending.conversation) return fail(DomainStatus::Stale);
    }
    if (p->inventoryRevision != source.inventoryRevision || p->characterRevision != source.characterRevision ||
        source.storage != ports_.inventory.storageAccess(actor.player) || source.cube!=ports_.inventory.cubeAccess(actor.player)) return fail(DomainStatus::Stale);
    if (!prepared.deferred.empty()) { state_.deferred = std::move(prepared.deferred); return fail(DomainStatus::Unavailable); }
    // Check reliable output before allocating identities or preparing the
    // transaction. Queue pressure must retain the request and its random seed.
    if (!ports_.transactions.hasOutputCapacity(1)) return {DomainStatus::Capacity, {}};
    if (!ports_.definitions || !prepared.equipment || !p->rules.character || !p->rules.equipment ||
        (prepared.outputs.empty() && !prepared.portal) || prepared.outputs.size() > 16) return fail(DomainStatus::InvalidRequest);
    std::optional<travel::SpecialPortalPlan> portal;
    if(prepared.portal) {auto ready=ports_.travel.prepareSpecialPortal(actor,*prepared.portal,source.pending.seed);if(!ready) return ready.status==DomainStatus::Capacity?DomainResult<>{ready.status,{}}:fail(ready.status);portal=std::move(*ready.value);}
    size_t identities=0;
    for(const auto &out:prepared.outputs) if(!out.replacement) identities+=1+out.item.socketedItems.size();
    if(!ports_.items.identityCapacity(identities)) return fail(DomainStatus::Capacity);
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
                if(!source.pending.conversation) return fail(DomainStatus::Conflict);
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
        auto world=inventory::detail::prepareSpill(*p,draft.edit,*rules,ports_.items);if(!world) return fail(world.status);
        for(const auto &item:draft.edit.spilled) edit.publicFacts.emplace_back(GroundDropFact{world.value->next.world.items.at(item.id)});
        edit.world=std::move(*world.value);
    }
    edit.equipment = std::move(rules);
    if(prepared.character) {
        edit.character=std::move(prepared.character);
        edit.facts.emplace_back(QuestFact{actor.player,*edit.character,ports_.settings.difficulty,0});
        if(source.pending.conversation) edit.facts.emplace_back(NpcServiceFact{std::visit([](const auto &r)->EntityId {if constexpr(requires {r.npc;}) return r.npc;else return {};},source.pending.request.intent),6});
    }
    if(std::holds_alternative<TransmuteCube>(source.pending.request.intent)) edit.facts.emplace_back(SoundFact{p->actor,0,p->area,4});
    auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return fail(plan.status);
    auto result = ports_.transactions.commit(std::move(*plan.value));
    if (!result) return result; // Backpressure preserves the seed and inputs.
    if(portal) ports_.travel.commitSpecialPortal(std::move(*portal));
    state_.pending.erase(queued); state_.deferred.clear(); return result;
}
StepStatus System::step(TickContext,FrameFacts &) {
    std::erase_if(state_.pending,[&](const auto &entry) {
        const auto &pending=entry.second;const auto *p=ports_.players.find(entry.first);const auto *area=ports_.areas.find(pending.actor.area);
        if(!p || !p->entered || p->actor!=pending.actor.actor || p->area!=pending.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=pending.actor.areaGeneration) return true;
        const auto *lease=ports_.npc.conversation(entry.first);
        return pending.conversation && (!lease || lease->revision!=pending.conversation);
    });
    return StepStatus::Complete;
}
}
