#pragma once
#include "contracts/online_world.hpp"

namespace d2x {
inline bool retailObjectSpeechAcknowledged(uint16_t message) {
    // D2Client 7D389–7D3B1 arms the end-of-scroll packet for these readings.
    return message == 127 || message == 396 || message == 20002 || message == 20131 || message == 20169;
}
inline std::optional<uint16_t> retailNpcSpeechFollowup(uint16_t message) {
    // D2Client 1.13c 45FF0 / F42F8, the two Act I Cain rescue speeches.
    if (message == 123 || message == 125) return 126;
    return {};
}
inline std::optional<uint16_t> nextAutomaticNpcSpeech(const OnlineNpcConversation &conversation) {
    for (const auto &message : conversation.messages)
        if (message.menu == 0 && message.stringId != UINT16_MAX &&
            !conversation.acknowledged.contains(message.stringId)) return message.stringId;
    return {};
}
}
