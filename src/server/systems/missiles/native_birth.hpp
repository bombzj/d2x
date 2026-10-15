#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/events.hpp"
#include <cstddef>
#include <memory>
#include <vector>

namespace d2x::server::missiles {
class System;
// A constructor batch owns all allocations until its enclosing publication is
// accepted. Actor births may reserve a contiguous prefix of the same ID range.
class NativeBirthPublication {
    friend class System;
    struct Data;
    std::unique_ptr<Data> data_;
    explicit NativeBirthPublication(std::unique_ptr<Data>);
  public:
    ~NativeBirthPublication();
    NativeBirthPublication(const NativeBirthPublication &)=delete;
    NativeBirthPublication &operator=(const NativeBirthPublication &)=delete;
    bool validate(size_t actorPrefix=0) const;
    void publish() noexcept;
    const std::vector<DomainFact> &facts() const;
    EntityId owner() const;
    EntityId first() const;
    bool changesOwnerSeed() const;
};
}
