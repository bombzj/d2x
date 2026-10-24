#include "monster_identity.hpp"
#include "resources/data_table.hpp"
#include <stdexcept>
#include <cctype>
#include <map>
#include <string>
namespace d2x {
int compiledMonsterIndex(const DataTable &table,size_t sourceRow) {
    if(sourceRow>=table.rows().size() || table.value(sourceRow,"hcIdx").empty())
        throw std::runtime_error("MonStats source row is not a compiled record");
    int index=0;
    for(size_t row=0;row<sourceRow;++row) if(!table.value(row,"hcIdx").empty()) ++index;
    return index;
}
int compiledMonsterChainIndex(const DataTable &table,size_t sourceRow) {
    (void)compiledMonsterIndex(table,sourceRow);
    const auto name=[](std::string_view value) {
        std::string result(value);
        for(auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        return result;
    };
    std::map<std::string,size_t,std::less<>> links;
    for(size_t row=0;row<table.rows().size();++row)
        if(!table.value(row,"hcIdx").empty()) links.emplace(name(table.value(row,"Id")),row);
    auto next=links.find(name(table.value(sourceRow,"BaseId")));
    if(next==links.end()) throw std::runtime_error("Missing compiled MonStats BaseId");
    for(int index=0;index<=255 && next!=links.end();++index) {
        const auto row=next->second;
        if(row==sourceRow) return index;
        const auto following=links.find(name(table.value(row,"NextInClass")));
        if(following==next) break;
        next=following;
    }
    // DATATBLS_LoadMonStatsTxt leaves the compiled initial nChainId (zero)
    // when a record is not reached from its BaseId. This is common in PD2.
    return 0;
}
std::optional<size_t> findCompiledMonsterRow(const DataTable &table,int nativeClass) {
    if(nativeClass>=0) for(size_t row=0;row<table.rows().size();++row) {
        if(table.value(row,"hcIdx").empty()) continue;
        if(!nativeClass--) return row;
    }
    return {};
}
size_t compiledMonsterRow(const DataTable &table,int nativeClass) {
    if(const auto row=findCompiledMonsterRow(table,nativeClass)) return *row;
    throw std::runtime_error("Missing compiled MonStats class");
}
}
