#pragma once
#include "resources/archive.hpp"
#include "options.hpp"
#include <raylib.h>
namespace d2x {
void runFrontend(Archives &, RenderTexture2D, const AppOptions &);
}
