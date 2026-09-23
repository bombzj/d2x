#include "item_projectiles.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
void loadItemProjectiles(std::vector<ItemDefinition> &items, const DataTable &missiles,
                         Archives &archives) {
    std::optional<int> crossbowBolt;
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (missiles.value(row, "Missile") == "bolt")
            crossbowBolt = missiles.number(row, "Id");
    for (auto &item : items) {
        if (item.family != ItemFamily::Weapon ||
            !(item.equipment.isType("miss") || item.equipment.isType("thro"))) continue;
        // The engine selects the bolt missile for crossbows; other weapons use Weapons.missiletype.
        auto missileId = item.equipment.isType("xbow") ? crossbowBolt :
            item.base.projectile ? std::optional<int>(item.base.projectile->id) : std::nullopt;
        if (!missileId) throw std::runtime_error("Missing original weapon missile ID: " + item.code);
        size_t row = 0;
        for (; row < missiles.rows().size(); ++row)
            if (missiles.number(row, "Id") == missileId) break;
        if (row == missiles.rows().size())
            throw std::runtime_error("Unknown original missile ID: " + item.code);
        auto speed = missiles.number(row, "Vel");
        auto range = missiles.number(row, "Range");
        auto file = std::string(missiles.value(row, "CelFile"));
        std::transform(file.begin(), file.end(), file.begin(),
                       [](unsigned char ch) { return char(std::tolower(ch)); });
        auto path = "data/global/missiles/" + file + ".dcc";
        if (!speed || *speed <= 0 || !range || *range <= 0 || file.empty() || !archives.contains(path))
            throw std::runtime_error("Incomplete original missile resource: " + item.code);
        item.base.projectile = ItemBaseStats::Projectile{*missileId, float(*speed), float(*range) / 25.f, path};
    }
}
} // namespace d2x
