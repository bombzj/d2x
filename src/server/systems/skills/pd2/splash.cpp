#include "../system.hpp"
#include "../runtime.hpp"
#include "../native_caster.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/systems/combat/player_contact.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/skill_state.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/items/pd2_parameterized_stat.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2Splash(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &rules=*caster->programs();
    if(!pending.directSkill) {
        const auto *player=ports_.players.findActor(pending.unit);
        const auto *monster=ports_.monsters.find(pending.unit);
        if(!player && !monster) return DomainStatus::Unavailable;
        const auto region=player?player->area:monster->area;
        if(ports_.areas.at(region).definition.town) return DomainStatus::Unavailable;
        int skill=0,rank=1;
        if(caster->player) if(const auto used=state_.usedSkill(actor.actor);used && used->skill!=pending.skill.sourceId && rules.formulas->skills.contains(used->skill)) {
            skill=used->skill;rank=std::max(1,nativeSkillSlotLevel(*used,caster->player->totals.skillOrder,
                caster->player->totals.skillRanks,caster->player->totals.chargedSkills,rules.formulas->maximumSkillRank));
        }
        const auto &row=rules.formulas->skills.at(skill);
        if(skill==97 && row.number("param5")) {
            // Do163 reuses this list without extending its original deadline.
            // Infection updates only a newly allocated list, so write through
            // the ordinary reuse path for this fixed three-frame native state.
            const auto *states=ports_.effects.unitEffects(actor.actor);
            uint32_t duration=3;
            if(states) for(const auto &entry:states->entries()) if(entry.spec.state.id==224 && entry.expiresAt)
                duration=*entry.expiresAt>actor.tick?uint32_t(std::min<uint64_t>(*entry.expiresAt-actor.tick,INT32_MAX)):0;
            effects::NativeSkillStateValues state;state.state=224;
            state.stats[0]={rules.stats->names.at("inc_splash_radius"),row.number("param5")};
            const auto posted=ports_.effects.skillAuraState(actor.actor,actor.actor,skill,rank,int(duration),actor.tick,
                effects::NativeSkillStateProcedure::Warcry,&state);
            if(isDeferred(posted.status)) return posted.status;
        }
        auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
        const auto range=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc3");
        if(!range) throw std::logic_error("Missing PD2 splash radius inputs");
        WeaponDamage weapon=caster->player?caster->player->totals.equipment.weapons[0]:caster->monster->petWeapon.value_or(WeaponDamage{});
        for(const auto &primary:ports_.combat.read().spells) if(primary.source==actor.actor && primary.nativeSkill && primary.weapon &&
            primary.next<primary.targets.size() && primary.targets[primary.next]==pending.unit && primary.nativeSkill->directFunction!=163)
            weapon=primary.weapon->weapon;
        const auto stats=ports_.effects.unitStatValues(actor.actor,actor.tick,weapon.item?std::optional{weapon.item}:std::nullopt);
        if(!stats) return DomainStatus::Unavailable;
        const auto *equipment=caster->player?&caster->player->totals.equipment:
            caster->monster->equipmentStats?&*caster->monster->equipmentStats:nullptr;
        const bool dual=caster->player?(caster->player->definition.code=="bar" || caster->player->definition.code=="ass"):
            caster->monster->identity.monster=="shadowwarrior" || caster->monster->identity.monster=="shadowmaster";
        const auto id=rules.stats->names.at("inc_splash_radius");
        // Calc3 already includes the unparameterized stat. The native helper
        // adds only the matching item-type parameter layers here.
        const auto extra=pd2ParameterizedItemStat(*stats,id,weapon,equipment,rules.formulas->itemTypes,dual);
        auto radius=nativeAdd(*range,extra/20);
        if(caster->player && weapon.item) {
            const auto item=caster->player->persistent.inventory.items.find(weapon.item);
            if(item!=caster->player->persistent.inventory.items.end() && rules.normalWeaponCodes.contains(item->second.definition)) --radius;
        }
        SkillCastSpec attack;attack.effect=SkillBehavior::WeaponProjectile;attack.sourceId=skill;attack.rank=rank;
        attack.pd2=pending.skill.pd2;
        attack.hitClass=row.number("hitclass");attack.weapon=WeaponSkillSpec{};
        if(skill==97) attack.weapon->doFunction=150;
        // Current core 0x306090 selects the ED formula by native skill ID.
        // Calc1 on unrelated skills may describe counts or another quantity.
        std::string_view enhancedField;
        switch(skill) {
        case 0:case 10:case 96:case 97:case 107:case 126:case 133:case 144:
        case 147:case 151:case 152:case 232:case 260:case 378:case 380:enhancedField="calc1";break;
        case 30:case 106:case 248:case 255:case 265:case 275:enhancedField="calc2";break;
        default:break;
        }
        if(!enhancedField.empty()) {
            const auto enhanced=values.calculation(FormulaDomain::Skill,skill,rank,enhancedField);
            if(!enhanced) throw std::logic_error("Missing PD2 splash enhanced damage");
            attack.weapon->damagePercent=*enhanced;
        }
        if(skill==144 && row.elementType) {
            const auto conversion=values.calculation(FormulaDomain::Skill,skill,rank,"calc4");
            if(!conversion) throw std::logic_error("Missing PD2 Concentrate splash conversion");
            attack.weapon->magicConversionPercent=*conversion;
        }
        const auto modifiers=caster->player?caster->player->totals.character.combat:ports_.effects.combatModifiers(actor.actor,actor.tick);
        if(caster->monster) {
            const auto sampled=ports_.effects.unitStatValues(actor.actor,actor.tick,weapon.item?std::optional{weapon.item}:std::nullopt);
            if(!sampled) return DomainStatus::Unavailable;
            weapon=sampleNativeWeapon(weapon,*sampled,*rules.stats,modifiers,true,0,
                caster->monster->equipmentStats?&*caster->monster->equipmentStats:nullptr,rules.formulas.get(),
                caster->monster->identity.monster=="shadowwarrior" || caster->monster->identity.monster=="shadowmaster");
        }
        auto contact=std::make_shared<combat::SpellImpact>();
        contact->projectile=contact->source=actor.actor;
        contact->weapon=prepareWeaponSkillProcedures(weapon,modifiers,attack,caster->level(),false);
        contact->suppressSourceEvents=true;
        auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &spec=pulse->spec;
        spec.source=actor.actor;spec.area=actor.area;spec.skill=skill;spec.rank=rank;
        spec.center=(player?player->position:monster->position)+ports_.areas.at(region).definition.origin-ports_.areas.at(actor.area).definition.origin;
        spec.radius=radius;spec.filter=0x8583;spec.directFunction=163;
        spec.hitFlags=uint32_t(row.number("hitflags"));spec.resultFlags=uint16_t(row.number("resultflags"))|5;
        if(skill==255 || skill==270 || skill==275) {
            // The native callback copies the allocated pCombat damage, after
            // reception, while the primary Drain is still on the stack.
            const combat::SpellImpact *primary=nullptr;
            for(const auto &candidate:ports_.combat.read().spells)
                if(candidate.source==actor.actor && candidate.nativeSkill && candidate.nativeSkill->kick &&
                    candidate.nativeSkill->skill==skill && candidate.next<candidate.targets.size() && candidate.targets[candidate.next]==pending.unit)
                    primary=&candidate;
            if(!primary || !primary->missileRolled || !primary->areaDamage) return DomainStatus::Unavailable;
            auto copied=primary->missileRolled->second;
            if(primary->playerContact) for(size_t channel=0;channel<copied.channels.size();++channel)
                copied.channels[channel]=nativeChecked(primary->playerContact->defense.damage.channels[channel].dealt);
            else if(primary->monsterContact) {
                const auto &allocated=*primary->monsterContact;
                if(!allocated.defense) return DomainStatus::Unavailable;
                for(size_t channel=0;channel<copied.channels.size();++channel)
                    copied.channels[channel]=nativeChecked(allocated.defense->damage.channels[channel].dealt);
                copied.channels[0]=nativeChecked(allocated.physical);
            }
            contact->nativeSkill=std::make_shared<combat::NativeSkillContact>();
            contact->nativeSkill->initialDamage=std::make_shared<MissileHitDamage>(std::move(copied));
            // pCombat retains its original HitFlags, independently of Drain's
            // local HitFlags 0x20 copy. Kicks allocated 0x23 (already filled).
            spec.hitFlags=0x23;spec.resultFlags=primary->areaDamage->resultFlags|5;
        }
        spec.missileContact=std::move(contact);
        spec.targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,radius,0x8583,false,
            spec.center+ports_.areas.at(actor.area).definition.origin,effects::NativeAuraTargetMode::TargetRange,actor.tick});
        std::erase(*spec.targets,pending.unit);
        pending.directSkill=std::move(pulse);
    }
    pending.nativeCallbackStarted=true;
    const auto result=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
    if(result) pending.nativeCallbackStarted=false;
    return result.status;
}
}
