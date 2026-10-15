#pragma once
#include "core/id.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace d2x {
enum class FrameEventUnit : uint8_t { Player=0, Monster=1, Object=2, Missile=3, Item=4, Game=6 };
enum class FrameEventType : uint8_t {
    ModeChange=0, EndAnimation=1, AiThink=2, StatRegeneration=3, Trap=4,
    ActiveState=5, FreeHover=6, MonsterModifier=7, PeriodicSkills=8,
    PeriodicStats=9, AiReset=10, DelayedPortal=11, RemoveState=12,
    TradeOrVendor=13, RemoveSkillCooldown=14
};
struct FrameEventOwner {
    FrameEventUnit type{FrameEventUnit::Game};
    EntityId unit;
    auto operator<=>(const FrameEventOwner &) const = default;
};
struct FrameEventHandle {
    uint64_t value{};
    explicit operator bool() const {return value!=0;}
    auto operator<=>(const FrameEventHandle &) const = default;
};
// Handler is a domain-owned identity, not a function pointer or live object.
// Default/continuous events dispatch through the owner's native type handler.
struct FrameEvent {
    FrameEventHandle handle;
    FrameEventOwner owner;
    FrameEventType type{};
    uint64_t due{};
    bool continuous{};
    uint32_t handler{};
    int32_t customId{},customParameter{};
    uint64_t payload{};
};
struct FrameEventObservation {
    FrameEvent event;
    bool executing{},pendingRemoval{};
};
struct FrameEventQueueObservation {
    size_t count{},phase{};
    std::optional<uint64_t> frame;
    FrameEventHandle executing,cursor;
    std::vector<FrameEventObservation> events;
};
// Compatibility evidence: D2MOO GAME/Event.cpp (5596f5c, MIT), retail 1.13c
// D2Game RVA 8F070/8EA80/8E810/8E6F0. Five unit queues, 64 timed buckets and a
// continuous head per queue. Reliable output is a separate server concern.
class FrameEventQueue {
    struct Chain {FrameEventHandle head,tail;};
    struct Node {
        FrameEvent event;
        FrameEventHandle previous,next,unitPrevious,unitNext;
        bool executing{},cancelled{};
    };
    struct UnitQueue {std::array<Chain,64> timed;Chain continuous;};
    std::array<UnitQueue,5> queues_;
    std::map<FrameEventHandle,Node> nodes_;
    std::map<FrameEventOwner,FrameEventHandle> owners_;
    uint64_t next_=1;
    std::optional<uint64_t> frame_;
    size_t phase_{};
    bool started_{};
    FrameEventHandle cursor_,executing_;
    static size_t queueIndex(FrameEventUnit);
    Chain &chain(const FrameEvent &);
    const Chain &chain(const FrameEvent &) const;
    void unlink(FrameEventHandle);
  public:
    class PreparedInsertion {
        friend class FrameEventQueue;
        const FrameEventQueue *queue_{};
        std::map<FrameEventHandle,Node> nodes_;
        std::map<FrameEventOwner,FrameEventHandle> owners_;
      public:
        PreparedInsertion()=default;
        PreparedInsertion(PreparedInsertion &&) noexcept=default;
        PreparedInsertion &operator=(PreparedInsertion &&) noexcept=default;
        PreparedInsertion(const PreparedInsertion &)=delete;
        PreparedInsertion &operator=(const PreparedInsertion &)=delete;
        FrameEventHandle handle() const {return nodes_.empty()?FrameEventHandle{}:nodes_.begin()->first;}
    };
    struct PreparedEdit {
        FrameEventOwner owner;
        FrameEventType type{};
        std::vector<FrameEventHandle> expected,removed;
        std::vector<PreparedInsertion> added;
        std::optional<uint32_t> handler{0};
    };
    // Insertion normalizes signed native deadlines; -1 ignores the explicit
    // callback like D2GAME_InitTimer and goes to the continuous queue head.
    FrameEventHandle insert(FrameEventOwner,FrameEventType,int32_t nativeFrame,uint64_t now,
        uint32_t handler=0,int32_t customId=0,int32_t customParameter=0);
    // Publish a deadline already normalized while preparing a candidate. The
    // supplied handle is ignored; publication assigns a fresh local identity.
    FrameEventHandle insertPrepared(FrameEvent);
    PreparedInsertion prepareInsertion(FrameEvent);
    // Nodes and owner-index nodes were allocated during preparation. Native
    // chain links are attached only after the domain accepts its publication.
    FrameEventHandle publish(PreparedInsertion &&) noexcept;
    PreparedEdit prepareEdit(FrameEventOwner,FrameEventType,const std::vector<FrameEvent> &localEvents,std::optional<uint32_t> handler=0);
    bool validate(const PreparedEdit &) const;
    void publish(PreparedEdit &&) noexcept;
    bool cancel(FrameEventHandle);
    void cancel(FrameEventOwner,FrameEventType,int32_t customId=0);
    void cancelHandler(FrameEventOwner,FrameEventType,uint32_t handler);
    void cancelOwner(FrameEventOwner);
    const FrameEvent *find(FrameEventHandle) const;
    std::vector<FrameEvent> events(FrameEventOwner) const;
    std::optional<uint64_t> nextDue(FrameEventOwner,FrameEventType) const;
    bool hasDue(FrameEventOwner,uint64_t frame) const;
    bool hasEvent(FrameEventOwner,FrameEventType) const;
    size_t size() const {return nodes_.size();}
    bool idle() const {return !frame_ || phase_==10;}
    uint64_t reserveIdentity();
    FrameEventQueueObservation observe(size_t limit) const;
    // False means a retained callback is blocked. Resume with the same frame;
    // self-cancellation does not destroy its cursor before it completes.
    bool advance(uint64_t frame,const std::function<bool(const FrameEvent &)> &dispatch);
};
struct FrameEventPublication {FrameEventQueue::PreparedEdit edit;};
}
