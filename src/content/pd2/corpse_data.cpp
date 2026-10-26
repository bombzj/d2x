#include "corpse_data.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_identity.hpp"
#include "content/skills/table_values.hpp"
#include <algorithm>
#include <set>
#include <string_view>
#include <string>
#include <cctype>
#include <stdexcept>

namespace d2x {
std::vector<std::array<int,25>> preparePd2CorpsePools(const ClassicData &data,int level,int difficulty) {
    const auto &levels=data.tables.at("levels"),&monsters=data.tables.at("monstats");
    // Current core 0x190C70 initializes Do161's exclusion set. Resolve the
    // authored identities against this profile, rather than assuming hcIdx.
    constexpr std::array exclusions{
        "frogdemon1","frogdemon2","frogdemon3","frogdemonThrone",
        "ancientbarb1","ancientbarb2","ancientbarb3","firetowerMarket",
        "bigheadMaus","bloodlordMaus","overseerMaus","pantherwomanMaus",
        "reanimatedMaus","siegebeast3Maus","slingerMaus","succubusMaus",
        "vampire2Maus","willowisp1Maus","voidWatcher","voidWatcherLimb",
        "voidBeast","voidling","voidFrog","voidKnightCorridor",
        "overseerRat","unraveler3Rat","skeleton4Rat","bonefetish4Rat",
        "mummy5Rat","willowisp1Rat","skmage_fire3Rat","minion5Rat",
        "overseerRat2","unraveler3Rat2","skeleton4Rat2","bonefetish4Rat2",
        "mummy5Rat2","willowisp1Rat2","skmage_fire3Rat2","minion5Rat2"};
    const auto linkedName=[](std::string_view value) {
        std::string result(value);
        for(auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        return result;
    };
    std::set<std::string> excluded;
    for(const auto name:exclusions) excluded.insert(linkedName(name));
    std::vector<std::array<int,25>> result;
    // Current core 0x190D60: fallback Levels IDs 6,66,76,106,132.
    for(const int id:std::array{level,6,66,76,106,132}) {
        std::array<int,25> pool;pool.fill(-1);
        const auto row=findSkillTableRow(levels,"Id",std::to_string(id));
        for(size_t slot=0;slot<pool.size();++slot) {
            const auto name=levels.value(row,(difficulty?"nmon":"mon")+std::to_string(slot+1));
            if(name.empty() || excluded.contains(linkedName(name))) continue;
            size_t monster=0;
            while(monster<monsters.rows().size() && linkedName(monsters.value(monster,"Id"))!=linkedName(name)) ++monster;
            if(monster==monsters.rows().size()) throw std::runtime_error("Missing PD2 corpse pool monster: "+std::string(name));
            if(monsters.number(monster,"PartyMin").value_or(0) || monsters.number(monster,"PartyMax").value_or(0) ||
               !monsters.value(monster,"spawn").empty()) continue;
            pool[slot]=compiledMonsterIndex(monsters,monster);
        }
        result.push_back(pool);
        if(result.size()==1 && std::any_of(pool.begin(),pool.end(),[](int entry){return entry>=0;})) break;
    }
    return result;
}
}
