#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <memory>
#include <optional>

namespace d2x::server::monsters {
class System;
// Prepared native room search and actual actor birth share one publication.
// A failed native placement may still consume the room's private random.
class NativeBirthPublication {
    friend class System;
    struct Data;
    std::unique_ptr<Data> data_;
    explicit NativeBirthPublication(std::unique_ptr<Data>);
  public:
    ~NativeBirthPublication();
    NativeBirthPublication(const NativeBirthPublication &)=delete;
    NativeBirthPublication &operator=(const NativeBirthPublication &)=delete;
    bool validate() const;
    void publish() noexcept;
    EntityId actor() const;
    std::optional<Vec> position() const;
};
}
