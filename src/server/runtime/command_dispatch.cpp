#include "game_command.hpp"
#include "game_systems.hpp"
#include <stdexcept>

namespace d2x::server {
namespace {
template<class T> struct Target;
template<> struct Target<MovementCommand> { static constexpr auto id = SystemId::Movement; };
template<> struct Target<inventory::Request> { static constexpr auto id = SystemId::Inventory; };
template<> struct Target<crafting::Request> { static constexpr auto id = SystemId::Crafting; };
template<> struct Target<skills::Request> { static constexpr auto id = SystemId::Skills; };
template<> struct Target<death::Request> { static constexpr auto id = SystemId::Death; };
template<> struct Target<companions::Request> { static constexpr auto id = SystemId::Companions; };
template<> struct Target<objects::Request> { static constexpr auto id = SystemId::Objects; };
template<> struct Target<npc::Request> { static constexpr auto id = SystemId::Npc; };
template<> struct Target<merchant::Request> { static constexpr auto id = SystemId::Merchant; };
template<> struct Target<quests::Request> { static constexpr auto id = SystemId::Quests; };
template<> struct Target<progression::Request> { static constexpr auto id = SystemId::Progression; };
template<> struct Target<travel::Request> { static constexpr auto id = SystemId::Travel; };
template<> struct Target<social::Request> { static constexpr auto id = SystemId::Social; };
template<> struct Target<trade::Request> { static constexpr auto id = SystemId::Trade; };
CommandStatus status(DomainStatus result) {
    switch (result) {
    case DomainStatus::Applied: return CommandStatus::Applied;
    case DomainStatus::NotImplemented: return CommandStatus::NotImplemented;
    case DomainStatus::InvalidActor: return CommandStatus::InvalidBinding;
    case DomainStatus::Stale: return CommandStatus::Stale;
    case DomainStatus::InvalidRequest: return CommandStatus::InvalidRequest;
    case DomainStatus::Unavailable: return CommandStatus::Unavailable;
    case DomainStatus::Conflict: return CommandStatus::Conflict;
    case DomainStatus::Capacity: return CommandStatus::QueueFull;
    }
    throw std::logic_error("Unknown domain result");
}
struct Dispatch {
    const ActorContext &actor;
    GameSystems &systems;
    CommandStatus operator()(const MovementCommand &request) const {
        if (const auto crossing = systems.travel.walk(actor, request)) return *crossing;
        return systems.movement.execute(actor, request);
    }
    CommandStatus operator()(const inventory::Request &request) const {
        return status(systems.inventory.execute(actor, request).status);
    }
    CommandStatus operator()(const crafting::Request &request) const {
        return status(systems.crafting.execute(actor, request).status);
    }
    CommandStatus operator()(const skills::Request &request) const {
        return status(systems.skills.execute(actor, request).status);
    }
    CommandStatus operator()(const death::Request &request) const {
        return status(systems.death.execute(actor, request).status);
    }
    CommandStatus operator()(const companions::Request &request) const {
        return status(systems.companions.execute(actor, request).status);
    }
    CommandStatus operator()(const objects::Request &request) const {
        return status(systems.objects.execute(actor, request).status);
    }
    CommandStatus operator()(const npc::Request &request) const {
        return status(systems.npc.execute(actor, request).status);
    }
    CommandStatus operator()(const merchant::Request &request) const {
        return status(systems.merchant.execute(actor, request).status);
    }
    CommandStatus operator()(const quests::Request &request) const {
        return status(systems.quests.execute(actor, request).status);
    }
    CommandStatus operator()(const progression::Request &request) const {
        return status(systems.progression.execute(actor, request).status);
    }
    CommandStatus operator()(const travel::Request &request) const {
        return status(systems.travel.execute(actor, request).status);
    }
    CommandStatus operator()(const social::Request &request) const {
        return status(systems.social.execute(actor, request).status);
    }
    CommandStatus operator()(const trade::Request &request) const {
        return status(systems.trade.execute(actor, request).status);
    }
};
}
SystemId commandSystem(const CommandPayload &payload) {
    return std::visit([]<class T>(const T &) { return Target<T>::id; }, payload);
}
bool acceptsCommand(const CommandPayload &payload) {
    if (const auto *request = std::get_if<inventory::Request>(&payload)) return inventory::supports(*request);
    if (const auto *request = std::get_if<skills::Request>(&payload)) return request->action != skills::Action::Bind;
    if (std::holds_alternative<death::Request>(payload)) return false;
    const auto id = commandSystem(payload);
    for (const auto &entry : systemCatalog())
        if (entry.id == id) return entry.scope != SystemScope::Scaffold;
    throw std::logic_error("Command domain missing from system catalog");
}
CommandStatus dispatchCommand(const ActorContext &actor, const CommandPayload &payload, GameSystems &systems) {
    return std::visit(Dispatch{actor, systems}, payload);
}
}
