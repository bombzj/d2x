#include "weapon_skill_data.hpp"
#include "missile_effects.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing weapon skill field: " + std::string(field));
    return *value;
}
size_t named(const DataTable &table, std::string_view column, std::string_view value) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (!value.empty() && table.value(row, column) == value) return row;
    throw std::runtime_error("Missing weapon skill reference: " + std::string(value));
}
} // namespace
void loadWeaponSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                      const DataTable &sounds, Archives &archives) {
    for (const auto name : {"Plague Javelin", "Exploding Arrow"}) {
        const bool plague = std::string_view(name) == "Plague Javelin";
        const auto row = named(skills, "skill", name);
        auto &entry = catalog.skills.at(required(skills, row, "Id"));
        const auto missile = named(missiles, "Missile", skills.value(row, "srvmissile"));
        if (entry.classCode != "ama" || required(skills, row, "srvstfunc") != 4 ||
            required(skills, row, "decquant") != 1 || required(skills, row, "UseAttackRate") != 1 ||
            required(skills, row, "SrcDam") != 128 || skills.value(row, "anim") != (plague ? "TH" : "A1") ||
            skills.value(row, "itypea1") != (plague ? "jave" : "miss") ||
            skills.value(row, "EType") != (plague ? "pois" : "fire") ||
            (plague && missiles.value(missile, "Skill") != name) ||
            (!plague && (!missiles.value(missile, "Skill").empty() || required(missiles, missile, "SrcDamage") != 128)) ||
            required(missiles, missile, "pSrvDoFunc") != (plague ? 3 : 1) ||
            required(missiles, missile, "pSrvHitFunc") != (plague ? 2 : 4) ||
            required(missiles, missile, "AlwaysExplode") != 1)
            throw std::runtime_error("Unsupported original weapon skill: " + std::string(name));
        SkillSpec spec;
        spec.sourceId = entry.id;
        spec.effect = SkillBehavior::WeaponProjectile;
        spec.weapon = WeaponSkillSpec{std::string(skills.value(row, "itypea1")), plague,
            skills.number(row, "usemanaondo").value_or(0) != 0,
            required(skills, row, "ToHit"), required(skills, row, "LevToHit"),
            skills.number(row, "delay").value_or(0)};
        spec.mana = required(skills, row, "mana");
        spec.minimumMana = required(skills, row, "minmana");
        spec.manaPerLevel = required(skills, row, "lvlmana");
        spec.manaShift = required(skills, row, "manashift");
        spec.hitShift = required(skills, row, "HitShift");
        spec.minimumDamage = required(skills, row, "EMin");
        spec.maximumDamage = required(skills, row, "EMax");
        for (int tier = 0; tier < 5; ++tier) {
            spec.minimumPerLevel[tier] = required(skills, row, "EMinLev" + std::to_string(tier + 1));
            spec.maximumPerLevel[tier] = required(skills, row, "EMaxLev" + std::to_string(tier + 1));
        }
        if (skills.value(row, "EDmgSymPerCalc") != (plague ? "(skill('Poison Javelin'.blvl))*par8" :
                                                           "(skill('Fire Arrow'.blvl)) * par8"))
            throw std::runtime_error("Unsupported weapon skill synergy formula");
        spec.synergyPercent = required(skills, row, "Param8");
        spec.synergySkills.push_back(required(skills, named(skills, "skill", plague ? "Poison Javelin" : "Fire Arrow"), "Id"));
        spec.poisonDamage = plague;
        spec.fireDamage = !plague;
        if (plague) {
            spec.poisonFrames = required(skills, row, "ELen");
            for (int tier = 0; tier < 3; ++tier)
                spec.poisonFramesPerLevel[tier] = required(skills, row, "ELevLen" + std::to_string(tier + 1));
        }
        const auto resource = loadProjectileResource(missiles, missile, archives);
        spec.missileId = resource.id;
        spec.missileArt = resource.art;
        spec.missileVelocity = float(required(missiles, missile, "Vel"));
        spec.missileLifetime = resource.lifetime;
        spec.missileVelocityPerLevel = missiles.number(missile, "VelLev").value_or(0);
        spec.missileRangePerLevel = missiles.number(missile, "LevRange").value_or(0);
        std::vector<ProjectileResource> resources;
        spec.missileImpact = loadMissileImpact(missiles, missile, archives, resources);
        for (const auto &visual : resources)
            spec.impacts.push_back({visual.id, visual.art, visual.lifetime});
        const auto sound = [&](std::string_view key) -> std::string {
            if (key.empty()) return {};
            const auto source = named(sounds, "Sound", key);
            const auto path = "data/global/sfx/" + std::string(sounds.value(source, "FileName"));
            if (!archives.contains(path)) throw std::runtime_error("Missing weapon skill sound: " + path);
            return path;
        };
        spec.castSoundArt = sound(skills.value(row, "stsound"));
        spec.releaseSoundArt = sound(missiles.value(missile, "TravelSound"));
        spec.impactSoundArt = sound(missiles.value(missile, "HitSound"));
        if (!plague) {
            const auto explosion = named(missiles, "Missile", missiles.value(missile, "ExplosionMissile"));
            spec.impactSoundArt = sound(missiles.value(explosion, "TravelSound"));
        }
        entry.spell = std::move(spec);
    }
}
} // namespace d2x
