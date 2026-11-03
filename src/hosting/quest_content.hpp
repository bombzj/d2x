#pragma once
#include "server/player_identity.hpp"

namespace d2x {
class GameHost;
struct ClassicData;
class Archives;
void prepareQuests(GameHost &,GameHandle,Archives &,const ClassicData &);
}
