#include "../system.hpp"
#include "../runtime.hpp"
#include "../evaluation.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2Enchant(Release &pending,const ActorContext &actor) {
    const auto &owner=*ports_.players.find(actor.player);
    if(!pending.directSkill) {
        auto input=formulaInput(owner);SkillFormulaValues values(*owner.rules.skills->formulas,input);
        const auto radius=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"aurarangecalc");
        if(!radius) throw std::runtime_error("Missing PD2 Enchant aura range");
        const auto &row=owner.rules.skills->formulas->skills.at(pending.skill.sourceId);
        auto pulse=std::make_shared<effects::NativeAreaPulse>();
        pulse->targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,*radius,uint32_t(row.number("aurafilter")),false,
            {},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
        std::erase(pulse->targets,actor.actor);pulse->targets.insert(pulse->targets.begin(),actor.actor);
        pending.directSkill=std::move(pulse);
    }
    if(!pending.manaPaid) {
        const auto paid=ports_.transactions.release(actor,owner.characterRevision,pending.skill.manaCost,{},pending.skill.charge,{},{},pending.skill.sourceId);
        if(!paid) return paid.status;
        pending.manaPaid=true;
    }
    auto applied=pending.skill;applied.manaCost=applied.startMana=0;applied.charge.reset();
    auto &pulse=*pending.directSkill;pending.nativeCallbackStarted=true;
    while(pulse.next<pulse.targets.size()) {
        const auto target=pulse.targets[pulse.next];
        DomainStatus status=DomainStatus::Unavailable;
        if(const auto *player=ports_.players.findActor(target))
            status=ports_.effects.skill(actor,applied,player->player).status;
        else status=ports_.effects.skillUnit(actor,applied,target).status;
        if(status==DomainStatus::Capacity) return status;
        ++pulse.next;
    }
    pulse.complete=true;pending.nativeCallbackStarted=false;
    return DomainStatus::Applied;
}
}
