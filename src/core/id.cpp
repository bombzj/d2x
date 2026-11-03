#include <limits>
#include "id.hpp"
#include <exception>
#include <stdexcept>

namespace d2x {
void EntityIdPublication::bind(EntityId id,NativeUnitType type) {
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

const NativeUnitIds &EntityIds::nativeIdentities() const {
    if(!native_) throw std::logic_error("Missing native identity registry");
    return *native_;
}

bool EntityIds::hasNative(EntityId id,NativeUnitType type) const {
    if(!native_) return true;
    const auto found=native_->find(id);
    if(found && found->type!=type) throw std::invalid_argument("Native entity type changed");
    return found.has_value();
}

std::optional<NativeUnitIds::Publication> EntityIds::prepareNative(std::span<const NativeUnitIds::Request> requests) const {
    if(!native_) return {};
    return native_->prepare(requests);
}

bool EntityIds::validateNative(const std::optional<NativeUnitIds::Publication> &plan) const noexcept {
    return !plan || (native_ && native_->validate(*plan));
}

void EntityIds::publishNative(std::optional<NativeUnitIds::Publication> &plan) noexcept {
    if(!validateNative(plan)) std::terminate();
    if(plan) native_->publish(*plan);
}

void EntityIds::advanceTo(uint64_t next) {
    if(next<next_ || !next) throw std::invalid_argument("Entity cursor moved backwards");
    next_=next;
}

std::shared_ptr<EntityIds::Publication> EntityIds::preparePublication(size_t count) const {
    if(count>std::numeric_limits<uint64_t>::max()-next_)
        throw std::overflow_error("Entity ID publication exhausted");
    return std::shared_ptr<Publication>(new Publication(*this,next_,next_+count));
}

bool EntityIds::validate(const Publication &value) const {
    return value.owner_==this && next_==(value.published_?value.next_:value.first_) &&
        (!value.native_ || native_->validate(*value.native_));
}

void EntityIds::publish(Publication &value) noexcept {
    if(!validate(value)) std::terminate();
    if(value.native_) native_->publish(*value.native_);
    next_=value.next_;value.published_=true;
}

EntityId EntityIds::allocate() {
    if (next_ == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Entity ID space exhausted");
    return {next_++};
}

EntityId EntityIds::allocate(NativeUnitType type) {
    if(next_==UINT64_MAX) throw std::overflow_error("Entity ID space exhausted");
    const EntityId entity{next_};
    const std::array requests{NativeUnitIds::Request{entity,type}};
    auto plan=prepareNative(requests);
    publishNative(plan);++next_;return entity;
}
}
