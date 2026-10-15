#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include <vector>
#include <memory>
#include <optional>
namespace d2x::server::effects {
struct NativeAreaSpec {
    EntityId source;RegionId area;Vec center;
    int radius{},skill{},rank{};uint32_t filter{},hitFlags{};uint16_t resultFlags{};
    MissileHitDamage damage;
};
struct NativeAreaPulse {
    NativeAreaSpec spec;std::vector<EntityId> targets;size_t next{};
    std::optional<uint64_t> occurrence;bool prepared{},complete{};
};
}
