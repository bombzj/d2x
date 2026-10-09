#include "gameplay_dispatch.hpp"

namespace d2x::hosting {
RequestResult submitGameplay(GameplayContext &context, server::CommandPayload payload) {
    const auto view = context.host.read(context.player);
    if (!view) return {RequestStatus::Rejected, CommandStatus::InvalidBinding};
    const auto status = context.host.submit(context.player,
        {++context.sequence, view->actor.region, view->areaGeneration, std::move(payload)});
    if (status == CommandStatus::NotImplemented) return {RequestStatus::NotImplemented, status};
    return {status == CommandStatus::Queued ? RequestStatus::Queued : RequestStatus::Rejected, status};
}
RequestResult dispatchGameplay(std::span<const uint8_t> packet, GameplayContext &context) {
    if (packet.empty() || clientPacketSize(packet) != packet.size())
        throw net::protocol::ProtocolError("Invalid gameplay request envelope");
    net::protocol::Reader in(packet);
    // Registration adds a real link to its named entry point. There is no
    // default stub that could hide a forgotten handler for a newly added packet.
    switch (ClientMessage(in.u8())) {
#define ROUTE(name) case ClientMessage::name: { const auto result = handlers::name(context, in); in.finish(); return result; }
#define ROUTE_Movement(name) ROUTE(name)
#define ROUTE_Combat(name) ROUTE(name)
#define ROUTE_Inventory(name) ROUTE(name)
#define ROUTE_Interaction(name) ROUTE(name)
#define ROUTE_Progression(name) ROUTE(name)
#define ROUTE_Social(name) ROUTE(name)
#define ROUTE_Lifecycle(name) case ClientMessage::name: break;
#define D2X_MESSAGE(id, name, length, domain, phase, support) ROUTE_##domain(name)
#include "client_messages.inc"
#undef D2X_MESSAGE
#undef ROUTE_Lifecycle
#undef ROUTE_Social
#undef ROUTE_Progression
#undef ROUTE_Interaction
#undef ROUTE_Inventory
#undef ROUTE_Combat
#undef ROUTE_Movement
#undef ROUTE
    }
    throw net::protocol::ProtocolError("Lifecycle message reached gameplay dispatcher");
}
}
