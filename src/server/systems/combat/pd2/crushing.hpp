#pragma once
#include "server/runtime/contracts.hpp"
#include <cstdint>
namespace d2x::server::combat {
struct Ports;struct SpellImpact;struct ParticipantView;
int64_t pd2CrushingBlow(const Ports &,const SpellImpact &,ParticipantView source,ParticipantView target,uint64_t tick,uint64_t &random);
}
