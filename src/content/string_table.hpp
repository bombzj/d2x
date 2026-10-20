#pragma once
#include "resources/archive.hpp"
#include "language.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// NPC TBL values start with a numeric SPEED line that is engine metadata, not
// subtitle text; this removes it when present and leaves other formats intact.
std::string_view npcTblSpeech(std::string_view value);
// Diablo II's language TBLs are read from the mounted MPQ; no exported text is maintained.
class ClassicStrings {
    std::map<std::string, std::string, std::less<>> entries_;
    std::map<int, std::string> indexed_;
    std::map<std::string, int, std::less<>> indices_;
    // Original TBLs mark nameless monstats rows with placeholder keys
    // (dummy/Dummy/not used/unused/Not used) whose shared value must never
    // render as a unit name.
    std::string placeholder_;
  public:
    explicit ClassicStrings(Archives &archives);
    explicit ClassicStrings(Archives &archives, const Language &language);
    std::string_view find(std::string_view key) const;
    std::string_view find(int index) const;
    std::string_view speech(int index) const; // Native NPC TBL SPEED line is metadata, not a subtitle.
    int index(std::string_view key) const;
    // Display lookup: suppresses the reserved placeholder value so nameless
    // units never render "an evil force"; raw key fallback stays with callers.
    std::string_view findDisplayName(std::string_view key) const;
    bool isPlaceholder(std::string_view value) const;
    const auto &entries() const { return entries_; }
};
} // namespace d2x
