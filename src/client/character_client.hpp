#pragma once
#include "contracts/character.hpp"

namespace d2x {
class ICharacterClient {
  public:
    virtual ~ICharacterClient() = default;
    // Borrow only until the next read or destruction. UI copies a changed revision.
    virtual const CharacterView &read() const = 0;
    virtual void submit(CharacterIntent intent) = 0;
};
} // namespace d2x
