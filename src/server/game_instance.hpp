#pragma once
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/game_command.hpp"
#include "server/runtime/game_systems.hpp"
#include "server/runtime/simulation.hpp"
#include "server/runtime/prepared_rules.hpp"
#include <optional>

namespace d2x::server {
struct GameDefinition {
    GameSettings settings;
    AreaDefinition area;
    CharacterDefinition character;
    PersistentCharacter persistent;
    uint64_t rulesFingerprint{};
    PreparedRules rules;
};
// Composition and ordering only; domain implementations live beside this class.
class GameInstance {
    const GameSettings settings_;
    AreaStore areas_;
    EntityIds entities_;
    uint64_t random_, tick_{}, revision_{1};
    uint64_t rulesFingerprint_{};
    const PreparedRules rules_;
    PlayerStore players_;
    EventOutbox events_;
    GameSystems systems_;
    Simulation simulation_;
    struct Pending { PlayerId player; GameCommand command; };
    std::deque<Pending> commands_;
  public:
    explicit GameInstance(GameDefinition);
    GameInstance(const GameInstance &) = delete;
    GameInstance &operator=(const GameInstance &) = delete;
    CommandStatus enqueue(PlayerId, GameCommand);
    void step();
    void suspend();
    std::optional<PlayerSnapshot> snapshot(PlayerId) const;
    std::optional<PersistentCharacter> exportCharacter(PlayerId) const;
    std::vector<PlayerId> participants() const;
    const SystemSteps &systemSteps() const { return simulation_.lastSteps(); }
    const std::deque<EventBatch> &pendingEvents() const { return events_.pending(); }
    bool acknowledgeEvents(uint64_t sequence) { return events_.acknowledge(sequence); }
    const std::vector<world::PrepareArea> &pendingAreas() const { return systems_.world.read().preparation; }
    DomainResult<> installArea(world::PreparedArea);
};
} // namespace d2x::server
