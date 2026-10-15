#pragma once
#include "gameplay/monsters/types.hpp"
#include "resources/data_table.hpp"
#include <memory>
namespace d2x {
std::shared_ptr<const MonsterTypeCatalog> loadMonsterTypeCatalog(const DataTable &monstats,const DataTable &montype,const DataTable &monstats2);
}
