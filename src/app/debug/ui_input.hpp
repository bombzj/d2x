#pragma once
#include "presentation/input.hpp"
#include <nlohmann/json_fwd.hpp>
#include <vector>
namespace d2x {
// Existing diagnostic UI input format. It uses normal controller intents.
std::vector<FrameInput> parseDebugInput(const nlohmann::json &);
}
