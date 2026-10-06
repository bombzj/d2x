#pragma once
#include "resources/archive.hpp"
#include "options.hpp"
#include <raylib.h>
namespace d2x {
void runOnlineFrontend(Archives &, RenderTexture2D, const AppOptions &);
}
