#include "../system.hpp"
#include "../native_area.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/runtime/skill_rules.hpp"

namespace d2x::server::effects {
DomainResult<> System::advancePd2EquippedStorm(const ActorContext &actor,const SkillCastSpec &skill,StormPulse &pulse) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster) return done();
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!pulse.prepared) {
        pulse.skill=skill;
        if(ports_.areas.at(actor.area).definition.town) return done();
        const auto last=stormLast_[{actor.actor,skill.sourceId}];EntityId next,fallback;
        for(const auto candidate:nativeAuraCandidateRecipients({actor.actor,actor.area,skill.stormRadius,0xA783,false,{},NativeAuraTargetMode::OwnerRange,actor.tick})) {
            if(candidate.value>uint32_t(last.value) && (!next || candidate<next)) next=candidate;
            if(candidate.value<=uint32_t(last.value) && (!fallback || candidate<fallback)) fallback=candidate;
        }
        pulse.target=next?next:fallback;
        if(pulse.target) {
            const auto victim=participants.find(pulse.target);pulse.position=victim.position();pulse.area=victim.player?victim.player->area:victim.monster->area;
        }
        pulse.prepared=true;
    }
    if(pulse.target && !pulse.launched) {
        const auto victim=participants.find(pulse.target);
        const auto *area=ports_.areas.find(pulse.area);
        if(victim.id() && area && !area->definition.town) {
            missiles::NativeSpawn spawn{actor.actor,pulse.area,area->generation,actor.tick,pulse.position,pulse.position,skill.missileId,skill.sourceId,skill.rank};
            spawn.transient=true;
            const auto born=ports_.missiles.spawnNative(spawn);if(!born) return {born.status,{}};
            pulse.projectile=*born.value;
            const auto contact=ports_.missiles.nativeAreaContact(pulse.projectile,true);
            if(!contact) return {DomainStatus::Unavailable,{}};
            pulse.damage=std::make_shared<NativeAreaPulse>();auto &spec=pulse.damage->spec;
            spec.source=actor.actor;spec.area=pulse.area;spec.center=pulse.position;spec.skill=skill.sourceId;spec.rank=skill.rank;
            spec.targets=std::vector<EntityId>{pulse.target};spec.missileContact=std::make_shared<combat::SpellImpact>(*contact);
            spec.rollMissileDamage=true;spec.resultFlags=1;
            pulse.notices.emplace_back(SkillPulseFact{actor.actor,pulse.target,uint8_t(caster->monster?1:0),uint8_t(victim.player?0:1),pulse.area,skill.sourceId,skill.rank,pulse.position});
        }
        pulse.launched=true;
    }
    if(pulse.projectile && !pulse.damaged) {
        const auto damaged=nativeArea(pulse.damage->spec,actor.tick,pulse.damage);if(!damaged) return damaged;
        ports_.missiles.removeTransient(pulse.projectile);pulse.damaged=true;
    }
    if(!pulse.notified) {
        if(!pulse.notices.empty()) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},pulse.area},pulse.notices});if(!sent) return {sent.status,{}};
        }
        stormLast_[{actor.actor,skill.sourceId}]=pulse.target;pulse.notified=true;
    }
    return done();
}
}
