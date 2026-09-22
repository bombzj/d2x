#include "data_table.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace d2x {
DataTable::DataTable(const Bytes &bytes) {
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        std::vector<std::string> cells;
        size_t from = 0;
        for (;;) {
            auto end = line.find('\t', from);
            cells.push_back(line.substr(from, end == std::string::npos ? end : end - from));
            if (end == std::string::npos)
                break;
            from = end + 1;
        }
        if (columns_.empty())
            columns_ = std::move(cells);
        else {
            if (cells.size() > columns_.size())
                throw std::runtime_error("Extra columns in MPQ data table");
            cells.resize(columns_.size());
            rows_.push_back(std::move(cells));
        }
    }
    if (columns_.empty())
        throw std::runtime_error("Empty MPQ data table");
}
bool DataTable::has(std::string_view column) const {
    return std::find(columns_.begin(), columns_.end(), column) != columns_.end();
}
std::string_view DataTable::value(size_t row, std::string_view column, size_t occurrence) const {
    for (size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i] == column && occurrence-- == 0)
            return rows_.at(row).at(i);
    return {};
}
std::optional<int> DataTable::number(size_t row, std::string_view column, size_t occurrence) const {
    auto text = value(row, column, occurrence);
    if (text.empty())
        return std::nullopt;
    int result = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("Invalid MPQ integer: row " + std::to_string(row) + " / " +
                                 std::string(column));
    return result;
}
} // namespace d2x
