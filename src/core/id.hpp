#pragma once
#include "entity_id.hpp"
#include "native_unit_ids.hpp"
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace d2x {
// IDs are session-wide, never vector indices. Zero always means no entity.
class EntityIds;
class EntityIdPublication {
    friend class EntityIds;
    const EntityIds *owner_;
    uint64_t first_,next_;
    bool published_{};
    std::map<EntityId,NativeUnitType> types_;
    std::vector<NativeUnitIds::Request> requests_;
    std::optional<NativeUnitIds::Publication> native_;
    EntityIdPublication(const EntityIds &owner,uint64_t first,uint64_t next) : owner_(&owner),first_(first),next_(next) {}
  public:
    EntityIdPublication(const EntityIdPublication &)=delete;
    EntityIdPublication &operator=(const EntityIdPublication &)=delete;
    bool covers(EntityId id) const {return id.value>=first_ && id.value<next_;}
    uint64_t first() const {return first_;}
    uint64_t next() const {return next_;}
    void bind(EntityId id,NativeUnitType type);
};

class EntityIds {
    friend class EntityIdPublication;
    uint64_t next_ = 1;
    NativeUnitIds *native_{};

  public:
    using Publication = EntityIdPublication;
    explicit EntityIds(uint64_t next = 1,NativeUnitIds *native=nullptr) : next_(next),native_(native) {
        if (!next_) throw std::invalid_argument("Entity ID cursor cannot be zero");
    }
    uint64_t cursor() const { return next_; }
    const NativeUnitIds &nativeIdentities() const;
    bool hasNative(EntityId id,NativeUnitType type) const;
    std::optional<NativeUnitIds::Publication> prepareNative(std::span<const NativeUnitIds::Request> requests) const;
    bool validateNative(const std::optional<NativeUnitIds::Publication> &plan) const noexcept;
    void publishNative(std::optional<NativeUnitIds::Publication> &plan) noexcept;
    void advanceTo(uint64_t next);
    std::shared_ptr<Publication> preparePublication(size_t count) const;
    bool validate(const Publication &value) const;
    void publish(Publication &value) noexcept;
    EntityId allocate();
    EntityId allocate(NativeUnitType type);
};
} // namespace d2x
