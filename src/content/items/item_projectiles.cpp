#include "resources/archive.hpp"
#include "item_projectiles.hpp"
#include "content/skills/missile_effects.hpp"
#include <map>
#include <stdexcept>

namespace d2x {
void loadItemProjectiles(std::vector<ItemDefinition> &items, const DataTable &missiles,
                         Archives &archives) {
    std::map<std::string, size_t, std::less<>> names;
    std::map<int, size_t> ids;
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (auto id = missiles.number(row, "Id")) {
            names.emplace(missiles.value(row, "Missile"), row);
            ids.emplace(*id, row);
        }
    auto required = [&](size_t row, std::string_view field) {
        auto value = missiles.number(row, field);
        if (!value) throw std::runtime_error("Missing weapon missile field: " + std::string(field));
        return *value;
    };
    auto number = [&](size_t row, std::string_view field) { return missiles.number(row, field).value_or(0); };
    // MISSILES_CreateMissileFromParams: 75% velocity, 4096 path units per subtile.
    for (auto &item : items) {
        if (item.family != ItemFamily::Weapon ||
            !(item.equipment.isType("miss") || item.equipment.throwable)) continue;
        int id = -1;
        if (item.equipment.isType("xbow")) {
            auto bolt = names.find("bolt");
            if (bolt != names.end()) id = required(bolt->second, "Id");
        } else if (item.base.projectile) id = item.base.projectile->id;
        auto found = ids.find(id);
        if (found == ids.end()) throw std::runtime_error("Missing original weapon missile: " + item.code);
        const auto row = found->second;
        const auto visual = loadProjectileResource(missiles, row, archives);
        const int velocity = required(row, "Vel") * 256 * 75 / 100;
        WeaponProjectileSpec spec{id, float(velocity) * 25.f / 4096.f, visual.lifetime, visual.art};
        spec.velocityUnits = velocity;
        if (spec.speed <= 0) throw std::runtime_error("Invalid weapon missile velocity: " + item.code);
        if (!item.equipment.isType("tpot")) {
            if (required(row, "SrcDamage") != 128 || required(row, "pSrvDoFunc") != 1 ||
                number(row, "pSrvHitFunc") != 0)
                throw std::runtime_error("Unsupported weapon missile behavior: " + item.code);
        } else {
            // Item/missile names do not reliably identify tiers; the MPQ mapping is authoritative.
            const int hit = required(row, "pSrvHitFunc");
            const auto type = missiles.value(row, "EType");
            if (required(row, "CollideType") != 6 || number(row, "SrcDamage") != 0 ||
                required(row, "pSrvDoFunc") != 1 || required(row, "AlwaysExplode") != 1 ||
                !((hit == 3 && type == "fire") || (hit == 2 && type == "pois")))
                throw std::runtime_error("Unsupported throwing potion: " + item.code);
            spec.groundTargeted = true;
            spec.impact = loadMissileImpact(missiles, row, archives, spec.resources);
            if (spec.impact->radius > 0) {
                requireFixedMissileDamage(missiles, row);
                const int shift = required(row, "HitShift");
                if (shift < 0 || shift > 8) throw std::runtime_error("Invalid projectile damage shift");
                spec.damage[0] = {required(row, "MinDamage") * (1 << shift),
                                  required(row, "MaxDamage") * (1 << shift)};
                spec.damage[2] = {required(row, "EMin") * (1 << shift),
                                  required(row, "EMax") * (1 << shift)};
            }
        }
        item.base.projectile = std::move(spec);
    }
}
} // namespace d2x
