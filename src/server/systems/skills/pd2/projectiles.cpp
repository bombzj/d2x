#include "../system.hpp"
#include "corpses.hpp"
#include "execution.hpp"
#include "../runtime.hpp"
#include "../evaluation.hpp"
#include "../native_caster.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <numbers>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2(Release &pending,const ActorContext &actor,Vec target) {
    if(!pending.skill.pd2) return DomainStatus::NotImplemented;
    const auto &program=*pending.skill.pd2;
    if(program.doFunction==177 || program.doFunction==179) return activatePd2BossSummon(pending,actor);
    if(program.doFunction==43) return activateAssassin(pending,actor,target);
    if(program.doFunction==161) return pd2Desecrate(ports_,pending,actor,target);
    if(program.doFunction==176) return activatePd2HolyLight(pending,actor);
    if(program.doFunction==162) return activatePd2Joust(pending,actor,target);
    if(program.doFunction==30) {
        pending.nativeCallbackStarted=true;
        const auto result=ports_.effects.pd2DarkPact(actor,pending.skill,target,pending.unit,pending.pd2DarkPact);
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    }
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    if(program.doFunction==165) {
        // Current Hunger (0x3B0330) eats a corpse and posts AuraState on
        // the caster. It no longer performs retail Hunger's melee hit.
        if(!pending.pulses) {
            if(!ports_.monsters.usableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
            constexpr int corpseNoDraw=104; // Native Do165's STATE_CORPSE_NODRAW.
            const auto hidden=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
                {StateFact{pending.unit,1,actor.area,corpseNoDraw,true}}});
            if(!hidden) return hidden.status;
            ports_.monsters.consumeCorpse(pending.unit);pending.pulses=1;
        }
        pending.nativeCallbackStarted=true;
        const auto applied=caster->monster?ports_.effects.unitSkill(actor.actor,pending.skill,actor.tick):ports_.effects.skill(actor,pending.skill);
        if(applied) pending.nativeCallbackStarted=false;
        return applied.status;
    }
    if(program.doFunction==163) return activatePd2Splash(pending,actor);
    const auto &rules=*caster->programs();const auto position=caster->position();
    const auto *equipment=caster->player?&caster->player->totals.equipment:
        caster->monster->equipmentStats?&*caster->monster->equipmentStats:nullptr;
    const auto finishShout=[&] {
        if(!pending.skill.appliedEffect) return DomainStatus::Applied;
        auto applied=pending.skill;applied.manaCost=applied.startMana=0;applied.charge.reset();
        pending.nativeCallbackStarted=true;
        const auto result=caster->monster?ports_.effects.unitSkill(actor.actor,applied,actor.tick):ports_.effects.skill(actor,applied);
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    };
    if(program.doFunction==68 && pending.manaPaid) return finishShout();
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto value=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
        if(!value) throw std::runtime_error("Missing PD2 projectile formula: "+std::string(field));
        return *value;
    };
    const auto &row=rules.formulas->skills.at(pending.skill.sourceId);
    std::vector<missiles::NativeSpawn> births;
    const auto append=[&](Vec origin,Vec destination,int definition) -> missiles::NativeSpawn & {
        births.push_back({actor.actor,actor.area,actor.areaGeneration,actor.tick,origin,destination,
            definition,pending.skill.sourceId,pending.skill.rank});
        if(pending.weapon) {
            WeaponSkillDamage weapon;weapon.weapon=*pending.weapon;weapon.level=caster->level();weapon.wearSkill=pending.skill;
            births.back().weapon=std::move(weapon);
        }
        return births.back();
    };
    switch(program.doFunction) {
    case 184:case 186: {
        if(!caster->monster) return DomainStatus::Unavailable;
        if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
        auto &prepared=pending.pd2Execution->bossProjectiles;
        if(!prepared) {
            Pd2CastExecution::BossProjectiles layout;layout.definition=program.missiles[0];
            if(program.doFunction==184) {
                const auto control=ports_.ai.read().controllers.find(actor.actor);
                if(control==ports_.ai.read().controllers.end()) return DomainStatus::Unavailable;
                if(control->second.phase==8) layout.definition=program.missiles[1];
                auto random=uint32_t(control->second.loop);
                int direction=int(pd2TaggedRandom(random)&63),remaining=64,gaps=5,wait=0,chance=15;
                while(remaining>0) {
                    if(gaps>0 && !wait && int32_t(pd2TaggedRandom(random))%100<chance) {
                        direction=(direction+6)&63;remaining-=6;wait=8;--gaps;chance-=2;
                    } else {
                        if(wait>0) --wait;
                        layout.points.emplace_back(position,position+missileRingDirection(direction));
                        direction=(direction+1)&63;--remaining;
                    }
                }
            } else {
                int count=formula("calc1"),radius=formula("calc2");
                if(caster->monster->identity.nativeClass==1112) {
                    const auto phase=ports_.maps.read().sceneCooldown;
                    if(phase==2) {count=nativeMultiply(count,2);radius=nativeAdd(radius,3);}
                    if(phase==3) {count=nativeMultiply(count,3);radius=nativeAdd(radius,6);}
                }
                if(count>65536) return DomainStatus::Capacity;
                for(int index=0;index<count;++index) {
                    const double angle=double(index)*2.*std::numbers::pi/double(count);
                    layout.points.emplace_back(Vec{float(int(std::cos(angle)*radius)+int(target.x)),
                        float(int(std::sin(angle)*radius)+int(target.y))},Vec{});
                }
            }
            prepared=std::move(layout);
        }
        if(prepared->definition<0) return DomainStatus::Unavailable;
        for(const auto &[from,to]:prepared->points) append(from,to,prepared->definition);
        if(!prepared->birth) {
            auto ready=ports_.missiles.prepareNativeBirth(births);if(!ready) return ready.status;
            prepared->birth=std::move(*ready.value);
        }
        pending.nativeCallbackStarted=true;
        const auto result=ports_.missiles.commitNativeBirth(prepared->birth,program.doFunction==186);
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    }
    case 143:
        // Core 3AE960 has a dedicated Do143 branch. Progressive143 is
        // still owned by the assassin executor, not this direct callback.
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        append(position,position,program.missiles[0]);
        break;
    case 164: {
        // 41d49d Do164 is 3B29B0 (installer 36BE3A), not the old
        // 17d5 poison-ring callback. It emits Calc1 missiles from the owner
        // toward the target, with the same 36F390 path callback as Firestorm.
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        const int count=formula("calc1");
        if(count<=0) return DomainStatus::Applied;
        if(count>512) return DomainStatus::Capacity;
        for(int index=0;index<count;++index) {
            auto &spawn=append(position,target,program.missiles[0]);
            spawn.weapon.reset();spawn.initialization=missiles::NativeSpawn::Initialization::Pd2Firestorm;
        }
        break;
    }
    case 175: {
        const int count=formula("calc2");
        if(count>512) return DomainStatus::Capacity;
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        for(int index=0;index<count;++index)
            append(position,target,program.missiles[0]).initialization=missiles::NativeSpawn::Initialization::Pd2Firestorm;
        break;
    }
    case 154:case 189: {
        if(program.doFunction==154 && caster->monster) {
            const auto action=caster->monster->rule.skillActions.find(pending.skill.sourceId);
            if(action==caster->monster->rule.skillActions.end() || !action->second.missile) return DomainStatus::Unavailable;
            auto &spawn=append(position,target,action->second.missile->definition);
            spawn.copyMonsterAttackBonus=true;
            spawn.initialDamagePercent=formula("calc1");spawn.finalParameters=std::array<int32_t,2>{formula("calc2"),0};
            break;
        }
        if(!pending.weapon || !pending.weapon->projectile) return DomainStatus::Unavailable;
        const int original=pending.weapon->projectile->id;
        const size_t slot=original==36?1:(original==1 || original==37 || original==371)?2:0;
        int missile=program.missiles[slot];if(missile<0) return DomainStatus::Unavailable;
        if(program.doFunction==189) missile+=original==37?1:original==371?2:0;
        if(missile<0) return DomainStatus::Unavailable;
        if(program.doFunction==154) {
            auto &spawn=append(position,target,missile);spawn.copyMonsterAttackBonus=true;
            spawn.initialDamagePercent=formula("calc1");spawn.finalParameters=std::array<int32_t,2>{formula("calc2"),0};
        } else {
            const int count=formula("calc1");if(count<=0) return DomainStatus::Unavailable;
            if(count>512) return DomainStatus::Capacity;
            const int central=formula("calc3")?formula("calc3"):count;
            const auto layout=pd2MissileFanLayout(position,target,count);const int left=std::max(0,(count-central)/2);
            for(int index=0;index<count;++index) {
                auto &spawn=append(position,layout.first+layout.step*float(index),missile);
                spawn.activateFrame=formula("calc2");spawn.initialDamagePercent=formula("calc4");
                spawn.suppressSourceEvents=index<left || index>=left+central;
            }
        }
        break;
    }
    case 80:
        if(program.missiles[0]<0 || !pending.unit) return DomainStatus::Unavailable;
        append(target,{},program.missiles[0]).finalParameters=std::array<int32_t,2>{pending.unitType,nativeNarrow(pending.unit.value)};
        break;
    case 166: {
        const int count=formula("calc1");
        if(count<=0 || program.missiles[0]<0) return DomainStatus::Unavailable;
        if(count>512) return DomainStatus::Capacity;
        for(int index=0;index<count;++index)
            append(position,target,program.missiles[0]).initialization=missiles::NativeSpawn::Initialization::Pd2Firestorm;
        break;
    }
    case 0:
        if(program.missile<0) return DomainStatus::NotImplemented;
        append(position,target,program.missile).distanceCountdown=row.number("lob")!=0;
        break;
    case 187:case 188: {
        const auto definition=program.missile>0?program.missile:program.missiles[0];
        if(definition<=0) return DomainStatus::Unavailable;
        if(program.doFunction==187 && !pending.unit) return DomainStatus::Unavailable;
        auto &spawn=append(position,target,definition);
        spawn.copyMonsterAttackBonus=program.doFunction==188;
        spawn.suppressSourceEvents=program.doFunction==187;
        if(program.doFunction==187) spawn.guidedTarget=pending.unit;
        break;
    }
    case 8: {
        // Current core RVA 0x3AC4A0; unlike retail, Calc3==0 leaves no
        // central arrows. Calc2 is activation delay, not missile lifetime.
        const int count=formula("calc1");
        if(count<=0) return DomainStatus::Unavailable;
        if(count>512) return DomainStatus::Capacity;
        int definition=program.missiles[0];
        if((!equipment || equipment->animationClass!="bow") && program.missiles[1]>=0) definition=program.missiles[1];
        if(definition<0) return DomainStatus::Unavailable;
        const int central=std::clamp(formula("calc3"),0,count),left=(count-central)/2;
        const int activate=formula("calc2"),rating=formula("calc4");
        const auto layout=pd2MissileFanLayout(position,target,count);
        for(int index=0;index<count;++index) {
            auto &spawn=append(position,layout.first+layout.step*float(index),definition);
            spawn.activateFrame=activate;spawn.suppressSourceEvents=index<left || index>=left+central;
            if(rating) spawn.attackBonus=rating;
        }
        break;
    }
    case 10: {
        int definition=program.missiles[0];
        if((!equipment || equipment->animationClass!="bow") && program.missiles[1]>=0) definition=program.missiles[1];
        if(definition<0) return DomainStatus::Unavailable;
        auto &spawn=append(position,target,definition);
        spawn.initialDamagePercent=formula("calc1");spawn.guidedTarget=pending.unit;
        spawn.distanceCountdown=!pending.unit;
        const auto dx=uint16_t(int32_t(std::floor(target.x))-int32_t(std::floor(position.x)));
        const auto dy=uint16_t(int32_t(std::floor(target.y))-int32_t(std::floor(position.y)));
        spawn.finalParameters=std::array<int32_t,2>{pending.unit?1:2,int32_t(uint32_t(dx)+(uint32_t(dy)<<16))};
        break;
    }
    case 12: {
        int definition=program.missiles[0];
        if((!equipment || equipment->animationClass!="bow") && program.missiles[1]>=0) definition=program.missiles[1];
        if(definition<0) return DomainStatus::Unavailable;
        append(position,target,definition).initialDamagePercent=formula("calc2");
        break;
    }
    case 17:case 118: {
        const int count=formula("calc1");
        if(count<=0) return DomainStatus::Unavailable;
        if(count>512) return DomainStatus::Capacity;
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        for(int index=0;index<count;++index) {
            auto &spawn=append(position,target,program.missiles[0]);
            spawn.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;spawn.initializationIndex=index;
        }
        break;
    }
    case 22:case 68:case 183: {
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        const auto &missile=rules.formulas->missiles.at(program.missiles[0]);
        const int velocity=program.doFunction==68 || program.doFunction==183?0:nativeAdd(formula("calc1"),
            nativeAdd(missile.number("vel"),nativeMultiply(missile.number("vellev"),pending.skill.rank)/8));
        const auto center=program.doFunction==183 && pending.unit?target:position;
        for(int direction=0;direction<64;++direction) {
            auto &spawn=append(center,center+missileRingDirection(direction),program.missiles[0]);
            spawn.pd2SharedClassHitDelay=program.doFunction==183;
            if(velocity) spawn.nativeVelocity=velocity;
        }
        break;
    }
    case 28:
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        if(!ports_.areas.at(actor.area).definition.collision.missileSegment(target,target,{5,1})) return DomainStatus::Unavailable;
        append(target,target,program.missiles[0]);break;
    case 24: {
        if(program.missiles[0]<0 || ports_.areas.at(actor.area).definition.town ||
            !ports_.areas.at(actor.area).definition.collision.missileSegment(target,target,{5,1})) return DomainStatus::Unavailable;
        const auto side=missileWallDirection(position,target);
        append(target,target+side,program.missiles[0]);append(target,target-side,program.missiles[0]);
        if(program.missiles[1]>=0) append(target,{},program.missiles[1]);
        break;
    }
    case 151:
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        append(pending.unit?target:position,target,program.missiles[0]).copyMonsterAttackBonus=true;
        births.back().finalParameters=std::array{formula("calc1"),0};break;
    case 26:
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        append(position,target,program.missiles[0]).finalParameters=std::array{formula("calc1"),0};break;
    case 73:
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        append(position,target,program.missiles[0]).initialization=missiles::NativeSpawn::Initialization::BlessedHammer;
        break;
    case 159: {
        // Current Do159 (RVA 0x3AF870) calls 0x36BAA0 with Calc2 as
        // the ring subdivision count and -1 as the velocity override.
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        int count=formula("calc2");if(count<0) return DomainStatus::Unavailable;
        if(!count || count>64) count=64;
        for(int direction=0;direction<64;direction+=64/count)
            append(position,position+missileRingDirection(direction),program.missiles[0]);
        break;
    }
    case 182: {
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        const auto frames=std::max(formula("calc1"),1),count=std::max(formula("calc3"),1);
        if(count>512) return DomainStatus::Capacity;
        const auto center=pd2MissileEndpoint(position,target,count/2+8);
        const auto layout=pd2MissileFanLayout(position,center,count);
        for(int index=0;index<count;++index)
            append(position,layout.first+layout.step*float(index),program.missiles[0]).totalFrames=frames;
        break;
    }
    case 169:
        if(program.missiles[0]<0) return DomainStatus::Unavailable;
        append(position,{},program.missiles[0]);break;
    default:return DomainStatus::NotImplemented;
    }
    DomainResult<std::shared_ptr<missiles::NativeBirthPublication>> prepared;
    if(program.doFunction==143) {
        if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
        auto &retained=pending.pd2Execution->royalFire;
        if(!retained) {
            prepared=ports_.missiles.preparePd2RoyalFire(births.front(),formula("calc1"));
            if(!prepared) return prepared.status;
            retained=*prepared.value;
        }
        prepared={DomainStatus::Applied,retained};
    } else prepared=ports_.missiles.prepareNativeBirth(births);
    if(!prepared) return prepared.status;
    if(caster->monster) {
        const auto committed=ports_.missiles.commitNativeBirth(*prepared.value);
        if(committed && program.doFunction==68) {pending.manaPaid=true;return finishShout();}
        return committed.status;
    }
    if(pending.weapon && pending.skill.weapon) {
        const bool payAmmo=(pending.weapon->ranged || pending.skill.weapon->thrown) && !(pending.skill.weapon->bow && pending.skill.weapon->bow->strafe);
        if(pending.manaPaid && (!payAmmo || pending.skill.weapon->noAmmo))
            return ports_.missiles.commitNativeBirth(*prepared.value).status;
        auto payment=ports_.inventory.weaponCost(actor,*pending.weapon,pending.skill,!pending.manaPaid,payAmmo);
        if(!payment) return payment.status;
        payment.value->missileBirth=std::move(*prepared.value);
        return ports_.transactions.commit(std::move(*payment.value)).status;
    }
    const auto &owner=*caster->player;
    transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
    if(!pending.manaPaid) {
        payment.player.mana-=pending.skill.manaCost;
        payment.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};payment.charge=pending.skill.charge;
    }
    auto plan=ports_.transactions.prepare(std::move(payment));if(!plan) return plan.status;
    plan.value->missileBirth=std::move(*prepared.value);
    const auto committed=ports_.transactions.commit(std::move(*plan.value));
    if(!committed) return committed.status;
    if(program.doFunction==68) {pending.manaPaid=true;return finishShout();}
    return committed.status;
}
}
