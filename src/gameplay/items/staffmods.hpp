#pragma once
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
namespace d2x {
// D2Game sub_6FC52650: policy arithmetic only; caller supplies native skill/type eligibility.
std::vector<std::pair<int,int>> rollStaffmods(int itemLevel, int firstSkill, bool inferior,
    int imbueBias, const std::function<bool(int)> &eligible, uint64_t &random);
}
