#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace d2x {
struct ActionFrameEvent {
    int tick{}, event{}, parameter{};
};
struct ActionTimeline {
    std::vector<ActionFrameEvent> events;
    int end{};
};
// D2MOO SUnit::sub_6FCBCE70: player MODECHANGE events are scheduled at
// action start. Preserve every crossed flag, including multiple flags in one
// game frame; this is not the monster's per-frame StopSequence algorithm.
ActionTimeline playerActionTimeline(int frames,int speed,int startFrame,std::span<const uint8_t> flags);
// SUnit::sub_6FCBCFD0/BD120/RewindSkillEx replan from elapsed game frames.
ActionTimeline rewoundPlayerActionTimeline(int frames,int speed,int elapsed,std::span<const uint8_t> flags);
}
