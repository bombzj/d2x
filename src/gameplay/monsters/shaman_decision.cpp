#include "shaman_decision.hpp"
#include "core/random.hpp"
namespace d2x {
MonsterDecision decideFallenShaman(const MonsterAiProfile &profile,MonsterDecisionInput in,bool corpseAvailable) {
    const auto &p=profile.params;MonsterDecision out;out.random=in.random;
    auto chance=[&](int percent){return int(limitedRandom(out.random,100))<percent;};
    // master fallenShamanThink / AITHINK_Fn013, including the independent
    // command and resurrection rolls and the alternate visible target roll.
    if(in.contact && chance(p[2])) {out.action=MonsterDecisionAction::Attack;return out;}
    out.commandParty=chance(p[0]);
    if(corpseAvailable && chance(p[0])) {out.action=MonsterDecisionAction::Special;out.skillSlot=0;}
    else if(in.distance<p[4] && chance(p[1])) {out.action=MonsterDecisionAction::Special;out.skillSlot=1;}
    else if(in.clear && in.distance<p[4] && chance(p[1])) {out.action=MonsterDecisionAction::Special;out.skillSlot=1;}
    else if(chance(p[2])) {out.action=MonsterDecisionAction::Circle;out.stopDistance=3;}
    else out.waitFrames=10;
    return out;
}
}
