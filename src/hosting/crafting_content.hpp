#pragma once
#include "server/player_identity.hpp"

namespace d2x {
class GameHost;
struct ClassicData;
void prepareCrafting(GameHost &, GameHandle, const ClassicData &);
}
