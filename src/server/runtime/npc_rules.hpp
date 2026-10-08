#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>
namespace d2x::server {
struct NpcRule {
    std::string code, introduction;
    int nativeClass{}, size{};
    bool vendor{}, repair{}, identify{}, heal{}, gamble{};
    std::map<std::string, uint16_t, std::less<>> introductions;
    std::vector<uint16_t> gossip;
    std::set<uint16_t> denMessages;
};
struct AreaNpc { EntityId id; Vec position; NpcRule rule; };
}
