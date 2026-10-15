#pragma once
#include <cstdint>
#include <memory>
#include <optional>
namespace d2x::server::combat {struct NativeSkillContact;}
namespace d2x::server::skills {
struct DruidMeleeExecution {
    enum class Phase { Initial,RageBuff,RabiesInfection,RabiesFinal,Complete } phase{Phase::Initial};
    std::shared_ptr<combat::NativeSkillContact> contact,accuracy;
    std::optional<uint64_t> occurrence;
    int infectionLength{};
    bool startPrepared{},initialHit{},infectionPosted{},carrierComplete{};
};
}
