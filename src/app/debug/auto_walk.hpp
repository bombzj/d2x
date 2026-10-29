#pragma once
#include "contracts/online_scene.hpp"
#include <chrono>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <string>

namespace d2x {
class RemoteTown;
class RemoteControl;
class RemoteCombat;
namespace net {class RealmSession;}
// Explicit debug input driver. Never reads host state or writes replica positions.
class DebugAutoWalk {
    using Clock=std::chrono::steady_clock;
    enum class Goal {Point,Unit,Waypoint,Exit} goal_{};
    std::string state_="idle",reason_,source_;
    uint64_t game_{},area_{};
    std::optional<uint16_t> level_;
    std::optional<uint16_t> destination_;
    std::optional<OnlinePoint> point_,segment_,previous_,client_,server_,edgeStart_,edgeEnd_;
    std::optional<OnlinePoint> lastFrom_,lastRequest_;
    std::optional<uint16_t> exitLevel_;
    std::optional<OnlineUnitKey> unit_;
    std::set<std::pair<int,int>> visited_;
    Clock::time_point deadline_,progressAt_,next_,submittedAt_;
    unsigned moves_{},interactions_{},searches_{},stationaryAttempts_{},decisions_{};
    bool run_=true,interact_{},landmark_{},clientMoving_{};
    void finish(std::string,std::string,RemoteControl &);
    nlohmann::json status() const;
  public:
    bool active() const;
    nlohmann::json snapshot() const;
    std::optional<std::string> command(const nlohmann::json &,net::RealmSession &,RemoteTown &,RemoteControl &,RemoteCombat &);
    void tick(net::RealmSession &,RemoteTown &,RemoteControl &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
    void manualInput(RemoteControl &);
};
}
