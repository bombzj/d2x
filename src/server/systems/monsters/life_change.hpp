#pragma once
#include "core/fixed.hpp"
#include "core/id.hpp"
#include "gameplay/combat/stats.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/combat/open_wounds.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "curse_control.hpp"
#include <optional>
#include <cstdint>
#include <vector>
#include <memory>
#include <utility>
namespace d2x {struct FrameEventPublication;}
namespace d2x::server::monsters {
struct LifeChange {
    EntityId actor;
    uint64_t expectedRevision{};
    Fixed8 life,maximum;
    std::optional<NativeUnitStats> intrinsic;
    bool changed{};
    bool cureColdPoison{}; // Coupled healer restoration, committed at the same revision.
    std::optional<bool> shatter{};
    std::optional<std::optional<PoisonStatus>> poison{};
    std::optional<std::vector<ItemAuraTimer>> itemAuraTimers{};
    std::optional<CurseControl> curseControl{};
    std::optional<uint64_t> controlCallbackEpoch{};
    bool stopMovement{};
    std::optional<uint64_t> hitDelay{};
    std::optional<AttractionControl> attraction{};
    std::optional<std::vector<AiResetTimer>> aiResetTimers{};
    std::optional<uint64_t> targetCallbackEpoch{};
    std::optional<bool> specialTargetNode{};
    std::optional<bool> detachedMinion{};
    std::optional<std::vector<EntityId>> nativeMinions{};
    bool tauntPostActions{},reselectAi{};
    std::optional<EntityId> tauntTarget{};
    std::optional<uint64_t> aiThinkAt{};
    std::shared_ptr<FrameEventPublication> periodicEvents{};
    std::optional<std::pair<uint64_t,uint64_t>> random{};
    std::optional<uint64_t> chilledUntil{},frozenUntil{},stunnedUntil{};
    bool restoreNeutral{};
    std::optional<std::optional<OpenWoundsApplication>> wound{};
    std::optional<bool> modeAlive{}; // A direct HP write need not perform a death mode.
};
}
