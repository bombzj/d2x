#pragma once
#include <array>
#include <optional>
#include <cstdint>
namespace d2x::server::combat {
struct Ports;struct SpellImpact;struct ParticipantView;
std::optional<std::array<int,2>> pd2ItemPierce(const Ports &,const SpellImpact &,ParticipantView,uint64_t tick);
}
