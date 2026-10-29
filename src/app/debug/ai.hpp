#pragma once
#include "contracts/online_scene.hpp"
#include <memory>
#include <functional>
#include <optional>
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
        std::function<std::optional<unsigned>(uint32_t)> buyQuote;
    };
    explicit DebugAi(Archives &);
    ~DebugAi();
    bool active() const;
    bool hasPlan() const;
    bool controlsMovement() const;
    void manualInput(net::RealmSession &,DebugAutoWalk &,RemoteControl &,RemoteCombat &);
    std::optional<std::string> command(const nlohmann::json &,net::RealmSession &,RemoteTown &,
        DebugAutoWalk &,RemoteControl &,RemoteCombat &,RemoteInventory &);
    void tick(net::RealmSession &,RemoteTown &,DebugAutoWalk &,RemoteControl &,RemoteCombat &,
        RemoteInventory &,const SuppliesView &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
};
}
