#include "system.hpp"
#include "server/player_store.hpp"
namespace d2x::server::attributes {
DomainResult<Totals> System::evaluate(EntityId actor) const {
    for (const auto &[id, player] : ports_.players.all()) {
        (void)id;
        if (player.actor == actor) return {DomainStatus::Applied, player.totals};
    }
    return {DomainStatus::InvalidActor, {}};
}
StepStatus System::step(TickContext, FrameFacts &) { return StepStatus::Complete; }
}
