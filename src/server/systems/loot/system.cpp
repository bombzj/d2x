#include "system.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/planning.hpp"
namespace d2x::server::loot {
std::set<size_t> System::prepareUniques(const std::set<size_t> &additions) const {
    auto next=state_.uniques; next.insert(additions.begin(),additions.end()); return next;
}
DomainResult<> System::queue(const Request &request) {
    if (state_.pending.contains(request.source.source)) return {DomainStatus::Applied, std::monostate{}};
    if (state_.pending.size() >= 256) return {DomainStatus::Capacity, {}};
    const auto *player = ports_.players.find(request.beneficiary);
    if (!player) return {DomainStatus::InvalidActor, {}};
    Preparation pending{request, player->persistent, player->definition.code, 0,
        player->totals.character.combat.magicFind, player->totals.character.combat.goldFind, state_.uniques};
    if(request.questClaimant) pending.character.player=*request.questClaimant;
    auto random = ports_.random; pending.seed = initialRandom(rollRandom(random));
    const unsigned count=unsigned(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[](const auto &entry){return entry.second.entered;}));
    // No party authority exists yet: only the killer is a qualifying party member.
    pending.effectivePlayers=std::min(std::clamp((count+1)/2,1u,8u),request.source.monsterPlayerCount);
    pending.character.inventory.items.clear();
    state_.pending.emplace(request.source.source, std::move(pending)); ports_.random = random;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::install(EntityId source, items::PreparedBatch batch, std::string deferred) {
    const auto found = state_.pending.find(source);
    if (found == state_.pending.end()) return {DomainStatus::Stale, {}};
    std::set<size_t> uniques = state_.uniques;
    uniques.insert(batch.limitedUniques.begin(), batch.limitedUniques.end());
    const auto &request = found->second.request;
    if (!deferred.empty()) {
        if(request.object) state_.completed.insert_or_assign(source, false);
        state_.deferred = std::move(deferred); state_.pending.erase(found);
        return {DomainStatus::Applied, std::monostate{}};
    }
    if (!ports_.events.hasCapacity(batch.items.size() + (request.object ? 2 : 0), request.object ? 2 : 1))
        return {DomainStatus::Capacity, {}};
    // Allocate follow-up bookkeeping before any authority is committed.
    auto completions = state_.completed;
    if (request.object) completions.insert_or_assign(source, true);
    auto next = ports_.items.prepare(std::move(batch), {request.source.region, request.position});
    if (!next) return {next.status, {}};
    std::vector<DomainFact> drops;
    for(const auto &[id,item]:next.value->world.items)
        if(!ports_.items.read().world.items.contains(id)) drops.emplace_back(GroundDropFact{item});
    if (request.object) {
        const auto *player = ports_.players.find(request.beneficiary);
        const auto fail = [&]() { state_.completed.insert_or_assign(source, false); state_.pending.erase(found); return DomainResult<>{DomainStatus::Applied, std::monostate{}}; };
        if (!player || !player->entered || player->area != request.source.region || player->persistent.player.hp <= 0) return fail();
        inventory::detail::Draft draft(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
        const auto key = request.object->key;
        if (key.id) {
            const auto *item = draft.resolve(key);
            const auto *at = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
            if (!at || at->container != player->persistent.containers.backpack || !item->quantity || item->revision == UINT64_MAX) return fail();
            auto &value = draft.edit.inventory.items.at(key.id); ++value.revision;
            if (--value.quantity) draft.edit.changes.push_back({key.id, value.revision, ItemChangeKind::QuantityChanged, value.location, value.location, value.quantity});
            else { draft.edit.changes.push_back({key.id, value.revision, ItemChangeKind::Removed, value.location, {}, 0}); draft.edit.inventory.items.erase(key.id); }
        }
        const ActorContext actor{player->player, player->actor, player->area, 0, 0, 0};
        transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision, std::move(draft.edit.inventory), std::move(draft.edit.changes), player->persistent.player.weaponSet};
        edit.world = transactions::WorldEdit{ports_.items.read().revision, std::move(*next.value)};
        edit.publicFacts=std::move(drops);
        auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return {plan.status, {}};
        const auto committed = ports_.transactions.commit(std::move(*plan.value)); if (!committed) return committed;
        state_.completed.swap(completions);
    } else {
        if(!drops.empty()) {
            auto sent=ports_.events.publish({0,0,{}, {AudienceKind::Area,{},request.source.region},std::move(drops)});
            if(!sent) return {sent.status,{}};
        }
        // The prepared state already contains final identities and placement.
        ports_.items.commit(std::move(*next.value));
    }
    state_.uniques.swap(uniques); state_.deferred = std::move(deferred); state_.pending.erase(found);
    // GameInstance snapshots current unique availability at each preparation.
    return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::step(TickContext, FrameFacts &) { return StepStatus::Complete; }
}

namespace d2x::server::loot {
std::optional<bool> System::takeCompletion(EntityId source) {
    const auto found = state_.completed.find(source); if (found == state_.completed.end()) return {};
    const bool result = found->second; state_.completed.erase(found); return result;
}
}
