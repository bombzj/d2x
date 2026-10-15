#include "time.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <cstdlib>
#include <stdexcept>
namespace d2x {
namespace {
struct Cycle {int begin,period;};
constexpr std::array<Cycle,6> normal{{{320,3},{340,3},{0,0},{160,1},{180,1},{200,2}}};
constexpr std::array<Cycle,6> actFour{{{340,3},{350,3},{0,0},{180,1},{190,1},{200,2}}};
constexpr std::array<Cycle,6> eclipse{{{300,3},{0,0},{60,1},{120,2},{180,2},{240,2}}};
void validate(const NativeActEnvironment &value) {
    if(value.cycle<0 || value.cycle>=6 || value.period<0 || value.period>=4 ||
        value.ticks<0 || (value.rate!=128 && value.rate!=4 && value.rate!=8) ||
        value.previousDegrees<0 || value.previousDegrees>=360 || value.ticks>=360*value.rate)
        throw std::invalid_argument("Invalid native act environment");
}
}
StatTimeContext actStatTime(const NativeActEnvironment &value) {
    validate(value);return {value.period,nativeDivide(value.ticks,value.rate)};
}
bool advanceActEnvironment(NativeActEnvironment &value,int act) {
    validate(value);if(act<0 || act>=5) throw std::invalid_argument("Invalid native environment act");
    const auto oldCycle=value.cycle,oldPeriod=value.period;
    value.ticks=nativeAdd(value.ticks,1);
    if(!value.eclipse) {
        if(act==3) value.ticks=nativeAdd(value.ticks,15);
        else if(normal[size_t(value.cycle)].period==2) {
            value.ticks=nativeAdd(value.ticks,1);
            // Fixed 1.13c RVA375A7 adds 8; the D2MOO 1.10f transcription adds 9.
            if(act==2) value.ticks=nativeAdd(value.ticks,8);
        }
    }
    if(value.ticks>=nativeMultiply(360,value.rate)) value.ticks=0;
    const auto next=(value.cycle+1)%6;
    // Preserve 1.13c's actual table selection (RVA375D4..375F9), including
    // its different act-IV handling from D2MOO's ENVIRONMENT_GetCycle helper.
    const auto &threshold=!value.eclipse?normal:value.eclipse && act==3?actFour:eclipse;
    if(value.ticks>nativeMultiply(value.rate,threshold[size_t(next)].begin)) {
        value.cycle=next;
        const auto &reset=value.eclipse?eclipse:normal;
        value.period=reset[size_t(next)].period;
        value.ticks=nativeMultiply(value.rate,reset[size_t(next)].begin);
    }
    const auto degrees=nativeDivide(value.ticks,value.rate);
    if(std::abs(nativeSubtract(degrees,value.previousDegrees))>16) {
        value.previousDegrees=degrees;return true;
    }
    return oldCycle!=value.cycle || oldPeriod!=value.period;
}
void setActSunEclipse(NativeActEnvironment &value,bool dark) {
    validate(value);
    value.eclipse=dark;value.rate=dark?4:128;value.cycle=dark?0:2;
    value.period=dark?eclipse[0].period:normal[2].period;
    // InitializeEnvironment overwrites ticks on eclipse begin. End retains
    // its explicit zero input; neither call resets nPrev.
    value.ticks=dark?nativeMultiply(eclipse[0].begin,value.rate):0;
}
}
