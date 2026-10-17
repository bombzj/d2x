#pragma once
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <memory>
#include <exception>
#include <cstddef>

namespace d2x {
// IDs are session-wide, never vector indices. Zero always means no entity.
struct EntityId {
    uint64_t value = 0;
    explicit operator bool() const { return value != 0; }
    auto operator<=>(const EntityId &) const = default;
};
class EntityIds {
    uint64_t next_ = 1;

  public:
    class Publication {
        friend class EntityIds;
        const EntityIds *owner_;
        uint64_t first_,next_;
        bool published_{};
        Publication(const EntityIds &owner,uint64_t first,uint64_t next) : owner_(&owner),first_(first),next_(next) {}
      public:
        Publication(const Publication &)=delete;
        Publication &operator=(const Publication &)=delete;
        bool covers(EntityId id) const {return id.value>=first_ && id.value<next_;}
        uint64_t first() const {return first_;}
        uint64_t next() const {return next_;}
    };
    explicit EntityIds(uint64_t next = 1) : next_(next) {
        if (!next_) throw std::invalid_argument("Entity ID cursor cannot be zero");
    }
    uint64_t cursor() const { return next_; }
    std::shared_ptr<Publication> preparePublication(size_t count) const {
        if(count>std::numeric_limits<uint64_t>::max()-next_)
            throw std::overflow_error("Entity ID publication exhausted");
        return std::shared_ptr<Publication>(new Publication(*this,next_,next_+count));
    }
    bool validate(const Publication &value) const {
        return value.owner_==this && next_==(value.published_?value.next_:value.first_);
    }
    void publish(Publication &value) noexcept {
        if(!validate(value)) std::terminate();
        next_=value.next_;value.published_=true;
    }
    EntityId allocate() {
        if (next_ == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Entity ID space exhausted");
        return {next_++};
    }
};
} // namespace d2x
