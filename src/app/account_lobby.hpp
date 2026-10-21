#pragma once
#include <raylib.h>
namespace d2x {
class Archives;
class StartupProfile;
struct AppOptions;
void runAccountLobby(Archives &, RenderTexture2D &, const AppOptions &, StartupProfile &);
}
