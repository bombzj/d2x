#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace d2x {
// Decision cadence is independent of native input throttles and request
// deadlines. New observations wake a slow mode; deferred searches resume on
// the next display frame without consuming an artificial thinking delay.
class DebugDecisionCadence {
    using Clock=std::chrono::steady_clock;
    std::array<uint64_t,24> observed_{};
    Clock::time_point last_{},next_{};
    bool continuation_{};
    int interval_=250;
    uint64_t events_{},timers_{},continuations_{};
    std::string_view reason_="initial";
  public:
    enum class Mode {Reaction,Combat,Navigation,Idle,Waiting,Suspended};
    void reset() {*this={};}
    void wake() {next_={};}
    void continueNextFrame() {continuation_=true;}
    bool due(Clock::time_point now,Mode mode,const std::array<uint64_t,24> &signals,bool urgent=false,
           Clock::time_point deadline={},int navigationMs=160,int reactionMs=40) {
        static constexpr int periods[]{40,80,160,250,500,500};
         interval_=mode==Mode::Navigation?navigationMs:mode==Mode::Reaction?reactionMs:periods[unsigned(mode)];
        const auto period=std::chrono::milliseconds(interval_);
        const bool changed=signals!=observed_;
        auto dueAt=std::min(next_,last_+period);
        if(deadline!=Clock::time_point{}) dueAt=std::min(dueAt,deadline);
        const bool event=changed && now-last_>=std::chrono::milliseconds(urgent?20:40);
        if(last_!=Clock::time_point{} && !continuation_ && !event && now<dueAt) return false;
        if(continuation_) {reason_="search-continuation";++continuations_;}
        else if(event) {reason_=urgent?"urgent-observation":"observation-changed";++events_;}
        else {reason_="mode-timer";++timers_;}
        observed_=signals;last_=now;next_=now+period;continuation_=false;return true;
    }
    int intervalMs() const {return interval_;}
    std::string_view reason() const {return reason_;}
    uint64_t eventWakeups() const {return events_;}
    uint64_t timerWakeups() const {return timers_;}
    uint64_t continuations() const {return continuations_;}
};
inline constexpr auto debugInputRetryInterval=std::chrono::milliseconds(200);
} // namespace d2x
