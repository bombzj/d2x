#pragma once
#include "network/realm_session.hpp"
#include <functional>
namespace d2x {
std::string onlineDebugCommand(const std::string &, net::RealmSession &,
                               const std::function<net::LoginOptions()> &loginConfiguration, bool &quit,
                               const std::function<void(const std::string &)> &screenshot);
}
