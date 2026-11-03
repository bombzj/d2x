#pragma once
#include "server/game_commands.hpp"
#include "system_catalog.hpp"
#include "server/systems/inventory/request.hpp"
#include "server/systems/crafting/request.hpp"
#include "server/systems/skills/request.hpp"
#include "server/systems/death/request.hpp"
#include "server/systems/companions/request.hpp"
#include "server/systems/objects/request.hpp"
#include "server/systems/npc/request.hpp"
#include "server/systems/merchant/request.hpp"
#include "server/systems/quests/request.hpp"
#include "server/systems/progression/request.hpp"
#include "server/systems/travel/request.hpp"
#include "server/systems/social/request.hpp"
#include "server/systems/trade/request.hpp"

namespace d2x::server {
// Internal typed requests, after native decoding/identity binding in hosting.
// Neither this variant nor ActorContext is sent to a client.
using CommandPayload = std::variant<MovementCommand,
    inventory::Request,
    crafting::Request,
    skills::Request,
    death::Request,
    companions::Request,
    objects::Request,
    npc::Request,
    merchant::Request,
    quests::Request,
    progression::Request,
    travel::Request,
    social::Request,
    trade::Request>;
struct GameCommand {
    uint64_t sequence{};
    RegionId area{};
    uint64_t areaGeneration{};
    CommandPayload payload;
};
SystemId commandSystem(const CommandPayload &);
bool acceptsCommand(const CommandPayload &);
struct GameSystems;
CommandStatus commandStatus(DomainStatus);
CommandStatus dispatchCommand(const ActorContext &, const CommandPayload &, GameSystems &);
}
