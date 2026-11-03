#pragma once
#include "entity_id.hpp"
#include "native_unit_id.hpp"
#include <array>
#include <map>
#include <optional>
#include <span>
#include <set>

namespace d2x {
class NativeUnitIds {
    std::array<uint64_t,6> next_{1,1,1,1,1,1};
    std::map<EntityId,NativeUnitId> forward_;
    std::map<NativeUnitId,EntityId> reverse_;
  public:
    const auto &entries() const {return forward_;}
    const auto &next() const {return next_;}
    struct Request {EntityId entity;NativeUnitType type;};
    class Publication {
        friend class NativeUnitIds;
        const NativeUnitIds *owner_{};
        std::array<uint64_t,6> expected_{};
        std::array<uint64_t,6> next_{};
        std::map<EntityId,NativeUnitId> forward_;
        std::map<NativeUnitId,EntityId> reverse_;
        bool published_{};
      public:
        Publication()=default;
        Publication(const Publication &)=delete;
        Publication &operator=(const Publication &)=delete;
        Publication(Publication &&)=default;
        Publication &operator=(Publication &&)=default;
    };
    std::optional<NativeUnitId> find(EntityId entity) const;
    std::optional<EntityId> find(NativeUnitId native) const;
    uint32_t guid(EntityId entity) const;
    // The caller supplies domain/output references as well as live entities.
    // Removing a unit alone does not release GUIDs held by delayed effects.
    void retain(const std::set<EntityId> &entities) noexcept;
    Publication prepare(std::span<const Request> requests) const;
    bool validate(const Publication &plan) const noexcept;
    void publish(Publication &plan) noexcept;
};
}
