#pragma once
#include "server/game_instance.hpp"
#include "world/generated_area.hpp"

namespace d2x {
struct WalkingGameContent {
    server::GameDefinition authority;
    GeneratedArea terrain;
};
// MPQ-facing composition, deliberately a different library from GameHost.
struct ClassicData;
WalkingGameContent prepareWalkingGame(Archives &, const ClassicData &, PersistentCharacter, uint64_t rulesFingerprint);
} // namespace d2x
