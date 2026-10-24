#pragma once
#include <cstddef>
#include <optional>
namespace d2x {
class DataTable;
// MonStats class IDs count compiled records, excluding the Expansion separator.
// hcIdx is not emitted into the compiled table.
int compiledMonsterIndex(const DataTable &, size_t sourceRow);
int compiledMonsterChainIndex(const DataTable &,size_t sourceRow);
std::optional<size_t> findCompiledMonsterRow(const DataTable &, int nativeClass);
size_t compiledMonsterRow(const DataTable &, int nativeClass);
}
