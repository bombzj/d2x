#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
namespace d2x::server::attributes {
DomainResult<Totals> System::evaluate(EntityId actor) const {
    for (const auto &[id, player] : ports_.players.all()) {
        (void)id;
        if (player.actor == actor) return {DomainStatus::Applied, player.totals};
    }
    return {DomainStatus::InvalidActor, {}};
}
StepStatus System::step(TickContext, FrameFacts &) { return StepStatus::Complete; }
StepStatus System::refreshEnvironment(TickContext tick) {
    const auto &pending=ports_.world.environmentNotice();if(!pending) return StepStatus::Complete;
    const auto key=std::pair{pending->value.act,pending->revision};
    if(!state_.environment) {
        state_.players.clear();
        // CLIENTS_AddClient prepends to pClientList. Server PlayerIds are
        // monotonically assigned at admission, so newest clients run first.
        for(auto client=ports_.players.all().rbegin();client!=ports_.players.all().rend();++client)
            state_.players.push_back(client->first);
        state_.environment=key;state_.next=0;
    } else if(*state_.environment!=key) throw std::logic_error("Native environment replaced an unfinished player refresh");
    while(state_.next<state_.players.size()) {
        const auto id=state_.players[state_.next];const auto *player=ports_.players.find(id);
        if(!player) {++state_.next;continue;}
        const auto &area=ports_.areas.at(player->area);
        const ActorContext actor{id,player->actor,player->area,area.generation,0,tick.tick};
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
        edit.refreshEquipment=true;
        if(player->entered && area.definition.act==pending->value.act) edit.facts.emplace_back(pending->value);
        auto prepared=ports_.transactions.prepare(std::move(edit));
        if(!prepared) {
            if(isDeferred(prepared.status)) return StepStatus::Blocked;
            throw std::runtime_error("Native environment player source refresh unavailable");
        }
        const auto committed=ports_.transactions.commit(std::move(*prepared.value));
        if(!committed) {
            if(isDeferred(committed.status)) return StepStatus::Blocked;
            throw std::logic_error("Native environment player refresh lost its candidate");
        }
        ++state_.next;
    }
    ports_.world.acknowledgeEnvironment(key.first,key.second);
    state_.environment.reset();state_.players.clear();state_.next=0;
    return StepStatus::Complete;
}
}
