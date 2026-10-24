#pragma once
#include "server/runtime/contracts.hpp"
#include "server/systems/missiles/requests.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include <vector>
namespace d2x::server::combat {
struct Ports;struct SpellImpact;
struct ParticipantView;
bool pd2Gathering(const SpellImpact &);
DomainStatus preparePd2Gather(const Ports &,SpellImpact &,TickContext);
DomainStatus postPd2Gather(const Ports &,SpellImpact &,TickContext);
DomainResult<std::vector<missiles::NativeSpawn>> preparePd2ProgressiveBirths(const Ports &,const SpellImpact &,
    const AssassinCharge &,AssassinProgressiveStep,ParticipantView,TickContext,uint64_t &ownerRandom);
}
