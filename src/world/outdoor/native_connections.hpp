#pragma once
#include "world/outdoor/native_act_layout.hpp"

namespace d2x {
void connectNativeLevels(const WorldCatalog &, NativeActLayout &, int source, int destination);
void finalizeNativeConnections(const WorldCatalog &, NativeActLayout &, int act);
} // namespace d2x
