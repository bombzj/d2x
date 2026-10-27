#include "crushing.hpp"
#include "../system.hpp"
#include "../participants.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/actions/state.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/items/pd2_parameterized_stat.hpp"
#include "gameplay/rules/pd2_corpse.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::combat {
int64_t pd2CrushingBlow(const Ports &ports,const SpellImpact &impact,ParticipantView source,ParticipantView target,uint64_t tick,uint64_t &random) {
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,source.id());
    if(!caster || !caster->programs() || !impact.weapon) return 0;
    const auto &rules=*caster->programs();
    const auto &weapon=impact.weapon->weapon;
    const auto stats=ports.effects.unitStatValues(source.id(),tick,weapon.item?std::optional{weapon.item}:std::nullopt);
    if(!stats) throw std::logic_error("Missing PD2 Crushing Blow owner stats");
    const auto *equipment=caster->player?&caster->player->totals.equipment:
        caster->monster->equipmentStats?&*caster->monster->equipmentStats:nullptr;
    const bool dual=caster->player?(caster->player->definition.code=="bar" || caster->player->definition.code=="ass"):
        caster->monster->identity.monster=="shadowwarrior" || caster->monster->identity.monster=="shadowmaster";
    const auto stat=[&](std::string_view name) {
        const auto id=rules.stats->names.at(std::string(name));
        return nativeAdd(stats->get({id,0}),pd2ParameterizedItemStat(*stats,id,weapon,equipment,rules.formulas->itemTypes,dual));
    };
    auto chance=stat("item_crushingblow"),efficiency=stat("item_crushingblow_efficiency");
    const auto used=ports.actions.usedSkill(source.id());
    if(used && used->skill==97) {
        auto input=caster->formulaInput(ports.effects,tick);SkillFormulaValues values(*rules.formulas,input);
        const auto rank=input.referenceRanks->effective.find(97);
        const auto level=rank==input.referenceRanks->effective.end()?1:rank->second;
        const auto bonus=values.calculation(FormulaDomain::Skill,97,level,"calc3");
        const auto effect=values.calculation(FormulaDomain::Skill,97,level,"calc4");
        if(!bonus || !effect) throw std::logic_error("Missing PD2 Smite Crushing Blow formulas");
        chance=nativeAdd(chance,*bonus);efficiency=nativeAdd(efficiency,*effect);
    }
    if(chance<=0 || pd2UnitEventRandom(random)%100>=uint32_t(chance)) return 0;
    const bool hireling=target.monster && target.monster->hireling;
    double divisor=target.player || hireling?10.0:8.0;
    if(target.monster && !hireling) {
        const auto &body=*target.monster;
        if(body.rule.boss || body.identity.rank==MonsterRank::SuperUnique) {
            if(!pd2SearchableCorpseClass(body.rule.nativeClass)) divisor=30.0;
            else {
                const auto maximum=body.maximumLife>>8;
                const auto percent=maximum?nativeMultiplyDivide(nativeChecked(body.life>>8),100,nativeChecked(maximum)):0;
                divisor=(100.0-double(percent))*10.0+70.0;
            }
        }
        divisor+=double(std::max(1u,body.admittedPlayerCount)-1);
    }
    if(!impact.melee) divisor*=1.5;
    divisor/=1.0+double(efficiency)/100.0;
    const auto life=target.player?int64_t(target.player->persistent.player.hp.raw()):target.monster->life;
    const auto resistance=ports.effects.unitResistance(target.id(),DamageType::Physical,tick);
    if(resistance>=100) return 0;
    // 0x3469B0 keeps the HP quotient in double. Only the resistance helper
    // and final new HP use CVTTSD2SI; rounding the CB amount first differs.
    const double amount=double(uint32_t(life))/divisor;
    const auto narrowed=nativeTruncate(amount);
    const double remaining=double(uint32_t(life))-(amount-pd2MultiplyDivide(narrowed,resistance,100));
    const auto next=nativeTruncate(remaining);
    return life-std::max(0,next);
}
}
