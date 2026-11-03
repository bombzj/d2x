#pragma once
#include "server/player_identity.hpp"
#include <string_view>
#include <string>
#include <vector>
namespace d2x::server::companions { struct Prepared; }
namespace d2x {
class GameHost;
class Archives;
struct ClassicData;
struct LootContent;
struct ItemInstance;
enum class ItemQuality;

std::vector<std::string> preparePendingSummons(GameHost &,GameHandle,Archives &,const ClassicData &);
void preparePendingHirelings(GameHost &,GameHandle,Archives &,const ClassicData &,LootContent &);
ItemInstance prepareSummonEquipment(const ClassicData &,server::companions::Prepared &,std::string_view,ItemQuality,int level);
}
