#pragma once
#include "gameplay/skills/assassin_progressive.hpp"
#include <optional>

namespace d2x::server::combat {
struct ProgressiveContactExecution {
    enum class Phase { Select,Write,Program,Notify,Remove,Complete } phase{};
    int lastState{-1};AssassinCharge charge;
    std::vector<AssassinProgressiveStep> steps;size_t next{};
    uint32_t clientSeed{};bool mainDrained{},areaQueued{};
    std::vector<uint64_t> areaOccurrences;size_t areaNext{};
    struct Gather {bool clearPrepared{};std::vector<int> clearStates;size_t clearNext{};bool chargePrepared{},cobraPrepared{};int oldCount{},chargeCount{};};
    std::optional<Gather> gather;
};
}
