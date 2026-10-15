#include "frame_queue.hpp"
#include "gameplay/rules/event_frame.hpp"
#include <limits>
#include <stdexcept>
#include <exception>
#include <utility>
#include <algorithm>

namespace d2x {
uint64_t FrameEventQueue::reserveIdentity() {
    if(next_==UINT64_MAX) throw std::overflow_error("Native timer identity exhausted");
    return next_++;
}
FrameEventQueueObservation FrameEventQueue::observe(size_t limit) const {
    FrameEventQueueObservation result{nodes_.size(),phase_,frame_,executing_,cursor_,{}};
    result.events.reserve(std::min(limit,nodes_.size()));
    for(const auto &[handle,node]:nodes_) {
        (void)handle;if(result.events.size()==limit) break;
        result.events.push_back({node.event,node.executing,node.cancelled});
    }
    return result;
}
size_t FrameEventQueue::queueIndex(FrameEventUnit type) {
    switch(type) {
    case FrameEventUnit::Player:case FrameEventUnit::Game:return 0;
    case FrameEventUnit::Monster:return 1;
    case FrameEventUnit::Missile:return 2;
    case FrameEventUnit::Object:return 3;
    case FrameEventUnit::Item:return 4;
    }
    throw std::invalid_argument("Unsupported native timer unit type");
}
FrameEventQueue::Chain &FrameEventQueue::chain(const FrameEvent &event) {
    auto &queue=queues_[queueIndex(event.owner.type)];
    return event.continuous?queue.continuous:queue.timed[size_t(event.due%64)];
}
const FrameEventQueue::Chain &FrameEventQueue::chain(const FrameEvent &event) const {
    const auto &queue=queues_[queueIndex(event.owner.type)];
    return event.continuous?queue.continuous:queue.timed[size_t(event.due%64)];
}
FrameEventHandle FrameEventQueue::insert(FrameEventOwner owner,FrameEventType type,int32_t nativeFrame,uint64_t now,
    uint32_t handler,int32_t customId,int32_t customParameter) {
    queueIndex(owner.type);
    if(uint8_t(type)>=15) throw std::invalid_argument("Unsupported native timer event type");
    if(owner.type!=FrameEventUnit::Game && !owner.unit) throw std::invalid_argument("Native timer has no unit identity");
    if(frame_ && now<*frame_) throw std::logic_error("Native timer inserted from an earlier frame");
    const auto deadline=nativeEventDeadline(now,nativeFrame);
    return insertPrepared({{},owner,type,deadline.due,deadline.continuous,deadline.continuous?0:handler,customId,customParameter});
}
FrameEventHandle FrameEventQueue::insertPrepared(FrameEvent event) {
    return publish(prepareInsertion(std::move(event)));
}
FrameEventQueue::PreparedInsertion FrameEventQueue::prepareInsertion(FrameEvent event) {
    queueIndex(event.owner.type);
    if(uint8_t(event.type)>=15) throw std::invalid_argument("Unsupported native timer event type");
    if(event.owner.type!=FrameEventUnit::Game && !event.owner.unit) throw std::invalid_argument("Native timer has no unit identity");
    if(frame_ && !event.continuous && event.due<=*frame_) throw std::logic_error("Prepared native timer is already due");
    if(next_==UINT64_MAX) throw std::overflow_error("Native timer identity exhausted");
    event.handle={next_};
    if(event.continuous) event.handler=0;
    PreparedInsertion plan;plan.queue_=this;
    plan.owners_.emplace(event.owner,FrameEventHandle{});
    plan.nodes_.emplace(event.handle,Node{event});
    ++next_;
    return plan;
}
FrameEventHandle FrameEventQueue::publish(PreparedInsertion &&plan) noexcept {
    if(plan.queue_!=this || plan.nodes_.size()!=1 || plan.owners_.size()!=1) std::terminate();
    const auto event=plan.nodes_.begin()->second.event;
    if(nodes_.contains(event.handle)) std::terminate();
    if(!owners_.contains(event.owner)) owners_.insert(plan.owners_.extract(plan.owners_.begin()));
    nodes_.insert(plan.nodes_.extract(plan.nodes_.begin()));plan.queue_=nullptr;
    auto &node=nodes_.at(event.handle);auto &ownerHead=owners_.at(event.owner);
    // SUNIT's timer list is newest first, independently of the bucket list.
    node.unitNext=ownerHead;
    if(node.unitNext) nodes_.at(node.unitNext).unitPrevious=event.handle;
    ownerHead=event.handle;
    auto &bucket=chain(event);
    if(event.continuous) {
        node.next=bucket.head;
        if(node.next) nodes_.at(node.next).previous=event.handle;
        else bucket.tail=event.handle;
        bucket.head=event.handle;
    } else {
        node.previous=bucket.tail;
        if(node.previous) nodes_.at(node.previous).next=event.handle;
        else bucket.head=event.handle;
        bucket.tail=event.handle;
    }
    return event.handle;
}
FrameEventQueue::PreparedEdit FrameEventQueue::prepareEdit(FrameEventOwner owner,FrameEventType type,const std::vector<FrameEvent> &localEvents,std::optional<uint32_t> handler) {
    PreparedEdit plan;plan.owner=owner;plan.type=type;plan.handler=handler;
    const auto existing=events(owner);
    const auto same=[](const FrameEvent &published,const FrameEvent &local) {
        return published.payload==local.handle.value && published.due==local.due && published.continuous==local.continuous &&
            published.handler==local.handler && published.customId==local.customId && published.customParameter==local.customParameter;
    };
    for(const auto &event:existing) if(event.type==type && (!handler || event.handler==*handler)) {
        plan.expected.push_back(event.handle);
        if(std::none_of(localEvents.begin(),localEvents.end(),[&](const auto &local){return same(event,local);})) plan.removed.push_back(event.handle);
    }
    // A local unit list is newest first. Commit new timers in creation order,
    // preserving timed tail insertion and continuous head insertion.
    for(auto it=localEvents.rbegin();it!=localEvents.rend();++it)
        if(std::none_of(existing.begin(),existing.end(),[&](const auto &event){return event.type==type && same(event,*it);})) {
            auto event=*it;event.owner=owner;event.type=type;event.payload=it->handle.value;
            plan.added.push_back(prepareInsertion(event));
        }
    return plan;
}
bool FrameEventQueue::validate(const PreparedEdit &plan) const {
    const auto found=owners_.find(plan.owner);
    auto handle=found==owners_.end()?FrameEventHandle{}:found->second;size_t index=0;
    for(;handle;handle=nodes_.at(handle).unitNext) {
        const auto &node=nodes_.at(handle);
        if(node.event.type!=plan.type || (plan.handler && node.event.handler!=*plan.handler)) continue;
        if(index==plan.expected.size() || plan.expected[index++]!=handle) return false;
    }
    return index==plan.expected.size();
}
void FrameEventQueue::publish(PreparedEdit &&plan) noexcept {
    for(const auto handle:plan.removed) cancel(handle);
    for(auto &insertion:plan.added) publish(std::move(insertion));
}
const FrameEvent *FrameEventQueue::find(FrameEventHandle handle) const {
    const auto found=nodes_.find(handle);return found==nodes_.end()?nullptr:&found->second.event;
}
void FrameEventQueue::unlink(FrameEventHandle handle) {
    const auto found=nodes_.find(handle);if(found==nodes_.end()) return;
    const auto node=found->second;
    if(node.executing) throw std::logic_error("Native timer unlinked inside its callback");
    if(cursor_==handle) cursor_=node.next;
    auto &bucket=chain(node.event);
    if(node.previous) nodes_.at(node.previous).next=node.next;else bucket.head=node.next;
    if(node.next) nodes_.at(node.next).previous=node.previous;else bucket.tail=node.previous;
    if(node.unitPrevious) nodes_.at(node.unitPrevious).unitNext=node.unitNext;
    else if(node.unitNext) owners_.at(node.event.owner)=node.unitNext;
    else owners_.erase(node.event.owner);
    if(node.unitNext) nodes_.at(node.unitNext).unitPrevious=node.unitPrevious;
    nodes_.erase(found);
}
bool FrameEventQueue::cancel(FrameEventHandle handle) {
    const auto found=nodes_.find(handle);if(found==nodes_.end()) return false;
    if(found->second.executing) found->second.cancelled=true;else unlink(handle);
    return true;
}
void FrameEventQueue::cancel(FrameEventOwner owner,FrameEventType type,int32_t customId) {
    const auto found=owners_.find(owner);if(found==owners_.end()) return;
    auto handle=found->second;
    while(handle) {
        const auto &node=nodes_.at(handle);const auto next=node.unitNext;
        if(node.event.type==type && (!customId || node.event.customId==customId)) cancel(handle);
        handle=next;
    }
}
void FrameEventQueue::cancelHandler(FrameEventOwner owner,FrameEventType type,uint32_t handler) {
    const auto found=owners_.find(owner);if(found==owners_.end()) return;
    auto handle=found->second;
    while(handle) {
        const auto &node=nodes_.at(handle);const auto next=node.unitNext;
        if(node.event.type==type && node.event.handler==handler) cancel(handle);
        handle=next;
    }
}
void FrameEventQueue::cancelOwner(FrameEventOwner owner) {
    const auto found=owners_.find(owner);if(found==owners_.end()) return;
    auto handle=found->second;
    while(handle) {const auto next=nodes_.at(handle).unitNext;cancel(handle);handle=next;}
}
std::vector<FrameEvent> FrameEventQueue::events(FrameEventOwner owner) const {
    std::vector<FrameEvent> result;
    const auto found=owners_.find(owner);if(found==owners_.end()) return result;
    for(auto handle=found->second;handle;handle=nodes_.at(handle).unitNext) result.push_back(nodes_.at(handle).event);
    return result;
}
std::optional<uint64_t> FrameEventQueue::nextDue(FrameEventOwner owner,FrameEventType type) const {
    std::optional<uint64_t> result;
    const auto found=owners_.find(owner);if(found==owners_.end()) return result;
    for(auto handle=found->second;handle;handle=nodes_.at(handle).unitNext) {
        const auto &node=nodes_.at(handle);
        if(!node.cancelled && !node.event.continuous && node.event.type==type && (!result || node.event.due<*result)) result=node.event.due;
    }
    return result;
}
bool FrameEventQueue::hasDue(FrameEventOwner owner,uint64_t frame) const {
    const auto found=owners_.find(owner);if(found==owners_.end()) return false;
    for(auto handle=found->second;handle;handle=nodes_.at(handle).unitNext) {
        const auto &node=nodes_.at(handle);if(!node.cancelled && node.event.due<=frame) return true;
    }
    return false;
}
bool FrameEventQueue::hasEvent(FrameEventOwner owner,FrameEventType type) const {
    const auto found=owners_.find(owner);if(found==owners_.end()) return false;
    for(auto handle=found->second;handle;handle=nodes_.at(handle).unitNext)
        if(nodes_.at(handle).event.type==type) return true;
    return false;
}
bool FrameEventQueue::advance(uint64_t frame,const std::function<bool(const FrameEvent &)> &dispatch) {
    if(!frame_ || frame>*frame_) {
        if(frame_ && phase_!=10) throw std::logic_error("Native event frame advanced before completion");
        if(frame_ && (*frame_==UINT64_MAX || frame!=*frame_+1)) throw std::logic_error("Native event queue skipped a frame");
        frame_=frame;phase_=0;started_=false;
    } else if(frame<*frame_) throw std::logic_error("Native event queue moved backwards");
    constexpr std::array<size_t,5> order{2,0,1,3,4};
    while(phase_<10) {
        if(!started_) {
            const auto &queue=queues_[order[phase_/2]];
            cursor_=(phase_%2?queue.timed[size_t(frame%64)]:queue.continuous).head;
            started_=true;
        }
        while(executing_ || cursor_) {
            if(!executing_) {
                const auto handle=cursor_;auto &node=nodes_.at(handle);
                // Capture the next link before invoking the callback. Cancel
                // updates cursor_, so deleting the next node cannot skip past
                // another valid node or leave a dangling traversal pointer.
                cursor_=node.next;
                if(!node.event.continuous && node.event.due!=frame) continue;
                node.executing=true;executing_=handle;
            }
            // A value snapshot stays valid if the callback inserts or removes
            // other timers. A blocked callback retains its executing marker.
            const auto event=nodes_.at(executing_).event;
            if(!dispatch(event)) return false;
            auto &node=nodes_.at(executing_);node.executing=false;
            const auto completed=executing_;executing_={};
            if(!node.event.continuous || node.cancelled) unlink(completed);
        }
        ++phase_;started_=false;
    }
    return true;
}
}
