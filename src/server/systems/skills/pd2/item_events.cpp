#include "../system.hpp"
#include "../runtime.hpp"
#include "execution.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "../native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/systems/reactions/pd2/item_events.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/pd2/cast_events.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace d2x::server::skills {
void System::postPd2MonsterCastItemEvent(MonsterRelease &pending,uint64_t tick) {
    if(pending.pd2CastEventPosted) return;
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->programs() || !caster->programs()->pd2) return;
    pending.pd2CastEventPosted=true;
    const auto &area=ports_.areas.at(caster->area()).definition;
    if(area.town || pd2::itemCastSafeZone(int(caster->area()),caster->position()+area.origin)) return;
    constexpr std::array excluded{277,57,219,220,514,217,218,513};
    constexpr std::array included{242,251,256,261,262,313,272,276,392};
    const auto skill=pending.request.skill;
    const auto contains=[&](const auto &set) {return std::find(set.begin(),set.end(),skill)!=set.end();};
    // Core 0x36E240 uses the monster's actual mode, without the player's
    // SQ6/12/18 restriction. Its forced set also admits non-SC/SQ modes.
    if(!contains(included) && !((pending.request.monsterMode==7 || pending.request.monsterMode==14) && !contains(excluded))) return;
    const auto target=std::get<UnitTarget>(pending.request.target);
    auto timer=ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,caster->id()},
        FrameEventType::MonsterModifier,tick+5,false,reactions::pd2ItemEventTimerHandler,14,int32_t(target.type),target.id.value});
    // The native wrapper posts before SrvDo, including a failed callback.
    // Retain the posted phase across domain backpressure retries.
    ports_.frameEvents.publish(std::move(timer));
}
DomainStatus System::preparePd2CastItemEvent(Release &pending,const ActorContext &actor,Vec) {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->rules.skills->pd2) return DomainStatus::Applied;
    const auto &area=ports_.areas.at(actor.area).definition;
    if(area.town || pd2::itemCastSafeZone(int(actor.area),player->position+area.origin)) return DomainStatus::Applied;
    // Core 0x190E20 / 0x190E90 initialize the forced and excluded sets.
    constexpr std::array excluded{277,57,219,220,514,217,218,513};
    constexpr std::array included{242,251,256,261,262,313,272,276,392};
    if(std::find(excluded.begin(),excluded.end(),pending.skill.sourceId)!=excluded.end()) return DomainStatus::Applied;
    const auto &record=player->rules.skills->formulas->skills.at(pending.skill.sourceId);
    const auto animation=record.reference("anim");
    const auto sequence=record.number("seqnum");
    if(std::find(included.begin(),included.end(),pending.skill.sourceId)==included.end() &&
        animation!="sc" && !(animation=="sq" && (sequence==6 || sequence==12 || sequence==18))) return DomainStatus::Applied;
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    if(pending.pd2Execution->castItemTimer) return DomainStatus::Applied;
    pending.pd2Execution->castItemTimer=ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Player,actor.actor},
        FrameEventType::MonsterModifier,actor.tick+5,false,reactions::pd2ItemEventTimerHandler,14,int32_t(pending.unitType),pending.unit.value});
    return DomainStatus::Applied;
}
void System::commitPd2CastItemEvent(Release &pending) noexcept {
    if(!pending.pd2Execution || !pending.pd2Execution->castItemTimer) return;
    ports_.frameEvents.publish(std::move(*pending.pd2Execution->castItemTimer));
    pending.pd2Execution->castItemTimer.reset();
}
}
