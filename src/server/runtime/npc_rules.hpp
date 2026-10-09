#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/quest/id.hpp"
#include "world/navigation.hpp"
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
    std::set<uint16_t> questMessages;
    std::map<std::pair<QuestId,std::string>,uint16_t> questSpeeches;
    int questInitFunction{};
    bool interactable=true;
    int walkVelocity{};
    MovementCollisionRule movement;
};
struct AreaNpc { EntityId id; Vec position; NpcRule rule; bool hidden{},moving{}; Vec destination{}; };
}
