#pragma once
#include <string_view>
namespace d2x {
struct DeathLootContext {
    std::string_view characterClass;
    bool andarielBonus = false;
};
} // namespace d2x
