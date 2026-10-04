#pragma once
#include "resources/archive.hpp"
#include <map>
#include <string>
#include <string_view>

namespace d2x {
// Diablo II's language TBLs are read from the mounted MPQ; no exported text is maintained.
class ClassicStrings {
    std::map<std::string, std::string, std::less<>> entries_;
    std::map<int, std::string> indexed_;
    std::map<std::string, int, std::less<>> indices_;
  public:
    explicit ClassicStrings(Archives &archives);
    std::string_view find(std::string_view key) const;
    std::string_view find(int index) const;
    int index(std::string_view key) const;
    const auto &entries() const { return entries_; }
};
} // namespace d2x
