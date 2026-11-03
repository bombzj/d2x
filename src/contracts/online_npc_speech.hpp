#pragma once
#include "contracts/online_world.hpp"

namespace d2x {
inline std::optional<uint16_t> nextAutomaticNpcSpeech(const OnlineNpcConversation &conversation) {
    for (const auto &message : conversation.messages)
        if (message.menu == 0 && !conversation.acknowledged.contains(message.stringId)) return message.stringId;
    return {};
}
}
