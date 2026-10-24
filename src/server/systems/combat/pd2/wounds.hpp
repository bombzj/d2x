#pragma once
#include "gameplay/combat/open_wounds.hpp"
#include <optional>

namespace d2x::server::combat {
struct Ports;struct SpellImpact;struct ParticipantView;
std::optional<OpenWoundsApplication> pd2OpenWounds(const Ports &,const SpellImpact &,ParticipantView source,
    ParticipantView target,uint64_t tick,uint64_t &random);
}
