#pragma once
#include "server/runtime/contracts.hpp"
#include <memory>
#include <optional>
namespace d2x::server::combat {struct NativeSkillContact;}
namespace d2x::server::effects {struct NativeAreaPulse;}
namespace d2x::server::skills {
struct AssassinKickExecution {
    enum class Phase { InitialDrain,NextSelect,NextAllocation,NextDrain,FlightTeleport,FlightAllocation,TailArea,Complete } phase{};
    std::shared_ptr<combat::NativeSkillContact> contact;
    std::optional<uint64_t> occurrence;
    std::shared_ptr<effects::NativeAreaPulse> area;
    int32_t copiedPhysical{},parameter{};
    bool startPrepared{},knockback{},parameterPrepared{},modeStats{};
    uint64_t nextEvent{};
};
}
