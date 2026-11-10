#include "walk_contract.hpp"
#include <nlohmann/json.hpp>

namespace d2x {
std::string_view debugWalkName(DebugWalkGoal goal) {
    switch(goal) {
    case DebugWalkGoal::None:return "";
    case DebugWalkGoal::Point:return "point";
    case DebugWalkGoal::Unit:return "unit";
    case DebugWalkGoal::Waypoint:return "waypoint";
    case DebugWalkGoal::Exit:return "exit";
    }
    return "";
}
std::string_view debugWalkName(DebugWalkState state) {
    switch(state) {
    case DebugWalkState::Idle:return "idle";
    case DebugWalkState::Searching:return "searching";
    case DebugWalkState::Moving:return "moving";
    case DebugWalkState::Interacting:return "interacting";
    case DebugWalkState::Completed:return "completed";
    case DebugWalkState::Failed:return "failed";
    case DebugWalkState::Cancelled:return "cancelled";
    case DebugWalkState::NeedsObservation:return "needs-observation";
    case DebugWalkState::NeedsReplan:return "needs-replan";
    }
    return "";
}
bool recoverableDebugWalkFailure(DebugWalkFailure failure) {
    switch(failure) {
    case DebugWalkFailure::NoProgress:
    case DebugWalkFailure::NoReachableSegment:
    case DebugWalkFailure::InteractionUnanswered:
    case DebugWalkFailure::ApproachUnanswered:return true;
    default:return false;
    }
}
void to_json(nlohmann::json &out,const DebugWalkIntent &intent) {
    if(!intent) {out=nullptr;return;}
    out={{"goal",debugWalkName(intent.goal)}};
    if(intent.point) {out["x"]=intent.point->x;out["y"]=intent.point->y;}
    if(intent.unit) {out["unitType"]=intent.unit->type;out["unitId"]=intent.unit->id;}
    const auto put=[&](const char *key,const auto &value) {if(value) out[key]=*value;};
    put("level",intent.level);put("interact",intent.interact);put("run",intent.run);
    put("exploration",intent.exploration);put("exactPoint",intent.exactPoint);
    put("recoverableRelay",intent.recoverableRelay);put("adjustLane",intent.adjustLane);
    put("forceApproach",intent.forceApproach);put("resyncApproach",intent.resyncApproach);
    put("changeStance",intent.changeStance);put("waitQuestAlert",intent.waitQuestAlert);
    put("confirmMenu",intent.confirmMenu);put("localCrossing",intent.localCrossing);
    put("purpose",intent.purpose);put("waypointArea",intent.waypointArea);
    put("activation",intent.activation);put("directTeleport",intent.directTeleport);
    put("itemId",intent.itemId);put("groundX",intent.groundX);put("groundY",intent.groundY);
    put("timeoutSeconds",intent.timeoutSeconds);
    if(intent.prepared) out["command"]="debug-walk";
}
} // namespace d2x
