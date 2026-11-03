#pragma once
#include "gameplay/npc/intents.hpp"
#include "core/math.hpp"
#include <variant>

namespace d2x::server::npc {
struct StopForClick { EntityId target; Vec observed; };
using Intent = std::variant<TalkToNpc, EndNpcConversation,ConfirmNpcVoice,StopForClick>;
struct Request { Intent intent; };
}
