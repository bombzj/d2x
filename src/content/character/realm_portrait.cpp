#include "realm_portrait.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool RealmPortraitCatalog::isType(const std::string &code, std::string_view target,
                                  std::set<std::string> &seen) const {
    if (code == target)
        return true;
    if (code.empty() || !seen.insert(code).second)
        return false;
    auto found = types_.find(code);
    if (found == types_.end())
        return false;
    for (const auto &parent : found->second)
        if (isType(parent, target, seen))
            return true;
    return false;
}
bool RealmPortraitCatalog::isType(const std::string &code, std::string_view target) const {
    std::set<std::string> seen;
    return isType(code, target, seen);
}
RealmPortraitCatalog::RealmPortraitCatalog(Archives &a) {
    const DataTable types(a.read("data/global/excel/itemtypes.txt"));
    for (size_t i = 0; i < types.rows().size(); ++i) {
        auto code = types.value(i, "Code");
        if (code.empty())
            continue;
        types_.emplace(std::string(code), std::array{std::string(types.value(i, "Equiv1")),
                                                     std::string(types.value(i, "Equiv2"))});
    }
    const DataTable armorTypes(a.read("data/global/excel/armtype.txt"));
    if (armorTypes.rows().size() < 3)
        throw std::runtime_error("Missing original armor components");
    for (size_t i = 0; i < 3; ++i)
        codes_[i + 1] = {std::string(armorTypes.value(i, "Token")), "tors", {}, {}, false};
    light_ = codes_[1].code;
    // Native 1.13c D2Common.dll, file 0x9D888: compatibility slot categories.
    // Only the original wire-slot reservation is fixed; item codes, inheritance,
    // appearances and weapon classes below are all taken from the current MPQ.
    constexpr std::string_view reserved = "...."
                                          "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAWWWWWWWWWWWWWWWWWWWWWWWWWWW"
                                          "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW.AAAA.."
                                          "AAAAAAWWWWAWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
                                          "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWAAAAAAAAAAAAAAAAAAAA";
    static_assert(reserved.size() == 255);
    size_t next = 4;
    for (const auto *name : {"weapons", "armor", "misc"}) {
        const DataTable items(a.read(std::string("data/global/excel/") + name + ".txt"));
        for (size_t row = 0; row < items.rows().size(); ++row) {
            std::string type(items.value(row, "type"));
            const bool weapon = isType(type, "weap"), armor = isType(type, "armo");
            if (!weapon && !isType(type, "tors") && !isType(type, "shld") &&
                !(isType(type, "helm") && !isType(type, "circ")))
                continue;
            auto gfx = items.value(row, "alternategfx");
            if (gfx.empty())
                gfx = items.value(row, "code");
            if (gfx.empty())
                continue;
            if (std::any_of(codes_.begin(), codes_.end(), [&](const auto &e) { return e.code == gfx; }))
                continue;
            size_t slot = next;
            while (slot < codes_.size() && (!codes_[slot].code.empty() || (reserved[slot] == 'W' && weapon) ||
                                            (reserved[slot] == 'A' && armor)))
                ++slot;
            if (slot == codes_.size())
                slot = next;
            if (slot >= codes_.size())
                throw std::runtime_error("Original preview component table exceeds wire capacity");
            codes_[slot] = {std::string(gfx), std::move(type), std::string(items.value(row, "wclass")),
                            std::string(items.value(row, "2handedwclass")),
                            items.number(row, "2handed").value_or(0) != 0};
            if (slot == next)
                ++next;
        }
    }
}
std::optional<RealmPortraitParts> RealmPortraitCatalog::decode(const OnlineCharacter &c) const {
    if (!c.characterClass || *c.characterClass >= 7 || c.portrait.size() != 33 || c.portrait[0] != 0x8D ||
        c.portrait[1] != 0x80)
        return {};
    // Component transforms require a separate per-layer palette consumer. Keep
    // unsupported previews absent rather than substituting unequipped art.
    for (size_t i = 14; i < 25; ++i)
        if (c.portrait[i] != 0xFF)
            return {};
    constexpr std::array tokens{"am", "so", "ne", "pa", "ba", "dz", "ai"};
    RealmPortraitParts p;
    p.token = tokens[*c.characterClass];
    p.weapon = "hth";
    p.components.fill(light_);
    p.components[5] = p.components[6] = p.components[7] = "nil";
    if (p.token == "ne")
        p.components[10] = "ne1";
    for (size_t component = 0; component < 11; ++component) {
        const auto id = c.portrait[2 + component];
        if (id == 0xFF)
            continue;
        if (id == 0 || id >= codes_.size() || codes_[id].code.empty())
            return {};
        p.components[component] = codes_[id].code;
    }
    auto hand = [&](size_t component) -> const Entry * {
        const auto id = c.portrait[2 + component];
        if (id == 0xFF || id >= codes_.size() || !isType(codes_[id].type, "weap"))
            return nullptr;
        return &codes_[id];
    };
    const auto *right = hand(5), *left = hand(6);
    if (right && left) {
        if (right->weapon == left->weapon && (right->weapon == "bow" || right->weapon == "xbw"))
            p.weapon = right->weapon;
        else if (right->weapon == "1hs" || right->weapon == "1ht") {
            if (left->weapon != "1hs" && left->weapon != "1ht")
                return {};
            p.weapon = right->weapon == "1ht" ? (left->weapon == "1ht" ? "1jt" : "1st")
                                              : (left->weapon == "1ht" ? "1js" : "1ss");
        } else
            return {};
    } else if (const auto *weapon = right ? right : left)
        p.weapon =
            weapon->twoHanded && !weapon->twoHandWeapon.empty() ? weapon->twoHandWeapon : weapon->weapon;
    if (p.weapon.empty())
        return {};
    return p;
}
} // namespace d2x
