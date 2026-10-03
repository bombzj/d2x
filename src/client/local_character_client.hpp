#pragma once
#include "client/character_client.hpp"

namespace d2x {
class GameSession;
class LocalCharacterClient final : public ICharacterClient {
    GameSession &session_;
    mutable CharacterView cached_;
  public:
    explicit LocalCharacterClient(GameSession &session) : session_(session) {}
    const CharacterView &read() const override;
    void submit(CharacterIntent intent) override;
};
} // namespace d2x
