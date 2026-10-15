#include "monster_type_data.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
namespace d2x {
std::shared_ptr<const MonsterTypeCatalog> loadMonsterTypeCatalog(const DataTable &monstats,const DataTable &montype,const DataTable &monstats2) {
    std::map<std::string,uint16_t,std::less<>> names;
    if(montype.rows().size()>UINT16_MAX) throw std::runtime_error("Native monster type table exceeds word identity");
    for(size_t row=0;row<montype.rows().size();++row) {
        const auto name=montype.value(row,"type");
        if(!name.empty() && !names.emplace(name,uint16_t(row)).second) throw std::runtime_error("Duplicate native monster type");
    }
    std::vector<std::array<uint16_t,3>> parents(montype.rows().size());
    for(size_t row=0;row<parents.size();++row) for(size_t i=0;i<3;++i) {
        const auto name=montype.value(row,"equiv"+std::to_string(i+1));
        if(name.empty()) continue;
        const auto found=names.find(name);if(found==names.end()) throw std::runtime_error("Missing native monster parent type");
        parents[row][i]=found->second;
    }
    std::vector<std::vector<uint16_t>> lineage(parents.size());std::vector<uint8_t> visiting(parents.size());
    const std::function<void(uint16_t)> resolve=[&](uint16_t id) {
        if(!id || visiting[id]==2) return;
        if(visiting[id]==1) throw std::runtime_error("Cyclic native monster type table");
        visiting[id]=1;std::set<uint16_t> result{id};
        // Native nesting only reaches equiv2/3 through preceding nonzero parents.
        for(const auto parent:parents[id]) {
            if(!parent) break;
            resolve(parent);result.insert(lineage[parent].begin(),lineage[parent].end());
        }
        lineage[id].assign(result.begin(),result.end());visiting[id]=2;
    };
    for(size_t id=1;id<lineage.size();++id) resolve(uint16_t(id));
    auto result=std::make_shared<MonsterTypeCatalog>();
    for(size_t row=0;row<monstats.rows().size();++row) if(const auto id=monstats.number(row,"hcIdx")) {
        const auto name=monstats.value(row,"MonType");
        std::vector<uint16_t> types;
        if(!name.empty()) {
            const auto found=names.find(name);if(found==names.end()) throw std::runtime_error("Missing MonStats monster type");
            types=lineage[found->second];
        }
        if(!result->classes.emplace(*id,std::move(types)).second) throw std::runtime_error("Duplicate native monster class type");
        const auto extra=monstats.value(row,"MonStatsEx");
        for(size_t entry=0;entry<monstats2.rows().size();++entry) if(monstats2.value(entry,"Id")==extra && !extra.empty()) {
            result->dimensions.emplace(*id,std::pair{monstats2.number(entry,"small").value_or(0)!=0,monstats2.number(entry,"large").value_or(0)!=0});
            break;
        }
    }
    return result;
}
}
