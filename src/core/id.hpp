#pragma once
#include "entity_id.hpp"
#include "native_unit_id.hpp"
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <memory>
#include <exception>
#include <cstddef>

namespace d2x {
// IDs are session-wide, never vector indices. Zero always means no entity.
class EntityIds {
    uint64_t next_ = 1;
    NativeUnitIds *native_{};

  public:
    class Publication {
        friend class EntityIds;
        const EntityIds *owner_;
        uint64_t first_,next_;
        bool published_{};
        std::map<EntityId,NativeUnitType> types_;
        std::vector<NativeUnitIds::Request> requests_;
        std::optional<NativeUnitIds::Publication> native_;
        Publication(const EntityIds &owner,uint64_t first,uint64_t next) : owner_(&owner),first_(first),next_(next) {}
      public:
        Publication(const Publication &)=delete;
        Publication &operator=(const Publication &)=delete;
        bool covers(EntityId id) const {return id.value>=first_ && id.value<next_;}
        uint64_t first() const {return first_;}
        uint64_t next() const {return next_;}
        void bind(EntityId id,NativeUnitType type) {
            if(!id || id.value>=next_) throw std::invalid_argument("Native identity outside publication");
            if(owner_->native_) if(const auto existing=owner_->native_->find(id)) {
                if(existing->type!=type) throw std::invalid_argument("Native entity type changed");
                return;
            }
            if(const auto found=types_.find(id);found!=types_.end()) {
                if(found->second!=type) throw std::invalid_argument("Conflicting native identity type");
                return;
            }
            if(published_) throw std::logic_error("Native identity added after publication");
            types_.emplace(id,type);
            requests_.push_back({id,type});
            if(owner_->native_) native_=owner_->native_->prepare(requests_);
        }
    };
    explicit EntityIds(uint64_t next = 1,NativeUnitIds *native=nullptr) : next_(next),native_(native) {
        if (!next_) throw std::invalid_argument("Entity ID cursor cannot be zero");
    }
    uint64_t cursor() const { return next_; }
    const NativeUnitIds &nativeIdentities() const {
        if(!native_) throw std::logic_error("Missing native identity registry");
        return *native_;
    }
    bool hasNative(EntityId id,NativeUnitType type) const {
        if(!native_) return true;
        const auto found=native_->find(id);
        if(found && found->type!=type) throw std::invalid_argument("Native entity type changed");
        return found.has_value();
    }
    std::optional<NativeUnitIds::Publication> prepareNative(std::span<const NativeUnitIds::Request> requests) const {
        if(!native_) return {};
        return native_->prepare(requests);
    }
    bool validateNative(const std::optional<NativeUnitIds::Publication> &plan) const noexcept {
        return !plan || (native_ && native_->validate(*plan));
    }
    void publishNative(std::optional<NativeUnitIds::Publication> &plan) noexcept {
        if(!validateNative(plan)) std::terminate();
        if(plan) native_->publish(*plan);
    }
    void advanceTo(uint64_t next) {
        if(next<next_ || !next) throw std::invalid_argument("Entity cursor moved backwards");
        next_=next;
    }
    std::shared_ptr<Publication> preparePublication(size_t count) const {
        if(count>std::numeric_limits<uint64_t>::max()-next_)
            throw std::overflow_error("Entity ID publication exhausted");
        return std::shared_ptr<Publication>(new Publication(*this,next_,next_+count));
    }
    bool validate(const Publication &value) const {
        return value.owner_==this && next_==(value.published_?value.next_:value.first_) &&
            (!value.native_ || native_->validate(*value.native_));
    }
    void publish(Publication &value) noexcept {
        if(!validate(value)) std::terminate();
        if(value.native_) native_->publish(*value.native_);
        next_=value.next_;value.published_=true;
    }
    EntityId allocate() {
        if (next_ == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Entity ID space exhausted");
        return {next_++};
    }
    EntityId allocate(NativeUnitType type) {
        if(next_==UINT64_MAX) throw std::overflow_error("Entity ID space exhausted");
        const EntityId entity{next_};
        const std::array requests{NativeUnitIds::Request{entity,type}};
        auto plan=prepareNative(requests);
        publishNative(plan);++next_;return entity;
    }
};
} // namespace d2x
