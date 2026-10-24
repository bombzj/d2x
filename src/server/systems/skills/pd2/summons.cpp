#include "../system.hpp"
#include "../native_caster.hpp"
#include "execution.hpp"
#include "server/systems/actions/release.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2Raven(Release &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    // Current Do114 rejects a monster controlled by a positive owner GUID.
    if(caster->monster && caster->controller) return DomainStatus::Unavailable;
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    auto &execution=*pending.pd2Execution;
    if(!execution.ravens) {
        auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        const auto count=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc3");
        if(!count) throw std::logic_error("Missing PD2 Raven count");
        if(*count>512) return DomainStatus::Capacity;
        execution.ravens=Pd2CastExecution::Ravens{*count,0,{}};
    }
    auto &ravens=*execution.ravens;
    pending.nativeCallbackStarted=true;
    while(ravens.next<ravens.count) {
        if(!ravens.point) {
            // 0x3AE230 draws each offset relative to the original target.
            // Keep the chosen point across asynchronous content preparation.
            Vec offset;
            if(!ravens.next) offset={0,-float(pd2CrtRandom(ports_.pd2Random)%4)-1};
            else if(ravens.next==1 || ravens.next==2) {
                offset.x=float(pd2CrtRandom(ports_.pd2Random)%4)+1;
                if(ravens.next==2) offset.x=-offset.x;
                offset.y=float(pd2CrtRandom(ports_.pd2Random)%4)+1;
            } else offset={float(int(pd2CrtRandom(ports_.pd2Random)%8)-4),float(int(pd2CrtRandom(ports_.pd2Random)%8)-4)};
            ravens.point=target+offset;
        }
        auto skill=pending.skill;
        if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        const auto result=ports_.companions.druid(actor,skill,*ravens.point);
        if(!result) return result.status;
        pending.manaPaid=true;++ravens.next;ravens.point.reset();
    }
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
}
