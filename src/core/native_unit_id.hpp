#pragma once
#include "entity_id.hpp"
#include <array>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <stdexcept>
#include <exception>
#include <set>

namespace d2x {
enum class NativeUnitType : uint8_t {Player,Monster,Object,Missile,Item,Tile};
struct NativeUnitId {
    NativeUnitType type{};
    uint32_t guid{};
    auto operator<=>(const NativeUnitId &) const = default;
};
class NativeUnitIds {
    std::array<uint64_t,6> next_{1,1,1,1,1,1};
    std::map<EntityId,NativeUnitId> forward_;
    std::map<NativeUnitId,EntityId> reverse_;
  public:
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
    std::optional<NativeUnitId> find(EntityId entity) const {
        const auto found=forward_.find(entity);
        return found==forward_.end()?std::nullopt:std::optional{found->second};
    }
    std::optional<EntityId> find(NativeUnitId native) const {
        const auto found=reverse_.find(native);
        return found==reverse_.end()?std::nullopt:std::optional{found->second};
    }
    uint32_t guid(EntityId entity) const {
        if(!entity) return 0;
        const auto native=find(entity);
        if(!native) throw std::logic_error("Missing native unit identity");
        return native->guid;
    }
    // The caller supplies domain/output references as well as live entities.
    // Removing a unit alone does not release GUIDs held by delayed effects.
    void retain(const std::set<EntityId> &entities) noexcept {
        for(auto entry=forward_.begin();entry!=forward_.end();) {
            if(entities.contains(entry->first)) {++entry;continue;}
            reverse_.erase(entry->second);entry=forward_.erase(entry);
        }
    }
    Publication prepare(std::span<const Request> requests) const {
        Publication plan;plan.owner_=this;plan.expected_=plan.next_=next_;
        for(const auto &request:requests) {
            const auto type=size_t(request.type);
            if(!request.entity || type>=next_.size()) throw std::invalid_argument("Invalid native identity request");
            auto previous=find(request.entity);
            if(const auto staged=plan.forward_.find(request.entity);staged!=plan.forward_.end()) previous=staged->second;
            if(previous) {
                if(previous->type!=request.type) throw std::invalid_argument("Native entity type changed");
                continue;
            }
            // SUNIT_AllocUnitData reserves -1 and wraps after the last -2.
            // A still-referenced old identity cannot safely be aliased by the
            // internal/protocol bijection; reject that collision explicitly.
            const NativeUnitId native{request.type,uint32_t(plan.next_[type])};
            if(reverse_.contains(native) || plan.reverse_.contains(native))
                throw std::overflow_error("Wrapped native GUID is still referenced");
            plan.next_[type]=plan.next_[type]==UINT32_MAX-1?1:plan.next_[type]+1;
            plan.forward_.emplace(request.entity,native);plan.reverse_.emplace(native,request.entity);
        }
        return plan;
    }
    bool validate(const Publication &plan) const noexcept {
        if(plan.owner_!=this) return false;
        if(plan.published_) return true;
        for(const auto &[entity,native]:plan.forward_)
            if(forward_.contains(entity) || reverse_.contains(native)) return false;
        for(size_t type=0;type<next_.size();++type) if(plan.next_[type]!=plan.expected_[type] &&
            next_[type]!=plan.expected_[type]) return false;
        return true;
    }
    void publish(Publication &plan) noexcept {
        if(!validate(plan)) std::terminate();
        if(plan.published_) return;
        forward_.merge(plan.forward_);reverse_.merge(plan.reverse_);
        if(!plan.forward_.empty() || !plan.reverse_.empty()) std::terminate();
        for(size_t type=0;type<next_.size();++type)
            if(plan.next_[type]!=plan.expected_[type]) next_[type]=plan.next_[type];
        plan.published_=true;
    }
};
}
