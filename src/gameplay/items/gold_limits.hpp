#pragma once
namespace d2x {
// D2Common UNITS_GetStashGoldLimit; no MPQ column provides this engine rule.
constexpr unsigned stashGoldLimit(unsigned level) {
    return 50000u * (level <= 30 ? level / 10u + 1u : level / 2u + 1u);
}
}
