#include "mode_stat_data.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace d2x {
namespace {
int number(const DataTable &table,size_t row,const std::string &field) {
    if(!table.has(field)) throw std::runtime_error("Missing native monster stat column: "+field);
    if(const auto value=table.number(row,field)) return *value;
    if(table.value(row,field).empty()) return 0; // Native TXT field compilation default.
    throw std::runtime_error("Invalid native monster stat cell: "+field);
}
}
std::shared_ptr<const MonsterModeLevelRatios> loadMonsterModeLevelRatios(const DataTable &table) {
    auto result=std::make_shared<MonsterModeLevelRatios>();
    const std::array<std::string_view,3> suffix{"","(N)","(H)"};
    for(size_t row=0;row<table.rows().size();++row) {
        const auto level=table.number(row,"Level");if(!level || *level<0) throw std::runtime_error("Invalid native MonLvl identity");
        std::array<MonsterLevelRatio,6> entry{};
        for(int group=0;group<2;++group) for(size_t difficulty=0;difficulty<3;++difficulty) {
            const auto prefix=group?"L-":"";
            entry[size_t(group*3)+difficulty]={number(table,row,std::string(prefix)+"DM"+std::string(suffix[difficulty])),
                number(table,row,std::string(prefix)+"TH"+std::string(suffix[difficulty]))};
        }
        if(!result->emplace(*level,entry).second) throw std::runtime_error("Duplicate native MonLvl identity");
    }
    if(result->empty()) throw std::runtime_error("Missing native MonLvl ratios");
    return result;
}
std::shared_ptr<const MonsterLifeLevelRatios> loadMonsterLifeLevelRatios(const DataTable &table) {
    auto result=std::make_shared<MonsterLifeLevelRatios>();
    const std::array<std::string_view,3> suffix{"","(N)","(H)"};
    for(size_t row=0;row<table.rows().size();++row) {
        const auto level=table.number(row,"Level");
        if(!level || *level!=int(row)) throw std::runtime_error("Non-indexed native MonLvl life identity");
        std::array<int,6> entry{};
        for(int group=0;group<2;++group) for(size_t difficulty=0;difficulty<3;++difficulty)
            entry[size_t(group*3)+difficulty]=number(table,row,std::string(group?"L-HP":"HP")+std::string(suffix[difficulty]));
        result->emplace(*level,entry);
    }
    if(result->empty()) throw std::runtime_error("Missing native MonLvl life ratios");
    return result;
}
std::shared_ptr<const MonsterLifeTable> loadMonsterLifeTable(const DataTable &table,size_t row,int difficulty,std::shared_ptr<const MonsterLifeLevelRatios> levels) {
    if(difficulty<0 || difficulty>2 || !levels) throw std::invalid_argument("Invalid native monster life table context");
    const std::array<std::string_view,3> suffix{"","(N)","(H)"};
    const auto ending=std::string(suffix[size_t(difficulty)]);
    auto result=std::make_shared<MonsterLifeTable>();result->levels=std::move(levels);result->noRatio=number(table,row,"noRatio")!=0;
    result->minimum=number(table,row,"minHP"+ending);result->maximum=number(table,row,"maxHP"+ending);
    return result;
}
std::shared_ptr<const MonsterModeTable> loadMonsterModeTable(const DataTable &table,size_t row,int difficulty,std::shared_ptr<const MonsterModeLevelRatios> levels) {
    if(difficulty<0 || difficulty>2 || !levels) throw std::invalid_argument("Invalid native monster mode table context");
    const std::array<std::string_view,3> suffix{"","(N)","(H)"};
    const auto ending=std::string(suffix[size_t(difficulty)]);
    const auto field=[&](const std::string &name){return number(table,row,name+ending);};
    auto result=std::make_shared<MonsterModeTable>();result->levels=std::move(levels);result->noRatio=number(table,row,"noRatio")!=0;
    for(size_t slot=0;slot<3;++slot) {
        const auto prefix=slot==2?std::string("S1"):"A"+std::to_string(slot+1);
        result->physical[slot]={field(prefix+"MinD"),field(prefix+"MaxD"),field(prefix+"TH")};
    }
    const std::array<std::string_view,16> modes{"DT","NU","WL","GH","A1","A2","BL","SC","S1","S2","S3","S4","DD","KB","SQ","RN"};
    for(size_t index=0;index<result->elements.size();++index) {
        const auto prefix="El"+std::to_string(index+1);auto &element=result->elements[index];
        const auto token=table.value(row,prefix+"Mode");if(token.empty()) continue;
        const auto mode=std::find(modes.begin(),modes.end(),token);if(mode==modes.end()) throw std::runtime_error("Unknown native element mode");
        element.mode=uint8_t(mode-modes.begin());element.type=table.value(row,prefix+"Type");
        element.chance=field(prefix+"Pct");element.minimum=field(prefix+"MinD");element.maximum=field(prefix+"MaxD");element.duration=field(prefix+"Dur");
    }
    return result;
}
}
