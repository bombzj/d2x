#pragma once
#include "contracts/online_scene.hpp"
#include "network/realm_session.hpp"
#include <functional>
namespace d2x {
class RemoteControl;
class RemoteInventory;
class RemoteCombat;
std::string onlineDebugCommand(const std::string &, net::RealmSession &,
                               const std::function<net::LoginOptions()> &loginConfiguration, bool &quit,
                               const std::function<void(const std::string &)> &screenshot,
                               const std::function<OnlineSceneView()> &scene,
                               RemoteControl &, RemoteInventory &, RemoteCombat &,
                               const std::function<void(bool, bool)> &automap);
}
