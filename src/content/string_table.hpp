#pragma once
#include "resources/archive.hpp"
#include <map>
#include <string>
#include <string_view>

namespace d2x {
// Diablo II's language TBLs are read from the mounted MPQ; no exported text is maintained.
class ClassicStrings {
    std::map<std::string, std::string, std::less<>> entries_;
  public:
    explicit ClassicStrings(Archives &archives);
    std::string_view find(std::string_view key) const;
};
} // namespace d2x
