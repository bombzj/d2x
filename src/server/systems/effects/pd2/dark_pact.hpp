#pragma once
#include "../native_area.hpp"
#include "gameplay/effects/values.hpp"
#include <optional>
#include <utility>
#include <vector>

namespace d2x::server::effects {
struct Pd2DarkPactPulse {
    std::vector<EntityId> targets;
    size_t next{};
    bool prepared{},paid{};
    struct Target {
        EntityId id;
        int count{},allowance{};
        std::optional<EffectHandle> consumed;
        bool withdrawn{},seedCommitted{};
        std::pair<uint64_t,uint64_t> seed;
        std::shared_ptr<NativeAreaPulse> damage;
    };
    std::optional<Target> target;
};
}
