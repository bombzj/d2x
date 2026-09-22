#pragma once
#include "core/bytes.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// Ordered columns retain duplicate headers, empty cells, and original row indices.
class DataTable {
    std::vector<std::string> columns_;
    std::vector<std::vector<std::string>> rows_;

  public:
    explicit DataTable(const Bytes &bytes);
    const auto &columns() const { return columns_; }
    const auto &rows() const { return rows_; }
    bool has(std::string_view column) const;
    std::string_view value(size_t row, std::string_view column, size_t occurrence = 0) const;
    std::optional<int> number(size_t row, std::string_view column, size_t occurrence = 0) const;
};
} // namespace d2x
