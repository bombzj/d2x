#pragma once
#include "contracts/online_scene.hpp"
#include <memory>
#include <functional>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace d2x {
class Archives;
class DebugAutoWalk;
class RemoteTown;
class RemoteControl;
class RemoteCombat;
class RemoteInventory;
namespace net {class RealmSession;}
// High-level debug input orchestration. Owns intentions and observations only.
class DebugAi {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct SuppliesView {
        std::optional<int> maxLife,maxMana;
        std::optional<bool> castInputBusy;
        std::optional<bool> actionInputBusy;
        std::function<std::optional<bool>(uint16_t)> skillUsable;
        std::function<std::optional<unsigned>(uint32_t)> buyQuote;
        std::function<bool()> openQuestLog;
        std::function<void()> closeQuestLog;
        std::span<const OnlineSceneView::MissileDisplay> missiles;
    };
    explicit DebugAi(Archives &,std::filesystem::path logDirectory="logs");
    ~DebugAi();
    bool active() const;
    bool hasPlan() const;
    bool controlsMovement() const;
    bool controlsInventory() const;
    // Player input releases a live AI plan only through Escape in the game view.
    void manualInput(net::RealmSession &,DebugAutoWalk &,RemoteControl &,RemoteCombat &);
    std::optional<std::string> command(const nlohmann::json &,net::RealmSession &,RemoteTown &,
        DebugAutoWalk &,RemoteControl &,RemoteCombat &,RemoteInventory &);
    void tick(net::RealmSession &,RemoteTown &,DebugAutoWalk &,RemoteControl &,RemoteCombat &,
        RemoteInventory &,const SuppliesView &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
  private:
    void tickPlan(net::RealmSession &,RemoteTown &,DebugAutoWalk &,RemoteControl &,RemoteCombat &,
        RemoteInventory &,const SuppliesView &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
};
}
