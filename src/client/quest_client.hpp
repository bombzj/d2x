#pragma once
#include "contracts/quest.hpp"

namespace d2x {
class IQuestClient {
  public:
    virtual ~IQuestClient() = default;
    // Borrow only until the next read/destruction; log gestures are local UI.
    virtual const QuestView &read() const = 0;
};
} // namespace d2x
