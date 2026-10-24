#include "../system.hpp"
#include "../runtime.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include <stdexcept>

namespace d2x::server::missiles {
bool System::preparePd2Program(Advance &plan) const {
    auto &missile=plan.next;const auto &rule=missile.nativeDamage->rule;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(missile.owner);
    switch(rule.serverDoFunction) {
    case 38:
        // Current core RVA 0x3A4E60: return to a live owner, remove within
        // two native distance units. A dead owner leaves the existing path.
        if(owner.id() && owner.aliveIn(unitArea(owner.id()))) {
            const auto point=unitPosition(missile,owner.id());
            if(point) {
                if(missileDistance(missile.position,*point)<2) {plan.finished=true;return false;}
                missile.pathTarget=*point;missile.turnTarget=*point-missile.position;
                missile.velocity=missile.turnTarget.unit()*missile.velocity.length();++missile.pathRevision;
            }
        }
        return true;
    case 39:
        // RVA 0x3A4EE0 uses CurrentFrame, not chronological elapsed time.
        if(!owner.id() || !owner.aliveIn(unitArea(owner.id()))) {plan.finished=true;return false;}
        if(!rule.serverParameters[0]) throw std::logic_error("Invalid PD2 child missile period");
        if(missile.currentFrame()%rule.serverParameters[0]<1)
            for(const auto definition:rule.subMissiles) if(definition>=0) {
                auto &child=planNativeChild(plan,definition,missile.position,{});
                // 0x37E080 sends the newborn to connected clients; it does
                // not change lifetime or activation frames.
                if(rule.serverParameters[1]>0) child.enemy->rule.clientSend=true;
            }
        return true;
    case 40:
        // RVA 0x3A5020 disables terrain collision while retaining native unit
        // collision. Its post-collision continuation is handled by the flight.
        missile.collision.mask=0x180;
        return true;
    default:return false;
    }
}
}
