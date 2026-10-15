#include "life_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
MonsterLifeBounds monsterLifeBounds(const MonsterLifeTable &table,int level,int difficulty,int gameType) {
    if(level<0 || !table.levels || table.levels->empty()) throw std::invalid_argument("Missing native monster life inputs");
    const auto row=table.levels->find(std::min(level,table.levels->rbegin()->first));
    if(row==table.levels->end()) throw std::runtime_error("Missing native monster life level");
    const auto ratio=row->second.at(size_t(std::clamp(difficulty,0,2)+(gameType?3:0)));
    return table.noRatio?MonsterLifeBounds{table.minimum,table.maximum}:
        MonsterLifeBounds{nativePercentage(ratio,table.minimum,100),nativePercentage(ratio,table.maximum,100)};
}
}
