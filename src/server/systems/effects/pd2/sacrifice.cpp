#include "sacrifice.hpp"
#include "../system.hpp"
#include "../native_area.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/monsters/life_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace d2x::server::effects {
struct Pd2SacrificeTimers {
    struct Pulse {
        EntityId source,corpse;int32_t overkill{};
        std::shared_ptr<NativeAreaPulse> damage;
        bool prepared{},marked{},notified{};
    };
    std::map<FrameEventHandle,Pulse> pulses;
};
DomainResult<> System::pd2SacrificeKill(EntityId source,EntityId target,int32_t overkill,int damageSkill,uint64_t tick) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source);
    const auto *body=ports_.monsters.find(target);
    if(!caster || !caster->programs() || !caster->programs()->pd2 || !body || body->life>0)
        return {DomainStatus::Applied,std::monostate{}};
    const auto used=ports_.actions.usedSkill(source);
    if(damageSkill!=96 && (!used || used->skill!=96)) return {DomainStatus::Applied,std::monostate{}};
    const auto marked=setNativeSkillState(target,223,true,tick);if(!marked) return marked;
    if(!pd2Sacrifice_) pd2Sacrifice_=std::make_shared<Pd2SacrificeTimers>();
    // ExecuteDamage delays the explosion until the death animation's last
    // six frames, clamped to 3..30. A corpse with no animation uses three.
    const auto delay=std::clamp(body->rule.deathTicks-6,3,30);
    auto timer=ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,target},FrameEventType::MonsterModifier,
        tick+uint64_t(delay),false,pd2SacrificeTimerHandler});
    pd2Sacrifice_->pulses.emplace(timer.handle(),Pd2SacrificeTimers::Pulse{source,target,int32_t(uint32_t(std::max(overkill,0))&0x0fffffff),{},false,false,false});
    ports_.frameEvents.publish(std::move(timer));return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::pd2SacrificeEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!pd2Sacrifice_) return done();
    const auto found=pd2Sacrifice_->pulses.find(event.handle);if(found==pd2Sacrifice_->pulses.end()) return done();
    auto &pulse=found->second;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,pulse.source);
    const auto *body=ports_.monsters.find(pulse.corpse);
    const auto finish=[&] {ports_.frameEvents.cancel(event.handle);pd2Sacrifice_->pulses.erase(found);return done();};
    if(!caster || !caster->programs() || !body) return finish();
    const auto &row=caster->programs()->formulas->skills.at(96);
    if(!pulse.prepared) {
        auto input=caster->formulaInput(*this,tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        auto radius=row.number("param4");
        if(const auto rank=input.referenceRanks->effective.find(96);rank!=input.referenceRanks->effective.end()) {
            const auto value=values.calculation(FormulaDomain::Skill,96,rank->second,"calc3");
            if(!value) throw std::logic_error("Missing PD2 Sacrifice explosion radius");
            radius=*value;
        }
        const auto &table=body->rule.lifeTables.at(size_t(body->rule.difficulty));
        if(!table) throw std::logic_error("Missing PD2 Sacrifice corpse life table");
        const auto life=monsterLifeBounds(*table,body->rule.level,body->rule.difficulty,body->rule.statGameType);
        const auto average=nativeShiftLeft(nativeAdd(life.minimum,life.maximum),7);
        const auto percent=[](int value,int factor){const auto amount=double(value)*double(factor)/100.0;
            return amount<double(INT32_MIN) || amount>double(INT32_MAX)?INT32_MIN:int32_t(amount);};
        pulse.damage=std::make_shared<NativeAreaPulse>();auto &area=pulse.damage->spec;
        area.source=pulse.source;area.area=body->area;area.center=body->position;area.radius=radius;
        area.skill=96;area.rank=1;area.filter=0x8583;area.resultFlags=1;
        area.damage.channels[0]=nativeAdd(percent(average,row.number("param5")),percent(pulse.overkill,row.number("param6")));
        pulse.prepared=true;
    }
    if(!pulse.marked) {const auto cleared=setNativeSkillState(pulse.corpse,223,false,tick);if(!cleared) return cleared;pulse.marked=true;}
    if(!pulse.notified) {
        const auto missile=ports_.missiles.nativeDefinition(row.reference("srvmissilea"));
        if(!missile) throw std::logic_error("Missing PD2 Sacrifice explosion visual");
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body->area},
            {MissileFact{pulse.corpse,1,body->area,*missile,1,0,body->position,body->position}}});
        if(!sent) return {sent.status,{}};
        pulse.notified=true;
    }
    const auto damaged=nativeArea(pulse.damage->spec,tick,pulse.damage);if(!damaged) return damaged;
    return finish();
}
}
