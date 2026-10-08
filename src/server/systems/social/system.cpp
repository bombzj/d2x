#include "system.hpp"
#include "server/player_store.hpp"
#include <algorithm>
namespace d2x::server::social {
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if (request.action != Action::Chat) return {};
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area) return {DomainStatus::InvalidActor, {}};
    if (request.target || request.text.empty() || request.text.size() > 255 ||
        !std::all_of(request.text.begin(), request.text.end(), [](unsigned char c) { return c >= 32 && c < 127; })) return {DomainStatus::InvalidRequest, {}};
    ChatFact fact{player->actor, player->persistent.player.name, request.text, {}};
    for (const auto &[id, peer] : ports_.players.all()) if (peer.entered) fact.recipients.push_back(id);
    const auto published = ports_.events.publish({0, actor.tick, {}, {AudienceKind::Game, {}, actor.area}, {std::move(fact)}});
    return published ? DomainResult<>{DomainStatus::Applied, std::monostate{}} : DomainResult<>{published.status, {}};
}
}
