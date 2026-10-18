#include "system.hpp"
#include "server/systems/actions/release.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include "evaluation.hpp"
#include <algorithm>
#include <climits>

namespace d2x::server::skills {
DomainResult<> System::releaseOverseer(MonsterRelease &pending,TickContext tick) {
    const auto *source=ports_.monsters.find(pending.request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
    const auto target=std::get<UnitTarget>(pending.request.target);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto recipient=participants.find(target.id);
    if(target.id==source->id || !(recipient.player?recipient.player->entered && recipient.player->persistent.player.hp>0:recipient.monster && recipient.monster->alive())) return {DomainStatus::InvalidActor,{}};
    const int skill=pending.request.skill,rank=pending.attack.rank;
    if(pending.attack.action==MonsterAttackRule::Action::SupportMissile) {
        const auto at=ports_.monsters.targetPosition(target.id,source->area);if(!at || !pending.attack.missile) return {DomainStatus::Unavailable,{}};
        missiles::NativeSpawn request{source->id,source->area,ports_.areas.at(source->area).generation,tick.tick,
            source->position,at->first,pending.attack.missile->definition,skill,rank};
        request.copyMonsterAttackBonus=true;
        const auto result=ports_.missiles.spawnNative(request);return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    }
    if(!pending.formulaPrepared) {
        const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        const auto expected=source->combatRandom;auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=formulaInput(*source,*sampled,draw);SkillFormulaValues values(*source->rule.formulas,input);
        pending.formulaValue=INT_MIN;
        if(pending.attack.action==MonsterAttackRule::Action::Rally) {
            const auto length=values.calculation(FormulaDomain::Skill,skill,rank,"calc1");if(!length) return {DomainStatus::Unavailable,{}};
            pending.formulaValue=nativeAdd(nativeNarrow(uint32_t(tick.tick)),std::max(*length,1));
        } else if(recipient.monster && recipient.monster->rule.ai.kind==MonsterAiKind::Minion) {
            // Retail Do131 evaluates calc1 before its unit-seeded roll, even
            // when BLOODLUST will subsequently prevent transformation.
            const auto chance=values.calculation(FormulaDomain::Skill,skill,rank,"calc1");if(!chance) return {DomainStatus::Unavailable,{}};
            const auto roll=int(limitedRandom(random,100));
            if(roll>=*chance && !ports_.effects.unitStates(target.id,tick.tick).contains(source->rule.bloodlustState)) {
                const auto found=source->rule.whipClasses.find(recipient.monster->rule.nativeClass);
                if(found==source->rule.whipClasses.end()) return {DomainStatus::NotImplemented,{}};
                pending.formulaValue=found->second;
            }
        }
        const auto consumed=ports_.monsters.consumeRandom(source->id,expected,random);if(!consumed) return consumed;
        pending.formulaPrepared=true;
    }
    if(pending.attack.action==MonsterAttackRule::Action::Whip) {
        if(pending.formulaValue==INT_MIN) return ports_.effects.monsterBloodlust(source->id,target.id,skill,rank,tick.tick);
        const auto changed=ports_.monsters.reinitializeWhipped(source->id,target.id,pending.formulaValue,tick.tick);
        if(changed && pending.formulaValue>=0) cancel({},target.id);
        return changed;
    }
    const auto overlay=source->rule.formulas->skills.at(skill).serverOverlay;
    if(overlay>0) {
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},source->area},{OverlayFact{target.id,target.type,source->area,overlay}}});
        if(!sent) return {sent.status,{}};
    }
    ports_.ai.commandMinions(source->id,target,uint32_t(pending.formulaValue));
    return {DomainStatus::Applied,std::monostate{}};
}
}
