#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include "gameplay/events/frame_queue.hpp"
#include "server/runtime/contracts.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <memory>
#include <vector>
#include <utility>

namespace d2x::server::missiles {class NativeBirthPublication;}
namespace d2x::server::skills {
struct Pd2VengeancePulse;
struct Pd2CastExecution {
    std::optional<FrameEventQueue::PreparedInsertion> castItemTimer;
    std::shared_ptr<Pd2VengeancePulse> vengeance;
    std::shared_ptr<missiles::NativeBirthPublication> royalFire;
    std::shared_ptr<missiles::NativeBirthPublication> teleportMissiles;
    struct BossProjectiles {
        int definition{-1};
        std::vector<std::pair<Vec,Vec>> points;
        std::shared_ptr<missiles::NativeBirthPublication> birth;
    };
    std::optional<BossProjectiles> bossProjectiles;
    struct BossSummon {
        struct Group {std::vector<int> definitions;int count{};Vec center;int spread{},failureLimit{40};bool search{true};uint64_t aiDelay{};};
        std::vector<Group> groups;size_t group{};int count{},failures{},definition{-1};bool sharedFailureBudget{};
        std::optional<PointTarget> point;bool positionPrepared{};
    };
    std::optional<BossSummon> bossSummon;
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
