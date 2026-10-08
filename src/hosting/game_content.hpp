#pragma once
#include "server/game_instance.hpp"
#include "world/generated_area.hpp"

namespace d2x {
struct ClassicData;
struct PreparedWorldArea { server::AreaDefinition authority; GeneratedArea terrain; };
PreparedWorldArea prepareWorldArea(Archives &, const ClassicData &, AreaGenerationRequest);
server::GameDefinition prepareJoiningCharacter(const ClassicData &, PersistentCharacter, server::GameSettings, RegionId town, uint64_t nextEntity, uint64_t fingerprint, std::shared_ptr<const ItemCatalog>);
struct WalkingGameContent {
    server::GameDefinition authority;
    GeneratedArea terrain;
};
// MPQ-facing composition, deliberately a different library from GameHost.
WalkingGameContent prepareWalkingGame(Archives &, const ClassicData &, PersistentCharacter, uint64_t rulesFingerprint, std::shared_ptr<const ItemCatalog>);
} // namespace d2x
