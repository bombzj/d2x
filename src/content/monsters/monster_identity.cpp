#include "monster_identity.hpp"
#include "resources/data_table.hpp"
#include <stdexcept>
namespace d2x {
int compiledMonsterIndex(const DataTable &table,size_t sourceRow) {
    if(sourceRow>=table.rows().size() || table.value(sourceRow,"hcIdx").empty())
        throw std::runtime_error("MonStats source row is not a compiled record");
    int index=0;
    for(size_t row=0;row<sourceRow;++row) if(!table.value(row,"hcIdx").empty()) ++index;
    return index;
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
