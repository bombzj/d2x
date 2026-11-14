#pragma once
#include "primitives.hpp"
#include <span>
#include <string>
#include <utility>

namespace d2x {
// D2Client 67AF0/67BF0/67FD0. Layout clients choose the font and row gap.
inline constexpr int textListInset = 32, textListBaseline = 25, textListLineStep = 14;
inline int textListWidth(Rectangle bounds) { return int(bounds.width) - 50; }
inline int textListHeight(Rectangle bounds) { return int(bounds.height) - 20; }

template<class Glyph, class Character>
std::vector<std::pair<size_t, size_t>> wrapClassicLines(std::span<const Glyph> glyphs, Character character,
                                                       const ClassicFont &font, float width) {
    std::vector<std::pair<size_t, size_t>> result;
    for (size_t start = 0; start < glyphs.size();) {
        size_t end = start, wordEnd = start;
        float used = 0;
        for (; end < glyphs.size(); ++end) {
            const auto code = character(glyphs[end]);
            if (code == '\n') break;
            if (code == ' ' && end > start && character(glyphs[end - 1]) != ' ') wordEnd = end;
            const int advance = code == '\r' ? 0 : font.widthOf(code);
            if (end > start && used + advance > width) break;
            used += advance;
        }
        const bool newline = end < glyphs.size() && character(glyphs[end]) == '\n';
        if (end < glyphs.size() && !newline && wordEnd > start) end = wordEnd;
        const auto contentEnd = end > start && character(glyphs[end - 1]) == '\r' ? end - 1 : end;
        result.emplace_back(start, contentEnd);
        start = end;
        if (newline) ++start;
        else while (start < glyphs.size() && character(glyphs[start]) == ' ') ++start;
    }
    return result;
}

std::vector<std::string> wrapClassicText(std::string_view, const ClassicFont &, int width);
int textListPageSize(std::span<const int> lineCounts, int first, int height, int rowGap);
int textListLastPage(std::span<const int> lineCounts, int height, int rowGap);
} // namespace d2x
