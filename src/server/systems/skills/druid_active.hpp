#pragma once
#include "server/systems/effects/native_area.hpp"
#include "server/systems/missiles/requests.hpp"
#include <optional>
namespace d2x::server::skills {
struct DruidActiveExecution {
    effects::NativeAreaSpec area;
    std::shared_ptr<effects::NativeAreaPulse> pulse;
    std::optional<missiles::NativeSpawn> missile;
    std::optional<std::pair<uint64_t,uint64_t>> random;
    int nextParameter{};
    bool prepared{},rescheduled{},consumed{},finished{};
};
}
