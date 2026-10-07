#pragma once
#include "hosting/administration.hpp"
#include <functional>
#include <nlohmann/json_fwd.hpp>

namespace d2x {
class EmbeddedRealm;
// JSON and pipe concerns stay in the app. Original-server connections pass null;
// this capability is never obtained through the ordinary gameplay transport.
std::optional<std::string> serverDebugCommand(const nlohmann::json &, EmbeddedRealm *,
    const std::function<hosting::AdminResult(const hosting::AdminRequest &)> &);
}
