#pragma once
#include "core/bytes.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace d2x {
// Ordered columns retain duplicate headers, empty cells, and original row indices.
// Field lookup ignores ASCII case; original headers and cell values stay intact.
class DataTable {
    struct ColumnHash {
        using is_transparent = void;
        size_t operator()(std::string_view) const;
    };
    struct ColumnEqual {
        using is_transparent = void;
        bool operator()(std::string_view, std::string_view) const;
    };
    std::vector<std::string> columns_;
    std::vector<std::vector<std::string>> rows_;
    std::unordered_map<std::string, std::vector<size_t>, ColumnHash, ColumnEqual> columnIndex_;

  public:
    explicit DataTable(const Bytes &bytes);
    const auto &columns() const { return columns_; }
    const auto &rows() const { return rows_; }
    bool has(std::string_view column) const;
    std::string_view value(size_t row, std::string_view column, size_t occurrence = 0) const;
    std::optional<int> number(size_t row, std::string_view column, size_t occurrence = 0) const;
};
} // namespace d2x
