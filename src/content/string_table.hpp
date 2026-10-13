#pragma once
#include "resources/archive.hpp"
#include "language.hpp"
#include <map>
#include <string>
#include <string_view>

namespace d2x {
// NPC TBL values start with a numeric SPEED line that is engine metadata, not
// subtitle text; this removes it when present and leaves other formats intact.
std::string_view npcTblSpeech(std::string_view value);
// Diablo II's language TBLs are read from the mounted MPQ; no exported text is maintained.
class ClassicStrings {
    std::map<std::string, std::string, std::less<>> entries_;
    std::map<int, std::string> indexed_;
    std::map<std::string, int, std::less<>> indices_;
  public:
    explicit ClassicStrings(Archives &archives);
    explicit ClassicStrings(Archives &archives, const Language &language);
    std::string_view find(std::string_view key) const;
    std::string_view find(int index) const;
    std::string_view speech(int index) const; // Native NPC TBL SPEED line is metadata, not a subtitle.
    int index(std::string_view key) const;
    const auto &entries() const { return entries_; }
};
} // namespace d2x
