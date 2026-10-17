#pragma once
#include <cstdint>
#include <memory>
#include <optional>
namespace d2x::server::combat {struct NativeSkillContact;}
namespace d2x::server::skills {
struct WeaponMeleeExecution {
    std::shared_ptr<combat::NativeSkillContact> contact;
    std::shared_ptr<WeaponMeleeExecution> following;
    std::optional<uint64_t> occurrence;
    bool allocated{},postAllocated{},recoilComplete{},drained{};
    std::optional<bool> conversion;
    int conversionFrames{};
};
}
