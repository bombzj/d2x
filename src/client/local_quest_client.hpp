#pragma once
#include "client/quest_client.hpp"

namespace d2x {
class GameSession;
class LocalQuestClient final : public IQuestClient {
    const GameSession &session_;
    mutable QuestView cached_;
  public:
    explicit LocalQuestClient(const GameSession &session) : session_(session) {}
    const QuestView &read() const override;
};
} // namespace d2x
