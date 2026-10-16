#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// Decode UTF-8 text into Unicode code points. Bytes that do not form a valid
// sequence are emitted as-is (single-byte/Latin-1 fallback) so resource strings
// using a legacy code page keep working alongside UTF-8 language tables.
inline std::vector<uint32_t> utf8Decode(std::string_view text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const auto lead = uint8_t(text[i]);
        uint32_t cp = lead;
        size_t len = 1;
        if (lead >= 0xF0) { len = 4; cp = lead & 0x07; }
        else if (lead >= 0xE0) { len = 3; cp = lead & 0x0F; }
        else if (lead >= 0xC0) { len = 2; cp = lead & 0x1F; }
        if (len > 1) {
            if (i + len <= n) {
                bool valid = true;
                for (size_t k = 1; k < len; ++k) {
                    const auto next = uint8_t(text[i + k]);
                    if ((next & 0xC0) != 0x80) { valid = false; break; }
                    cp = (cp << 6) | (next & 0x3F);
                }
                if (valid) { out.push_back(cp); i += len; continue; }
            }
            out.push_back(lead);
            ++i;
            continue;
        }
        out.push_back(cp);
        ++i;
    }
    return out;
}
} // namespace d2x
