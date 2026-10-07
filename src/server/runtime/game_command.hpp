#pragma once
#include "system_catalog.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/crafting/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/death/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/merchant/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/trade/system.hpp"

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
CommandStatus dispatchCommand(const ActorContext &, const CommandPayload &, GameSystems &);
}
