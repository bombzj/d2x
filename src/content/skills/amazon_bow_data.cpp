#include "amazon_bow_data.hpp"
#include "missile_effects.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
size_t named(const DataTable &table, std::string_view column, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (!name.empty() && table.value(row, column) == name) return row;
    throw std::runtime_error("Missing Amazon bow reference: " + std::string(name));
}
int number(const DataTable &table, size_t row, std::string_view field) {
    if (!table.has(field)) throw std::runtime_error("Missing Amazon bow field: " + std::string(field));
    if (!table.value(row, field).empty() && !table.number(row, field))
        throw std::runtime_error("Invalid Amazon bow number: " + std::string(field));
    return table.number(row, field).value_or(0); // Blank native numeric fields are zero.
}
std::string_view formula(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.value(row, field);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}
std::string sound(const DataTable &sounds, std::string_view key, Archives &archives) {
    if (key.empty()) return {};
    const auto row = named(sounds, "Sound", key);
    const auto path = "data/global/sfx/" + std::string(sounds.value(row, "FileName"));
    if (!archives.contains(path)) throw std::runtime_error("Missing Amazon bow sound: " + path);
    return path;
}
}
void loadAmazonBowSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                         const DataTable &sounds, Archives &archives) {
    for (const auto name : {"Magic Arrow", "Fire Arrow", "Cold Arrow", "Multiple Shot", "Ice Arrow", "Guided Arrow", "Strafe", "Immolation Arrow", "Freezing Arrow"}) {
        const bool magic = std::string_view(name) == "Magic Arrow";
        const bool ice = std::string_view(name) == "Ice Arrow";
        const bool freezing = std::string_view(name) == "Freezing Arrow";
        const bool cold = ice || freezing || std::string_view(name) == "Cold Arrow";
        const bool multiple = std::string_view(name) == "Multiple Shot";
        const bool guided = std::string_view(name) == "Guided Arrow";
        const bool strafe = std::string_view(name) == "Strafe";
        const bool immolation = std::string_view(name) == "Immolation Arrow";
        const auto row = named(skills, "skill", name);
        const auto missile = named(missiles, "Missile", skills.value(row, multiple || guided || strafe ? "srvmissilea" : "srvmissile"));
        if (skills.value(row, "charclass") != "ama" || skills.value(row, "itypea1") != "miss" ||
            skills.value(row, "anim") != "A1" || number(skills, row, "UseAttackRate") != 1 ||
            number(missiles, missile, "pSrvDoFunc") != (guided ? 7 : 1) || number(missiles, missile, "pSrvDmgFunc") != (multiple || guided || strafe || immolation || freezing ? 0 : ice ? 2 : 1) ||
            (!multiple && !guided && !strafe && !immolation && !freezing && !ice && missiles.value(missile, "DmgCalc1") != "dl12"))
            throw std::runtime_error("Unsupported Amazon bow rules: " + std::string(name));
        SkillSpec spec;
        spec.sourceId = number(skills, row, "Id"); spec.effect = SkillBehavior::WeaponProjectile;
        spec.weapon = WeaponSkillSpec{};
        auto &weapon = *spec.weapon;
        weapon.requiredType = std::string(skills.value(row, "itypea1"));
        weapon.noAmmo = number(skills, row, "noammo") != 0;
        weapon.manaOnRelease = number(skills, row, "usemanaondo") != 0;
        weapon.attackRating = number(skills, row, "ToHit");
        weapon.attackRatingPerLevel = number(skills, row, "LevToHit");
        weapon.delayFrames = number(skills, row, "delay");
        spec.mana = number(skills, row, "mana"); spec.minimumMana = number(skills, row, "minmana");
        spec.manaPerLevel = number(skills, row, "lvlmana"); spec.manaShift = number(skills, row, "manashift");
        spec.hitShift = number(skills, row, "HitShift");
        spec.minimumDamage = number(skills, row, magic ? "MinDam" : "EMin");
        spec.maximumDamage = number(skills, row, magic ? "MaxDam" : "EMax");
        for (int tier = 0; tier < 5; ++tier) {
            spec.minimumPerLevel[tier] = number(skills, row, std::string(magic ? "MinLevDam" : "EMinLev") + std::to_string(tier + 1));
            spec.maximumPerLevel[tier] = number(skills, row, std::string(magic ? "MaxLevDam" : "EMaxLev") + std::to_string(tier + 1));
        }
        if (!magic && !multiple && !guided && !strafe) {
            const auto synergy = ice || freezing ? "Cold Arrow" : cold ? "Ice Arrow" : "Exploding Arrow";
            if (skills.value(row, "EType") != (cold ? "cold" : "fire") ||
                formula(skills, row, "EDmgSymPerCalc") != (ice || freezing ? "(skill('Cold Arrow'.blvl))*par8" :
                    "(skill('" + std::string(synergy) + "'.blvl)) * par8"))
                throw std::runtime_error("Unsupported elemental arrow synergy");
            spec.fireDamage = !cold; spec.coldDamage = cold;
            spec.synergyPercent = number(skills, row, "Param8");
            spec.synergySkills.push_back(number(skills, named(skills, "skill", synergy), "Id"));
        }
        if (cold) {
            spec.coldFrames = number(skills, row, "ELen");
            for (int tier = 0; tier < 3; ++tier)
                spec.coldFramesPerLevel[tier] = number(skills, row, "ELevLen" + std::to_string(tier + 1));
            if (ice || freezing) {
                const auto synergy = ice ? "Freezing Arrow" : "Ice Arrow";
                if (formula(skills, row, "ELenSymPerCalc") != "(skill('" + std::string(synergy) + "'.blvl)) * par7")
                    throw std::runtime_error("Unsupported arrow freeze synergy");
                spec.coldSynergySkill = number(skills, named(skills, "skill", synergy), "Id");
                spec.coldSynergyPercent = number(skills, row, "Param7");
            }
        }
        auto bow = std::make_shared<BowSkillSpec>();
        bow->sourceDamage = number(skills, row, "SrcDam"); bow->physicalSkillDamage = magic;
        bow->element = magic || freezing ? DamageType::Magic : cold ? DamageType::Cold : DamageType::Fire;
        bow->conversionPercent = ice ? 0 : number(missiles, missile, "dParam1");
        bow->conversionPerLevel = ice ? 0 : number(missiles, missile, "dParam2");
        bow->freezePercent = ice ? number(missiles, missile, "dParam1") : 0;
        bow->multiple = multiple;
        if(multiple) bow->activateFrames=number(skills,row,"Param3");
        bow->guided = guided;
        bow->strafe = strafe;
        bow->immolation = immolation;
        if (immolation) {
            if (formula(skills, row, "calc1") != "par1" || formula(skills, row, "calc2") != "par2" ||
                number(missiles, missile, "pSrvHitFunc") != 9)
                throw std::runtime_error("Unsupported Immolation Arrow impact");
            bow->fireRadius = number(skills, row, "Param1"); bow->explosionRadius = number(skills, row, "Param2");
            const auto child = named(missiles, "Missile", missiles.value(missile, "HitSubMissile1"));
            if (number(missiles, child, "pSrvDoFunc") != 5 || number(missiles, child, "pSrvDmgFunc") != 3 ||
                missiles.value(child, "EDmgSymPerCalc") != "skill('Fire Arrow'.blvl) * 5")
                throw std::runtime_error("Unsupported Immolation Arrow ground fire");
            auto &fire = bow->fire;
            const auto resource = loadProjectileResource(missiles, child, archives);
            fire.fireId = resource.id; spec.submissileResources.push_back(resource);
            const auto duration = missiles.value(missile, "SHitCalc1");
            size_t parsed = 0; fire.fireFrames = std::stoi(std::string(duration), &parsed);
            if (parsed != duration.size() || fire.fireFrames <= 0) throw std::runtime_error("Unsupported Immolation fire duration");
            fire.size = number(missiles, child, "Size"); fire.hitShift = number(missiles, child, "HitShift");
            fire.minimumDamage = number(missiles, child, "EMin"); fire.maximumDamage = number(missiles, child, "EMax");
            fire.softHitChance = number(missiles, child, "dParam1");
            for (int tier = 0; tier < 5; ++tier) {
                bow->fireMinimumPerLevel[tier] = number(missiles, child, "MinELev" + std::to_string(tier + 1));
                bow->fireMaximumPerLevel[tier] = number(missiles, child, "MaxELev" + std::to_string(tier + 1));
            }
            bow->fireSynergySkill = number(skills, named(skills, "skill", "Fire Arrow"), "Id");
            const auto formula = missiles.value(child, "EDmgSymPerCalc");
            bow->fireSynergyPercent = std::stoi(std::string(formula.substr(formula.find('*') + 1)));
            bow->fireMastery = number(missiles, child, "ApplyMastery") != 0;
            spec.missileImpact = MissileImpactSpec{}; spec.missileImpact->radius = float(bow->explosionRadius);
        }
        if (strafe) {
            if (number(skills, row, "srvstfunc") != 8 || number(skills, row, "srvdofunc") != 12 ||
                formula(skills, row, "calc1") != "min(par3 + lvl - 1, par4)" || formula(skills, row, "calc2") != "ln12" ||
                formula(skills, row, "calc3") != "2+lvl/4" || formula(skills, row, "aurarangecalc") != "par5")
                throw std::runtime_error("Unsupported Strafe formula");
            weapon.damagePercent = number(skills, row, "Param1"); weapon.damagePerLevel = number(skills, row, "Param2");
            weapon.attacks = number(skills, row, "Param3"); weapon.attackLimit = number(skills, row, "Param4");
            weapon.rollbackPercent = number(skills, row, "Param6"); weapon.interruptible = number(skills, row, "interrupt") != 0;
            bow->targetRadius = number(skills, row, "Param5");
        }
        if (guided) {
            if (formula(skills, row, "calc1") != "ln34" || number(skills, row, "srvdofunc") != 10 ||
                number(missiles, missile, "pSrvHitFunc") != 10 || number(missiles, missile, "ToHit") != 0)
                throw std::runtime_error("Unsupported Guided Arrow rules");
            weapon.damagePercent = number(skills, row, "Param3");
            weapon.damagePerLevel = number(skills, row, "Param4");
            bow->retargetPeriod = std::max(1, number(missiles, missile, "Param1"));
            bow->searchRadius = number(missiles, missile, "Param2");
        }
        if (multiple || strafe || guided) {
            if (multiple && (formula(skills, row, "calc1") != "min(24,ln12)" || formula(skills, row, "calc2") != "par3" ||
                formula(skills, row, "calc3") != "2" || number(skills, row, "srvdofunc") != 8))
                throw std::runtime_error("Unsupported Multiple Shot formula");
            if (multiple) {
                spec.missileCount = number(skills, row, "Param1");
                spec.missileCountPerLevel = number(skills, row, "Param2"); spec.missileCountLimit = 24;
            }
            const auto bolt = loadProjectileResource(missiles, named(missiles, "Missile", skills.value(row, "srvmissileb")), archives);
            bow->boltId = bolt.id; spec.submissileResources.push_back(bolt);
        }
        weapon.bow = std::move(bow);
        const auto resource = loadProjectileResource(missiles, missile, archives);
        spec.missileId = resource.id; spec.missileArt = resource.art;
        spec.missileVelocity = float(number(missiles, missile, "Vel")); spec.missileLifetime = resource.lifetime;
        spec.missileVelocityPerLevel = number(missiles, missile, "VelLev");
        spec.missileRangePerLevel = number(missiles, missile, "LevRange");
        spec.missileNextDelay = number(missiles, missile, "NextHit") ? number(missiles, missile, "NextDelay") : 0;
        if (freezing) {
            std::vector<ProjectileResource> resources;
            spec.missileImpact = loadMissileImpact(missiles, missile, archives, resources);
            auto program = std::make_shared<BowSkillSpec>(*weapon.bow);
            program->freezingEjecta = number(missiles, named(missiles, "Missile", missiles.value(missile, "CltHitSubMissile2")), "Id");
            weapon.bow = std::move(program);
            for (const auto &visual : resources) spec.impacts.push_back({visual.id, visual.art, visual.lifetime});
        }
        const auto explosion = missiles.value(missile, "ExplosionMissile");
        if (!explosion.empty()) {
            const auto visual = loadProjectileResource(missiles, named(missiles, "Missile", explosion), archives);
            spec.missileImpact = MissileImpactSpec{};
            spec.missileImpact->visualId = visual.id; spec.missileImpact->visualDuration = visual.lifetime;
            spec.impacts.push_back({visual.id, visual.art, visual.lifetime});
        }
        spec.castSoundArt = sound(sounds, skills.value(row, "stsound"), archives);
        spec.releaseSoundArt = sound(sounds, missiles.value(missile, "TravelSound"), archives);
        spec.impactSoundArt = sound(sounds, missiles.value(missile, "HitSound"), archives);
        catalog.skills.at(spec.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spec));
    }
}
}
