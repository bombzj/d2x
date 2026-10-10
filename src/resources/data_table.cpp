#include "data_table.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace d2x {
bool DataTable::ColumnEqual::operator()(std::string_view left, std::string_view right) const {
    // MPQ headers retain their original spelling, while engine field bindings
    // use case-insensitive ASCII names (e.g. StrBonus vs ItemsTbls strbonus).
    if (left.size() != right.size()) return false;
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    for (size_t index = 0; index < left.size(); ++index)
        if (lower(left[index]) != lower(right[index])) return false;
    return true;
}
size_t DataTable::ColumnHash::operator()(std::string_view column) const {
    size_t hash = 2166136261u;
    for (unsigned char c : column) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash = (hash ^ c) * 16777619u;
    }
    return hash;
}
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
            // Native TXT compilation omits this section marker. Counting it as
            // a record shifts cube affix IDs and D2S special/affix identities.
            if (cells.front() == "Expansion") continue;
            if (cells.size() > columns_.size())
                throw std::runtime_error("Extra columns in MPQ data table");
            cells.resize(columns_.size());
            rows_.push_back(std::move(cells));
        }
    }
    if (columns_.empty())
        throw std::runtime_error("Empty MPQ data table");
    columnIndex_.reserve(columns_.size());
    for (size_t i = 0; i < columns_.size(); ++i)
        columnIndex_[columns_[i]].push_back(i);
}
bool DataTable::has(std::string_view column) const {
    return columnIndex_.contains(column);
}
std::string_view DataTable::value(size_t row, std::string_view column, size_t occurrence) const {
    const auto found = columnIndex_.find(column);
    if (found != columnIndex_.end() && occurrence < found->second.size())
        return rows_.at(row).at(found->second[occurrence]);
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
