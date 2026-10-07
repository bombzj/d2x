#pragma once
#include "server/game_instance.hpp"
#include <memory>
#include <vector>

namespace d2x {
// No window, network, archive or storage dependencies. Call from one host
// scheduler thread; a future transport posts commands onto that thread.
class GameHost {
    struct Slot {
        uint64_t generation{};
        std::unique_ptr<server::GameInstance> game;
        double accumulator{};
        bool paused{};
        bool resumed{};
        std::map<PlayerId, std::shared_ptr<const PlayerSnapshot>> snapshots;
    };
    std::vector<Slot> slots_;
    uint64_t nextGeneration_ = 1;
    Slot *find(GameHandle);
    const Slot *find(GameHandle) const;
    void publish(size_t slot);
  public:
    PlayerBinding create(server::GameDefinition);
    bool destroy(GameHandle);
    bool pause(GameHandle, bool);
    void advance(double seconds);
    // Explicit authority stepping while paused; no accumulated wall-clock debt.
    bool step(GameHandle, uint32_t frames);
    CommandStatus submit(PlayerBinding, server::GameCommand);
    std::shared_ptr<const PlayerSnapshot> read(PlayerBinding) const;
    // Host storage boundary only; never exposed on a client transport.
    std::optional<PersistentCharacter> exportCharacter(PlayerBinding) const;
    std::optional<server::SystemSteps> systemSteps(GameHandle) const;
    // Reliable game facts stay on the host side; native encoding must complete
    // before acknowledging. A failed encoder cannot silently discard output.
    std::optional<std::vector<server::EventBatch>> pendingEvents(GameHandle) const;
    bool acknowledgeEvents(GameHandle, uint64_t sequence);
    // Content worker results are installed on the scheduler thread. The worker
    // may prepare values but must not retain a mutable game/area reference.
    std::optional<std::vector<server::world::PrepareArea>> pendingAreas(GameHandle) const;
    server::DomainResult<> installArea(GameHandle, server::world::PreparedArea);
};
} // namespace d2x
