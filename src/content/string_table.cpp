#include "resources/archive.hpp"
#include "string_table.hpp"
#include <cstdint>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
uint32_t number(const Bytes &bytes, size_t offset, int count) {
    if (offset + count > bytes.size()) throw std::runtime_error("Truncated MPQ string table");
    uint32_t value = 0;
    for (int i = 0; i < count; ++i) value |= uint32_t(bytes[offset + i]) << (8 * i);
    return value;
}
std::string field(const Bytes &bytes, size_t offset, size_t limit) {
    if (offset >= bytes.size() || limit > bytes.size() - offset)
        throw std::runtime_error("Invalid MPQ string table offset");
    size_t length = 0;
    while (length < limit && bytes[offset + length]) ++length;
    return {reinterpret_cast<const char *>(bytes.data() + offset), length};
}
void merge(const Bytes &bytes, std::map<std::string, std::string, std::less<>> &entries,
           std::map<int, std::string> &indexed, std::map<std::string, int, std::less<>> &indices, int base) {
    if (bytes.size() < 21 || number(bytes, 8, 1) > 1 || number(bytes, 17, 4) != bytes.size())
        throw std::runtime_error("Invalid MPQ string table header");
    const auto nodes = number(bytes, 2, 2), buckets = number(bytes, 4, 4);
    const auto start = number(bytes, 9, 4);
    const uint64_t hashStart = 21ull + uint64_t(nodes) * 2;
    if (start > bytes.size() || hashStart + uint64_t(buckets) * 17 > start)
        throw std::runtime_error("Invalid MPQ string table index");
    for (uint32_t i = 0; i < buckets; ++i) {
        const size_t node = size_t(hashStart) + size_t(i) * 17;
        if (!bytes[node]) continue;
        const auto keyOffset = number(bytes, node + 7, 4);
        const auto textOffset = number(bytes, node + 11, 4);
        const auto textLength = number(bytes, node + 15, 2);
        if (keyOffset >= bytes.size()) throw std::runtime_error("Invalid MPQ string key offset");
        auto key = field(bytes, keyOffset, bytes.size() - keyOffset);
        auto value = field(bytes, textOffset, textLength);
        indexed[base + int(number(bytes, node + 1, 2))] = value;
        if (!key.empty()) {
            indices[key] = base + int(number(bytes, node + 1, 2));
            entries[std::move(key)] = std::move(value);
        }
    }
}
} // namespace
ClassicStrings::ClassicStrings(Archives &archives) {
    for (const auto &[name, base] : {std::pair{"string", 0}, {"expansionstring", 20000}, {"patchstring", 10000}}) {
        auto path = "data/local/lng/eng/" + std::string(name) + ".tbl";
        if (archives.contains(path)) merge(archives.read(path), entries_, indexed_, indices_, base);
    }
}
std::string_view ClassicStrings::find(std::string_view key) const {
    auto found = entries_.find(key);
    return found == entries_.end() ? std::string_view{} : found->second;
}
std::string_view ClassicStrings::find(int index) const {
    auto found = indexed_.find(index);
    return found == indexed_.end() ? std::string_view{} : found->second;
}
std::string_view ClassicStrings::speech(int index) const {
    auto text=find(index);
    const auto newline=text.find('\n');
    if (newline==std::string_view::npos) return text;
    auto header=text.substr(0,newline);
    if (header.ends_with('\r')) header.remove_suffix(1);
    unsigned speed{};
    const auto [end,error]=std::from_chars(header.data(),header.data()+header.size(),speed);
    // Current a1npc.txt SPEED:45/124 matches the first line of the original
    // Warriv TBL messages. Leave non-speech/unrecognized formats untouched.
    if (error==std::errc{} && end==header.data()+header.size()) text.remove_prefix(newline+1);
    return text;
}
int ClassicStrings::index(std::string_view key) const {
    auto found = indices_.find(key);
    return found == indices_.end() ? -1 : found->second;
}
} // namespace d2x
