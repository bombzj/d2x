#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace d2x {
// Only execution budgets stop with input. Submitted native requests keep
// their steady-clock deadlines and continue observing the real replica.
class DebugExecutionClock {
    using Clock=std::chrono::steady_clock;
    Clock::time_point last_{};Clock::duration suspended_{};bool blocked_{};
  public:
    void reset(Clock::time_point now) {last_=now;suspended_={};blocked_=false;}
    Clock::duration sample(Clock::time_point now,bool blocked) {
        const auto elapsed=last_!=Clock::time_point{} && blocked_?now-last_:Clock::duration{};
        last_=now;blocked_=blocked;suspended_+=elapsed;return elapsed;
    }
    bool blocked() const {return blocked_;}
    int64_t suspendedMs() const {return std::chrono::duration_cast<std::chrono::milliseconds>(suspended_).count();}
};
enum class DebugActionOwner:uint8_t {Goal,Navigation,Door,Waypoint,Mobility,Potion,Supply,Combat};
enum DebugActionInput:uint8_t {DebugMotion=1,DebugSkill=2,DebugItem=4,DebugInteraction=8,DebugMenu=16,DebugAllInputs=31};
constexpr unsigned debugActionBit(DebugActionOwner owner) {return 1u<<unsigned(owner);}
enum class DebugSubmitResult {Waiting,Accepted,Expired};
// Pre-submission waits own no server request. Their budget pauses with AI
// input; the domain starts a separate real-time result wait on acceptance.
struct DebugSubmissionWait {
    std::string action,reason;unsigned inputs{},attempts{};uint64_t decision{};
    std::chrono::steady_clock::time_point deadline{},retryAt{};
};
struct DebugActionContract {
    DebugActionOwner owner;std::string_view action;unsigned inputs{},interruptors{};
    std::string_view waitingFor,recovery;
    std::chrono::steady_clock::time_point deadline{};
    bool resultObserved{};
    bool permits(DebugActionOwner caller,unsigned requested) const {
        return caller==owner || !(inputs&requested) || (interruptors&debugActionBit(caller));
    }
};
} // namespace d2x
