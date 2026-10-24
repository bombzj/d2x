#include "../system.hpp"
#include "../runtime.hpp"
#include "execution.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/systems/reactions/pd2/item_events.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace d2x::server::skills {
namespace {
bool itemCastSafeZone(int level,Vec position) {
    // Current core 0x37B650 uses two fixed PvP rectangles, not a DT1 bit.
    const auto x=uint32_t(int32_t(std::floor(position.x))),y=uint32_t(int32_t(std::floor(position.y)));
    return (level==157 && x-0x50b0u<=0x2au && y-0x281cu<=0x36u) ||
        (level==159 && x-0x4ff9u<=0x2cu && y-0x2d79u<=0x1fu);
}
}
DomainStatus System::preparePd2CastItemEvent(Release &pending,const ActorContext &actor,Vec) {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->rules.skills->pd2) return DomainStatus::Applied;
    const auto &area=ports_.areas.at(actor.area).definition;
    if(area.town || itemCastSafeZone(int(actor.area),player->position+area.origin)) return DomainStatus::Applied;
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
