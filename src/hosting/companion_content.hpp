#pragma once
#include "game_host.hpp"
#include <string>
#include <vector>
namespace d2x {struct ClassicData;class Archives;std::vector<std::string> preparePendingSummons(GameHost &,GameHandle,Archives &,const ClassicData &);}
namespace d2x {struct LootContent;void preparePendingHirelings(GameHost &,GameHandle,Archives &,const ClassicData &,LootContent &);}
