#pragma once
#include "gameplay/stats/stat_list.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
namespace d2x {
struct MonsterModePhysical {int minimum{},maximum{},rating{};};
struct MonsterLevelRatio {int damage{},rating{};};
using MonsterModeLevelRatios=std::map<int,std::array<MonsterLevelRatio,6>>;
struct MonsterModeElement {
    uint8_t mode{};
    std::string type;
    int chance{},minimum{},maximum{},duration{};
};
struct MonsterModeTable {
    bool noRatio{};
    std::array<MonsterModePhysical,3> physical;
    std::array<MonsterModeElement,3> elements;
    std::shared_ptr<const MonsterModeLevelRatios> levels;
};
struct MonsterModeInput {
    uint8_t mode{};
    int level{},difficulty{},gameType{};
    unsigned playerCount{1};
    bool evil{};
    MonsterModePhysical ownerSkill;
    EntityId owner;
    uint64_t occurrence{};
};
std::shared_ptr<const StatList> prepareMonsterModeStats(const MonsterModeTable &,const StatRuleCatalog &,const MonsterModeInput &,uint64_t &random);
}
