#pragma once
#include "contracts/online_scene.hpp"
#include <memory>
#include <functional>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <nlohmann/json_fwd.hpp>

namespace d2x {
class Archives;
class OnlineObservation;
class DebugAutoWalk;
class RemoteTown;
class IOnlineControl;
class IOnlineCombat;
class IOnlineInventory;
// High-level debug input orchestration. Owns intentions and observations only.
class DebugAi {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct SuppliesView {
        std::optional<int> maxLife,maxMana;
        std::optional<bool> castInputBusy;
        std::optional<bool> actionInputBusy;
        std::optional<float> movementSecondsPerUnit;
        std::function<std::optional<std::pair<uint64_t,float>>(uint16_t)> observedCastSeconds;
        std::optional<int> navigationFasterCast;
        unsigned navigationWeaponSet{};
        std::function<std::optional<bool>(uint16_t)> skillUsable;
        // Observed skill/resource eligibility before the current town gate;
        // advisory future field legs only, never an input authorization.
        std::function<std::optional<bool>(uint16_t)> skillFieldUsable;
        std::function<std::optional<unsigned>(uint32_t)> buyQuote;
        std::function<bool(uint32_t source,uint32_t item,uint64_t itemRevision)> submitStaff;
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
    void manualInput(DebugAutoWalk &,IOnlineControl &,IOnlineCombat &);
    std::optional<std::string> command(const nlohmann::json &,const OnlineObservation &,const RemoteTown &,
        DebugAutoWalk &,IOnlineControl &,IOnlineCombat &);
    void tick(const OnlineObservation &,const RemoteTown &,DebugAutoWalk &,IOnlineControl &,IOnlineCombat &,
        IOnlineInventory &,const SuppliesView &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
  private:
    void tickPlan(const OnlineObservation &,const RemoteTown &,DebugAutoWalk &,IOnlineControl &,IOnlineCombat &,
        IOnlineInventory &,const SuppliesView &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
};
}
