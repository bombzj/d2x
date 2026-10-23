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
// Diablo II embeds text colours as 'c' + code after a ÿ prefix. TBL files use
// the UTF-8 form C3 BF 'c' <code>; network/d2bs text uses the raw 0xFF form.
// Codes 0-9 are digits, 10-15 use ":;<=>?", and PD2 tables also use '/' as
// white/reset. Returns the consumed byte length (0 = no escape); colour -1
// means "unknown code, keep the current colour".
inline size_t d2ColorEscape(std::string_view text, size_t index, int &color) {
    size_t i = index;
    if (i < text.size() && uint8_t(text[i]) == 0xFF) ++i;
    else if (index + 1 < text.size() && uint8_t(text[index]) == 0xC3 && uint8_t(text[index + 1]) == 0xBF)
        i = index + 2;
    else return 0;
    if (i >= text.size() || text[i] != 'c') return 0;
    ++i;
    color = -1;
    if (i < text.size()) {
        const auto code = uint8_t(text[i++]);
        if (code >= '0' && code <= '9') color = code - '0';
        else switch (code) {
        case ':': color = 10; break;
        case ';': color = 11; break;
        case '<': color = 12; break;
        case '=': color = 13; break;
        case '>': color = 14; break;
        case '?': color = 15; break;
        case '/': color = 0; break;
        default: break;
        }
    }
    return i - index;
}
inline bool hasD2ColorEscape(std::string_view text) {
    int color = -1;
    for (size_t i = 0; i < text.size(); ++i)
        if (d2ColorEscape(text, i, color)) return true;
    return false;
}
inline std::string stripD2ColorCodes(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        int color = -1;
        if (const size_t used = d2ColorEscape(text, i, color)) { i += used; continue; }
        out.push_back(text[i++]);
    }
    return out;
}
struct D2ColorRun {
    std::string text;
    int color = -1; // -1 keeps the caller-supplied default.
};
inline std::vector<D2ColorRun> parseD2ColorRuns(std::string_view text) {
    std::vector<D2ColorRun> runs;
    int color = -1;
    for (size_t i = 0; i < text.size();) {
        int code = -2;
        if (const size_t used = d2ColorEscape(text, i, code)) {
            if (code >= 0) color = code;
            i += used;
            continue;
        }
        if (runs.empty() || runs.back().color != color) runs.push_back({"", color});
        runs.back().text.push_back(text[i++]);
    }
    return runs;
}
} // namespace d2x
