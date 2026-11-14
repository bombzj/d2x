#include "text_list.hpp"
#include "core/text.hpp"
#include "core/utf8.hpp"
#include <algorithm>

namespace d2x {
std::vector<std::string> wrapClassicText(std::string_view text, const ClassicFont &font, int width) {
    const auto codes = utf8Decode(text);
    std::vector<std::string> lines;
    for (const auto &[start, end] : wrapClassicLines(std::span(codes), [](uint32_t code) { return code; }, font, float(width))) {
        auto &line = lines.emplace_back();
        for (size_t i = start; i < end; ++i) appendUtf8(line, codes[i]);
    }
    return lines;
}

int textListPageSize(std::span<const int> lineCounts, int first, int height, int rowGap) {
    int end = first;
    while (end < int(lineCounts.size())) {
        height -= lineCounts[size_t(end)] * textListLineStep + rowGap;
        if (height < 0) break;
        ++end;
    }
    return std::max(end - first, first < int(lineCounts.size()) ? 1 : 0);
}

int textListLastPage(std::span<const int> lineCounts, int height, int rowGap) {
    int first = int(lineCounts.size());
    while (first > 0) {
        height -= lineCounts[size_t(first - 1)] * textListLineStep + rowGap;
        if (height < 0) break;
        --first;
    }
    return first;
}
} // namespace d2x
