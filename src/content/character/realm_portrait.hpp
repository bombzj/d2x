#pragma once
#include "contracts/online.hpp"
#include "resources/archive.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace d2x {
struct RealmPortraitParts {
    std::string token, weapon;
    std::array<std::string, 16> components;
};
// Native 1.13c preview component IDs are rebuilt from the mounted item tables,
// not OpenD2's older, static appearance-code list.
class RealmPortraitCatalog {
    struct Entry {
        std::string code, type, weapon, twoHandWeapon;
        bool twoHanded{};
    };
    std::array<Entry, 255> codes_;
    struct Item {
        Entry appearance;
        int component{-1};
        std::array<std::string, 6> body;
    };
    std::map<std::string, Item, std::less<>> items_;
    std::map<std::string, std::array<std::string, 2>> types_;
    std::string light_;
    bool isType(const std::string &, std::string_view, std::set<std::string> &) const;
    bool isType(const std::string &, std::string_view) const;

  public:
    explicit RealmPortraitCatalog(Archives &);
    std::optional<RealmPortraitParts> decode(const OnlineCharacter &) const;
    std::optional<RealmPortraitParts> decode(const OnlineUnit &, const OnlineWorldView &, bool unequipped = false) const;
};
} // namespace d2x
