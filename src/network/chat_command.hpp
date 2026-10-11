#pragma once
#include <string>
#include <string_view>

namespace d2x::net {
enum class ChatRoute { Room, Overhead, Whisper, BattleNet, NoPickup, Players, Invalid };
struct ChatCommand {
    ChatRoute route{ChatRoute::Room};
    std::string text, receiver, error;
    unsigned players{};
};
// Parse client syntax before selecting a transport. Unknown slash commands are
// Battle.net commands; a recognized but invalid local command never falls through.
ChatCommand parseChatCommand(std::string_view);
}
