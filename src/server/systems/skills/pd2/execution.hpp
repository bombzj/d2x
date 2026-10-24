#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <memory>
#include <vector>

namespace d2x::server::skills {
struct Pd2VengeancePulse;
struct Pd2CastExecution {
    std::shared_ptr<Pd2VengeancePulse> vengeance;
    int chargeDistance{};bool chargeState{};
    struct Ravens {int count{},next{};std::optional<Vec> point;};
    std::optional<Ravens> ravens;
    struct Desecrate {
        Vec position;int count{},next{};std::array<int,25> pool{};
        std::optional<int> definition;EntityId corpse;bool cloud{};
    };
    std::optional<Desecrate> desecrate;
    struct HolyLight {
        std::vector<EntityId> targets;size_t next{};
        std::array<int32_t,2> healing{},damage{};
        std::optional<int32_t> amount;
        bool hostile{},overlayPublished{};
    };
    std::optional<HolyLight> holyLight;
};
}
