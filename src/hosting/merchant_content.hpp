#pragma once
#include "server/player_identity.hpp"

namespace d2x {
class GameHost;
struct ClassicData;
void prepareMerchant(GameHost &,GameHandle,const ClassicData &);
}
