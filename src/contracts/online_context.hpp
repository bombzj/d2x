#pragma once
#include <cstdint>
#include <optional>
namespace d2x {
// Local intent identity only. Never serialized as a native transaction/ACK ID.
struct OnlineIntentContext {
    uint64_t connectionGeneration{}, gameGeneration{}, areaGeneration{}, interactionGeneration{};
    std::optional<uint32_t> player, npc;
    bool operator==(const OnlineIntentContext &) const = default;
};
}
