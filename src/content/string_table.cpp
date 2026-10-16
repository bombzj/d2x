#include "resources/archive.hpp"
#include "language.hpp"
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
    struct KeyEntry { uint32_t distance; int index; std::string value; };
    std::map<std::string,KeyEntry,std::less<>> keys;
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
            // Native key lookup starts at the stored hash bucket and probes
            // forward, wrapping. Original TBLs contain duplicate names; the
            // first probe wins, not the last record in physical bucket order.
            const auto hash = number(bytes, node + 3, 4);
            if (hash >= buckets) throw std::runtime_error("Invalid MPQ string hash bucket");
            const auto distance = (uint64_t(i) + buckets - hash) % buckets;
            const auto found = keys.find(key);
            if (found == keys.end() || distance < found->second.distance)
                keys.insert_or_assign(std::move(key),KeyEntry{uint32_t(distance),base + int(number(bytes,node+1,2)),std::move(value)});
        }
    }
    // Expansion/patch tables still override a previous table's chosen key;
    // numeric lookups retain every original entry, including duplicate names.
    for (auto &[key,record]:keys) {
        indices[key]=record.index;
        entries[key]=std::move(record.value);
    }
}
} // namespace
std::string_view npcTblSpeech(std::string_view text) {
    const auto newline=text.find('\n');
    if (newline==std::string_view::npos) return text;
    auto header=text.substr(0,newline);
    if (header.ends_with('\r')) header.remove_suffix(1);
    unsigned speed{};
    const auto [end,error]=std::from_chars(header.data(),header.data()+header.size(),speed);
    // Authored SPEED lines match the first line of the original TBL messages.
    // Leave non-speech/unrecognized formats untouched.
    if (error==std::errc{} && end==header.data()+header.size()) text.remove_prefix(newline+1);
    return text;
}
ClassicStrings::ClassicStrings(Archives &archives)
    : ClassicStrings(archives, languageOf(archives)) {}

ClassicStrings::ClassicStrings(Archives &archives, const Language &language) {
    const std::string dir = "data/local/lng/" + language.directory + "/";
    for (const auto &[name, base] : {std::pair{"string", 0}, {"expansionstring", 20000}, {"patchstring", 10000}}) {
        auto path = dir + std::string(name) + ".tbl";
        if (archives.contains(path)) {
            merge(archives.read(path), entries_, indexed_, indices_, base);
            continue;
        }
        // Fallback to English if not found
        if (language.directory != "eng") {
            auto fallback = "data/local/lng/eng/" + std::string(name) + ".tbl";
            if (archives.contains(fallback)) {
                merge(archives.read(fallback), entries_, indexed_, indices_, base);
            }
        }
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
    return npcTblSpeech(find(index));
}
int ClassicStrings::index(std::string_view key) const {
    auto found = indices_.find(key);
    return found == indices_.end() ? -1 : found->second;
}
} // namespace d2x
