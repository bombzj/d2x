#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/bow_spec.hpp"
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
std::string_view formula(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.value(row, field);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}
} // namespace
void loadPaladinSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                      const DataTable &overlays, const DataTable &sounds, const CombatStateCatalog &states, Archives &archives) {
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
    entry.spell = std::make_shared<const SkillSpec>(std::move(spec));
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
    catalog.skills.at(melee.sourceId).spell = std::make_shared<const SkillSpec>(std::move(melee));
    const auto zeal = named(skills, "skill", "Zeal");
    if (required(skills, zeal, "srvstfunc") != 37 || required(skills, zeal, "srvdofunc") != 13 ||
        formula(skills, zeal, "calc1") != "min((par5 + lvl -1), par6)" ||
        skills.value(zeal, "calc2") != "((lvl < 5) ? 0 : ((lvl-4) * par4) )+skill('Sacrifice'.blvl)*par8")
        throw std::runtime_error("Unsupported Zeal rules");
    SkillSpec combo;
    combo.sourceId = required(skills, zeal, "Id");
    combo.effect = SkillBehavior::Zeal;
    combo.mana = required(skills, zeal, "mana");
    combo.minimumMana = required(skills, zeal, "minmana");
    combo.manaPerLevel = required(skills, zeal, "lvlmana");
    combo.manaShift = required(skills, zeal, "manashift");
    combo.weapon = WeaponSkillSpec{};
    auto &sequence = *combo.weapon;
    sequence.requiredType = std::string(skills.value(zeal, "itypea1"));
    sequence.attackRating = required(skills, zeal, "ToHit");
    sequence.attackRatingPerLevel = required(skills, zeal, "LevToHit");
    sequence.damagePerLevel = required(skills, zeal, "Param4");
    sequence.damageStartLevel = 4;
    sequence.attacks = required(skills, zeal, "Param5");
    sequence.attackLimit = required(skills, zeal, "Param6");
    sequence.rollbackPercent = required(skills, zeal, "Param2");
    sequence.interruptible = skills.number(zeal, "interrupt").value_or(0) != 0;
    sequence.damageSynergies.emplace(required(skills, sacrifice, "Id"), required(skills, zeal, "Param8"));
    catalog.skills.at(combo.sourceId).spell = std::make_shared<const SkillSpec>(std::move(combo));
    const auto vengeance = named(skills, "skill", "Vengeance");
    SkillSpec elemental;
    elemental.sourceId = required(skills, vengeance, "Id");
    elemental.effect = SkillBehavior::Vengeance;
    elemental.mana = required(skills, vengeance, "mana");
    elemental.minimumMana = required(skills, vengeance, "minmana");
    elemental.manaPerLevel = required(skills, vengeance, "lvlmana");
    elemental.manaShift = required(skills, vengeance, "manashift");
    elemental.coldFrames = required(skills, vengeance, "ELen");
    for (int tier = 0; tier < 3; ++tier)
        elemental.coldFramesPerLevel[tier] = required(skills, vengeance, "ELevLen" + std::to_string(tier + 1));
    elemental.weapon = WeaponSkillSpec{};
    auto &elements = *elemental.weapon;
    elements.requiredType = std::string(skills.value(vengeance, "itypea1"));
    elements.attackRating = required(skills, vengeance, "ToHit");
    elements.attackRatingPerLevel = required(skills, vengeance, "LevToHit");
    elements.elementPerLevel = required(skills, vengeance, "Param2");
    const std::array<std::string_view, 3> resists{"Resist Fire", "Resist Cold", "Resist Lightning"};
    for (size_t element = 0; element < resists.size(); ++element) {
        if (skills.value(vengeance, "calc" + std::to_string(element + 1)) !=
            "ln12+skill('" + std::string(resists[element]) + "'.blvl)*par8+skill('Salvation'.blvl)*par7")
            throw std::runtime_error("Unsupported Vengeance formula");
        elements.elementPercent[element] = required(skills, vengeance, "Param1");
        elements.elementSynergies[element].emplace(required(skills, named(skills, "skill", resists[element]), "Id"),
            required(skills, vengeance, "Param8"));
        elements.elementSynergies[element].emplace(required(skills, named(skills, "skill", "Salvation"), "Id"),
            required(skills, vengeance, "Param7"));
    }
    catalog.skills.at(elemental.sourceId).spell = std::make_shared<const SkillSpec>(std::move(elemental));
    const auto holyShield = named(skills, "skill", "Holy Shield");
    if (skills.value(holyShield, "auralencalc") != "ln12" || skills.value(holyShield, "aurastatcalc1") != "dm56" ||
        skills.value(holyShield, "calc1") != "ln34+skill('Defiance'.blvl)*par8")
        throw std::runtime_error("Unsupported Holy Shield formula");
    SkillSpec shield;
    shield.sourceId = required(skills, holyShield, "Id");
    shield.effect = SkillBehavior::HolyShield;
    shield.requiresShield = shield.holyShield = true;
    shield.state = states.at(std::string(skills.value(holyShield, "aurastate"))).definition;
    shield.mana = required(skills, holyShield, "mana");
    shield.minimumMana = required(skills, holyShield, "minmana");
    shield.manaPerLevel = required(skills, holyShield, "lvlmana");
    shield.manaShift = required(skills, holyShield, "manashift");
    shield.minimumDamage = required(skills, holyShield, "MinDam");
    shield.maximumDamage = required(skills, holyShield, "MaxDam");
    for (int tier = 0; tier < 5; ++tier) {
        shield.minimumPerLevel[tier] = required(skills, holyShield, "MinLevDam" + std::to_string(tier + 1));
        shield.maximumPerLevel[tier] = required(skills, holyShield, "MaxLevDam" + std::to_string(tier + 1));
    }
    for (int parameter = 0; parameter < 8; ++parameter)
        shield.armorParameters[parameter] = skills.number(holyShield, "Param" + std::to_string(parameter + 1)).value_or(0);
    shield.armorSynergySkills.push_back(required(skills, named(skills, "skill", "Defiance"), "Id"));
    shield.castSoundArt = sound(skills.value(holyShield, "stsound"));
    catalog.skills.at(shield.sourceId).spell = std::make_shared<const SkillSpec>(std::move(shield));
    const auto smiteRow = named(skills, "skill", "Smite");
    if (required(skills, smiteRow, "srvdofunc") != 150 || skills.value(smiteRow, "anim") != "S1" ||
        skills.value(smiteRow, "calc1") != "ln34" || formula(skills, smiteRow, "calc2") != "min(250,ln12)")
        throw std::runtime_error("Unsupported Smite rules");
    SkillSpec smite;
    smite.sourceId = required(skills, smiteRow, "Id");
    smite.effect = SkillBehavior::Smite;
    smite.mana = required(skills, smiteRow, "mana");
    smite.minimumMana = required(skills, smiteRow, "minmana");
    smite.manaPerLevel = required(skills, smiteRow, "lvlmana");
    smite.manaShift = required(skills, smiteRow, "manashift");
    smite.weapon = WeaponSkillSpec{};
    smite.weapon->smite = true;
    smite.weapon->mode = "s1";
    smite.weapon->damagePercent = required(skills, smiteRow, "Param3");
    smite.weapon->damagePerLevel = required(skills, smiteRow, "Param4");
    smite.weapon->stunFrames = required(skills, smiteRow, "Param1");
    smite.weapon->stunPerLevel = required(skills, smiteRow, "Param2");
    catalog.skills.at(smite.sourceId).spell = std::make_shared<const SkillSpec>(std::move(smite));
    const auto hammerRow = named(skills, "skill", "Blessed Hammer");
    SkillSpec hammer;
    hammer.sourceId = required(skills, hammerRow, "Id");
    hammer.effect = SkillBehavior::BlessedHammer;
    hammer.mana = required(skills, hammerRow, "mana");
    hammer.minimumMana = required(skills, hammerRow, "minmana");
    hammer.manaPerLevel = required(skills, hammerRow, "lvlmana");
    hammer.manaShift = required(skills, hammerRow, "manashift");
    hammer.hitShift = required(skills, hammerRow, "HitShift");
    hammer.minimumDamage = required(skills, hammerRow, "EMin");
    hammer.maximumDamage = required(skills, hammerRow, "EMax");
    for (int tier = 0; tier < 5; ++tier) {
        hammer.minimumPerLevel[tier] = required(skills, hammerRow, "EMinLev" + std::to_string(tier + 1));
        hammer.maximumPerLevel[tier] = required(skills, hammerRow, "EMaxLev" + std::to_string(tier + 1));
    }
    if (skills.value(hammerRow, "EDmgSymPerCalc") != "(skill('Vigor'.blvl)+skill('Blessed Aim'.blvl))*par8")
        throw std::runtime_error("Unsupported Blessed Hammer synergy");
    hammer.synergyPercent = required(skills, hammerRow, "Param8");
    for (const auto name : {"Vigor", "Blessed Aim"})
        hammer.synergySkills.push_back(required(skills, named(skills, "skill", name), "Id"));
    hammer.concentrationState = states.at("concentration").definition.id;
    hammer.concentrationFactor = required(skills, hammerRow, "Param1");
    const auto hammerMissile = named(missiles, "Missile", skills.value(hammerRow, "srvmissilea"));
    const auto hammerResource = loadProjectileResource(missiles, hammerMissile, archives);
    hammer.missileId = hammerResource.id;
    hammer.missileArt = hammerResource.art;
    hammer.missileLifetime = hammerResource.lifetime;
    hammer.missileVelocity = float(required(missiles, hammerMissile, "Vel"));
    hammer.castSoundArt = sound(skills.value(hammerRow, "stsound"));
    hammer.releaseSoundArt = sound(missiles.value(hammerMissile, "TravelSound"));
    hammer.impactSoundArt = sound(missiles.value(hammerMissile, "HitSound"));
    catalog.skills.at(hammer.sourceId).spell = std::make_shared<const SkillSpec>(std::move(hammer));
    const auto conversionRow = named(skills, "skill", "Conversion");
    if (skills.value(conversionRow, "calc1") != "dm34" || skills.value(conversionRow, "auralencalc") != "ln12")
        throw std::runtime_error("Unsupported Conversion formula");
    SkillSpec conversion;
    conversion.sourceId = required(skills, conversionRow, "Id");
    conversion.effect = SkillBehavior::Conversion;
    conversion.mana = required(skills, conversionRow, "mana");
    conversion.minimumMana = required(skills, conversionRow, "minmana");
    conversion.manaPerLevel = required(skills, conversionRow, "lvlmana");
    conversion.manaShift = required(skills, conversionRow, "manashift");
    conversion.weapon = WeaponSkillSpec{};
    conversion.weapon->requiredType = std::string(skills.value(conversionRow, "itypea1"));
    conversion.weapon->conversionMinimum = required(skills, conversionRow, "Param3");
    conversion.weapon->conversionMaximum = required(skills, conversionRow, "Param4");
    conversion.weapon->conversionFrames = required(skills, conversionRow, "Param1");
    conversion.state = states.at(std::string(skills.value(conversionRow, "auratargetstate"))).definition;
    conversion.weapon->conversionState = conversion.state;
    catalog.skills.at(conversion.sourceId).spell = std::make_shared<const SkillSpec>(std::move(conversion));
    const auto heavenRow = named(skills, "skill", "Fist of the Heavens");
    SkillSpec heaven;
    heaven.sourceId = required(skills, heavenRow, "Id");
    heaven.effect = SkillBehavior::FistOfTheHeavens;
    heaven.lightningDamage = true;
    heaven.mana = required(skills, heavenRow, "mana");
    heaven.minimumMana = required(skills, heavenRow, "minmana");
    heaven.manaPerLevel = required(skills, heavenRow, "lvlmana");
    heaven.manaShift = required(skills, heavenRow, "manashift");
    heaven.hitShift = required(skills, heavenRow, "HitShift");
    heaven.minimumDamage = required(skills, heavenRow, "EMin");
    heaven.maximumDamage = required(skills, heavenRow, "EMax");
    for (int tier = 0; tier < 5; ++tier) {
        heaven.minimumPerLevel[tier] = required(skills, heavenRow, "EMinLev" + std::to_string(tier + 1));
        heaven.maximumPerLevel[tier] = required(skills, heavenRow, "EMaxLev" + std::to_string(tier + 1));
    }
    heaven.synergySkills.push_back(required(skills, named(skills, "skill", "Holy Shock"), "Id"));
    heaven.synergyPercent = required(skills, heavenRow, "Param8");
    heaven.delayFrames = required(skills, heavenRow, "delay");
    heaven.heaven = HeavenSpec{};
    auto &program = *heaven.heaven;
    const auto delayRow = named(missiles, "Missile", skills.value(heavenRow, "srvmissilea"));
    const auto boltRow = named(missiles, "Missile", missiles.value(delayRow, "HitSubMissile1"));
    if (required(missiles, delayRow, "pSrvHitFunc") != 22 || skills.value(heavenRow, "aurarangecalc") != "20" ||
        skills.value(heavenRow, "calc4") != "ln12" || missiles.value(boltRow, "EDmgSymPerCalc") != "skill('Holy Bolt'.blvl) * 15")
        throw std::runtime_error("Unsupported Fist of the Heavens program");
    program.delayFrames = required(missiles, delayRow, "Range");
    program.radius = required(skills, heavenRow, "aurarangecalc");
    program.limit = required(skills, heavenRow, "Param1");
    program.limitPerLevel = required(skills, heavenRow, "Param2");
    const auto bolt = loadProjectileResource(missiles, boltRow, archives);
    program.boltId = bolt.id;
    program.boltFrames = required(missiles, boltRow, "Range");
    program.boltVelocity = required(missiles, boltRow, "Vel");
    program.minimum = required(missiles, boltRow, "EMin");
    program.maximum = required(missiles, boltRow, "EMax");
    for (int tier = 0; tier < 5; ++tier) {
        program.minimumPerLevel[tier] = required(missiles, boltRow, "MinELev" + std::to_string(tier + 1));
        program.maximumPerLevel[tier] = required(missiles, boltRow, "MaxELev" + std::to_string(tier + 1));
    }
    program.synergySkill = entry.id;
    program.synergyPercent = 15;
    program.healingMinimum = required(skills, heavenRow, "Param3");
    program.healingMinimumPerLevel = required(skills, heavenRow, "Param4");
    program.healingMaximum = required(skills, heavenRow, "Param5");
    program.healingMaximumPerLevel = required(skills, heavenRow, "Param6");
    heaven.submissileResources.push_back(bolt);
    const auto strike = named(overlays, "overlay", skills.value(heavenRow, "srvoverlay"));
    heaven.hitOverlay = catalog.skills.at(entry.id).spell->hitOverlay;
    heaven.hitOverlay.id = int(strike);
    heaven.hitOverlay.frames = required(overlays, strike, "Frames");
    heaven.hitOverlay.fps = float(required(overlays, strike, "AnimRate"));
    heaven.hitOverlay.trans = required(overlays, strike, "Trans");
    heaven.hitOverlay.preDraw = overlays.number(strike, "PreDraw").value_or(0) != 0;
    heaven.hitOverlay.offset = {-float(required(overlays, strike, "Xoffset")), float(required(overlays, strike, "Yoffset"))};
    for (int height = 0; height < 4; ++height)
        heaven.hitOverlay.heights[height] = required(overlays, strike, "Height" + std::to_string(height + 1));
    heaven.hitOverlay.art = "data/global/overlays/" + std::string(overlays.value(strike, "Filename")) + ".dcc";
    heaven.missileId = required(missiles, delayRow, "Id");
    heaven.castSoundArt = sound(skills.value(heavenRow, "stsound"));
    heaven.releaseSoundArt = sound(missiles.value(delayRow, "TravelSound"));
    catalog.skills.at(heaven.sourceId).spell = std::make_shared<const SkillSpec>(std::move(heaven));
    const auto chargeRow = named(skills, "skill", "Charge");
    if (skills.value(chargeRow, "calc1") != "ln34+(skill('Vigor'.blvl)+skill('Might'.blvl))*par8" ||
        required(skills, chargeRow, "seqnum") != 4 || skills.value(chargeRow, "seqtrans") != "A1")
        throw std::runtime_error("Unsupported Charge rules");
    SkillSpec charge;
    charge.sourceId = required(skills, chargeRow, "Id");
    charge.effect = SkillBehavior::Charge;
    charge.mana = required(skills, chargeRow, "mana");
    charge.minimumMana = required(skills, chargeRow, "minmana");
    charge.manaPerLevel = required(skills, chargeRow, "lvlmana");
    charge.manaShift = required(skills, chargeRow, "manashift");
    charge.weapon = WeaponSkillSpec{};
    charge.weapon->requiredType = std::string(skills.value(chargeRow, "itypea1"));
    charge.weapon->chargeVelocity = required(skills, chargeRow, "Param1");
    charge.weapon->damagePercent = required(skills, chargeRow, "Param3");
    charge.weapon->damagePerLevel = required(skills, chargeRow, "Param4");
    charge.weapon->attackRating = required(skills, chargeRow, "ToHit");
    charge.weapon->attackRatingPerLevel = required(skills, chargeRow, "LevToHit");
    for (const auto name : {"Vigor", "Might"})
        charge.weapon->damageSynergies.emplace(required(skills, named(skills, "skill", name), "Id"), required(skills, chargeRow, "Param8"));
    catalog.skills.at(charge.sourceId).spell = std::make_shared<const SkillSpec>(std::move(charge));
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
        spec.weapon->interruptible = skills.number(row, "interrupt").value_or(0) != 0;
        if (!plague) {
            auto bow = std::make_shared<BowSkillSpec>();
            bow->sourceDamage = required(skills, row, "SrcDam");
            // This native parent has no Skill link. Fire is resolved by child 656,
            // not added to the direct arrow payload a second time.
            spec.weapon->bow = std::move(bow);
        }
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
        spec.missileNextDelay = missiles.number(missile, "NextHit").value_or(0) ?
            missiles.number(missile, "NextDelay").value_or(0) : 0;
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
        entry.spell = std::make_shared<const SkillSpec>(std::move(spec));
    }
}
} // namespace d2x
