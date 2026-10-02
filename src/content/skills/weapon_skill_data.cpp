#include "resources/archive.hpp"
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
void loadPaladinSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                      const DataTable &overlays, const DataTable &sounds, Archives &archives) {
    const auto row = named(skills, "skill", "Holy Bolt");
    auto &entry = catalog.skills.at(required(skills, row, "Id"));
    if (entry.classCode != "pal" || skills.value(row, "anim") != "SC" || skills.value(row, "EType") != "mag")
        throw std::runtime_error("Unsupported original Holy Bolt");
    SkillSpec spec;
    spec.sourceId = entry.id;
    spec.effect = SkillBehavior::HolyBolt;
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
    if (skills.value(row, "calc1") != "ln12 * (100 + skill('Prayer'.blvl) * par7) / 100" ||
        skills.value(row, "calc2") != "ln34 * (100 + skill('Prayer'.blvl) * par7) / 100" ||
        skills.value(row, "EDmgSymPerCalc") != "(skill('Blessed Hammer'.blvl)+skill('Fist of the Heavens'.blvl))*par8")
        throw std::runtime_error("Unsupported Holy Bolt formula");
    for (int parameter = 0; parameter < 4; ++parameter)
        spec.healingParameters[parameter] = required(skills, row, "Param" + std::to_string(parameter + 1));
    spec.healingSynergySkill = required(skills, named(skills, "skill", "Prayer"), "Id");
    spec.healingSynergyPercent = required(skills, row, "Param7");
    spec.synergyPercent = required(skills, row, "Param8");
    for (const auto name : {"Blessed Hammer", "Fist of the Heavens"})
        spec.synergySkills.push_back(required(skills, named(skills, "skill", name), "Id"));
    const auto missile = named(missiles, "Missile", skills.value(row, "srvmissile"));
    if (required(missiles, missile, "pSrvHitFunc") != 7 || required(missiles, missile, "sHitPar1") != 1 ||
        required(missiles, missile, "sHitPar2") != 1)
        throw std::runtime_error("Unsupported Holy Bolt hit filter");
    const auto overlay = named(overlays, "overlay", missiles.value(missile, "ProgOverlay"));
    auto &visual = spec.hitOverlay;
    visual.id = int(overlay);
    visual.frames = required(overlays, overlay, "Frames");
    visual.fps = float(required(overlays, overlay, "AnimRate"));
    visual.trans = required(overlays, overlay, "Trans");
    visual.preDraw = overlays.number(overlay, "PreDraw").value_or(0) != 0;
    visual.offset = {-float(required(overlays, overlay, "Xoffset")), float(required(overlays, overlay, "Yoffset"))};
    for (int height = 0; height < 4; ++height)
        visual.heights[height] = required(overlays, overlay, "Height" + std::to_string(height + 1));
    visual.art = "data/global/overlays/" + std::string(overlays.value(overlay, "Filename")) + ".dcc";
    if (visual.frames <= 0 || visual.fps <= 0 || !archives.contains(visual.art))
        throw std::runtime_error("Missing Holy Bolt healing overlay");
    const auto resource = loadProjectileResource(missiles, missile, archives);
    spec.missileId = resource.id;
    spec.missileArt = resource.art;
    spec.missileVelocity = float(required(missiles, missile, "Vel"));
    spec.missileLifetime = resource.lifetime;
    spec.missileVelocityPerLevel = missiles.number(missile, "VelLev").value_or(0);
    spec.missileRangePerLevel = missiles.number(missile, "LevRange").value_or(0);
    const auto sound = [&](std::string_view key) -> std::string {
        if (key.empty()) return {};
        const auto source = named(sounds, "Sound", key);
        const auto path = "data/global/sfx/" + std::string(sounds.value(source, "FileName"));
        if (!archives.contains(path)) throw std::runtime_error("Missing paladin skill sound: " + path);
        return path;
    };
    spec.castSoundArt = sound(skills.value(row, "stsound"));
    spec.releaseSoundArt = sound(missiles.value(missile, "TravelSound"));
    spec.impactSoundArt = sound(missiles.value(missile, "HitSound"));
    entry.spell = std::move(spec);
    const auto sacrifice = named(skills, "skill", "Sacrifice");
    if (required(skills, sacrifice, "srvstfunc") != 29 || required(skills, sacrifice, "srvdofunc") != 64 ||
        skills.value(sacrifice, "calc1") != "ln12+skill('Redemption'.blvl)*par8+skill('Fanaticism'.blvl)*par7" ||
        skills.value(sacrifice, "calc2") != "par3" || required(skills, sacrifice, "SrcDam") != 128 ||
        skills.value(sacrifice, "anim") != "A1" || skills.value(sacrifice, "itypea1") != "mele")
        throw std::runtime_error("Unsupported Sacrifice rules");
    SkillSpec melee;
    melee.sourceId = required(skills, sacrifice, "Id");
    melee.effect = SkillBehavior::Sacrifice;
    melee.mana = required(skills, sacrifice, "mana");
    melee.minimumMana = required(skills, sacrifice, "minmana");
    melee.manaPerLevel = required(skills, sacrifice, "lvlmana");
    melee.manaShift = required(skills, sacrifice, "manashift");
    melee.weapon = WeaponSkillSpec{};
    auto &weapon = *melee.weapon;
    weapon.requiredType = std::string(skills.value(sacrifice, "itypea1"));
    weapon.attackRating = required(skills, sacrifice, "ToHit");
    weapon.attackRatingPerLevel = required(skills, sacrifice, "LevToHit");
    weapon.damagePercent = required(skills, sacrifice, "Param1");
    weapon.damagePerLevel = required(skills, sacrifice, "Param2");
    weapon.selfDamagePercent = required(skills, sacrifice, "Param3");
    weapon.damageSynergies.emplace(required(skills, named(skills, "skill", "Redemption"), "Id"), required(skills, sacrifice, "Param8"));
    weapon.damageSynergies.emplace(required(skills, named(skills, "skill", "Fanaticism"), "Id"), required(skills, sacrifice, "Param7"));
    catalog.skills.at(melee.sourceId).spell = std::move(melee);
}
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
        spec.weapon = WeaponSkillSpec{};
        spec.weapon->requiredType = std::string(skills.value(row, "itypea1"));
        spec.weapon->thrown = plague;
        spec.weapon->manaOnRelease = skills.number(row, "usemanaondo").value_or(0) != 0;
        spec.weapon->attackRating = required(skills, row, "ToHit");
        spec.weapon->attackRatingPerLevel = required(skills, row, "LevToHit");
        spec.weapon->delayFrames = skills.number(row, "delay").value_or(0);
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
