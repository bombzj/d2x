#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace d2x::net {
// UI text is UTF-8; the original game chat payload uses the system ANSI code
// page. This adapter belongs outside gameplay and never changes wire framing.
std::optional<std::string> encodeChatText(std::string_view utf8);
std::optional<std::string> decodeChatText(std::string_view bytes,uint8_t language,uint8_t localLanguage);
bool validChatInput(std::string_view utf8);
}
