#include "amazon_spear_data.hpp"
#include "missile_effects.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "resources/archive.hpp"
#include <stdexcept>
#include <regex>

namespace d2x {
namespace {
size_t named(const DataTable &table, std::string_view column, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (!name.empty() && table.value(row, column) == name) return row;
    throw std::runtime_error("Missing Amazon spear reference: " + std::string(name));
}
int number(const DataTable &table, size_t row, std::string_view field) {
    if (!table.has(field) || (!table.value(row, field).empty() && !table.number(row, field)))
        throw std::runtime_error("Invalid Amazon spear field: " + std::string(field));
    return table.number(row, field).value_or(0);
}
SkillSpec base(const DataTable &skills, size_t row) {
    if (skills.value(row, "charclass") != "ama") throw std::runtime_error("Invalid Amazon spear class");
    SkillSpec spec;
    spec.sourceId = number(skills, row, "Id"); spec.effect = SkillBehavior::WeaponProjectile;
    spec.mana = number(skills, row, "mana"); spec.minimumMana = number(skills, row, "minmana");
    spec.manaPerLevel = number(skills, row, "lvlmana"); spec.manaShift = number(skills, row, "manashift");
    spec.weapon = WeaponSkillSpec{};
    auto &weapon = *spec.weapon;
    weapon.requiredType = std::string(skills.value(row, "itypea1"));
    weapon.interruptible = number(skills, row, "interrupt") != 0;
    weapon.thrown = skills.value(row, "anim") == "TH";
    weapon.manaOnRelease = number(skills, row, "usemanaondo") != 0;
    weapon.delayFrames = number(skills, row, "delay");
    weapon.attackRating = number(skills, row, "ToHit"); weapon.attackRatingPerLevel = number(skills, row, "LevToHit");
    return spec;
}
void elemental(SkillSpec &spec, const DataTable &skills, size_t row) {
    spec.hitShift = number(skills, row, "HitShift");
    spec.minimumDamage = number(skills, row, "EMin"); spec.maximumDamage = number(skills, row, "EMax");
    for (int tier = 0; tier < 5; ++tier) {
        spec.minimumPerLevel[tier] = number(skills, row, "EMinLev" + std::to_string(tier + 1));
        spec.maximumPerLevel[tier] = number(skills, row, "EMaxLev" + std::to_string(tier + 1));
    }
    const std::string formula(skills.value(row, "EDmgSymPerCalc"));
    const std::regex term("skill\\('([^']+)'\\.blvl\\)");
    std::string remainder = std::regex_replace(formula, term, "");
    for (char letter : remainder) if (letter != '(' && letter != ')' && letter != '+' && letter != ' ' &&
        letter != '*' && letter != 'p' && letter != 'a' && letter != 'r' && letter != '8')
        throw std::runtime_error("Unsupported spear synergy formula");
    if (!formula.ends_with("par8")) throw std::runtime_error("Unsupported spear synergy parameter");
    for (auto it = std::sregex_iterator(formula.begin(), formula.end(), term); it != std::sregex_iterator(); ++it)
        spec.synergySkills.push_back(number(skills, named(skills, "skill", (*it)[1].str()), "Id"));
    if (spec.synergySkills.empty()) throw std::runtime_error("Missing spear synergy terms");
    spec.synergyPercent = number(skills, row, "Param8");
    spec.lightningDamage = skills.value(row, "EType") == "ltng";
    spec.poisonDamage = skills.value(row, "EType") == "pois";
}

std::string sound(const DataTable &sounds, std::string_view key, Archives &archives) {
    if (key.empty()) return {};
    const auto row = named(sounds, "Sound", key);
    const auto path = "data/global/sfx/" + std::string(sounds.value(row, "FileName"));
    if (!archives.contains(path)) throw std::runtime_error("Missing Amazon spear sound: " + path);
    return path;
}
void projectile(SkillSpec &spec, const DataTable &skills, size_t row, const DataTable &missiles,
                size_t missile, const DataTable &sounds, Archives &archives) {
    const auto resource = loadProjectileResource(missiles, missile, archives);
    spec.missileId = resource.id; spec.missileArt = resource.art; spec.missileLifetime = resource.lifetime;
    spec.missileVelocity = float(number(missiles, missile, "Vel"));
    spec.missileRangePerLevel = number(missiles, missile, "LevRange");
    spec.missileVelocityPerLevel = number(missiles, missile, "VelLev");
    spec.missileNextDelay = number(missiles, missile, "NextHit") ? number(missiles, missile, "NextDelay") : 0;
    spec.castSoundArt = sound(sounds, skills.value(row, "stsound"), archives);
    spec.releaseSoundArt = sound(sounds, missiles.value(missile, "TravelSound"), archives);
    spec.impactSoundArt = sound(sounds, missiles.value(missile, "HitSound"), archives);
    const auto overlayName = missiles.value(missile, "ProgOverlay");
    if (!overlayName.empty()) {
        const DataTable overlays(archives.read("data/global/excel/overlay.txt"));
        const auto overlay = named(overlays, "overlay", overlayName);
        auto &visual = spec.hitOverlay;
        visual.id = int(overlay); visual.frames = number(overlays, overlay, "Frames");
        visual.fps = float(number(overlays, overlay, "AnimRate")); visual.trans = number(overlays, overlay, "Trans");
        visual.preDraw = number(overlays, overlay, "PreDraw") != 0;
        visual.offset = {-float(number(overlays, overlay, "Xoffset")), float(number(overlays, overlay, "Yoffset"))};
        for (int height = 0; height < 4; ++height) visual.heights[height] = number(overlays, overlay, "Height" + std::to_string(height + 1));
        visual.art = "data/global/overlays/" + std::string(overlays.value(overlay, "Filename")) + ".dcc";
        if (visual.frames <= 0 || visual.fps <= 0 || !archives.contains(visual.art)) throw std::runtime_error("Missing spear hit overlay");
    }
}
void install(SkillCatalog &catalog, SkillSpec spec, std::shared_ptr<SpearSkillSpec> program,
             const DataTable &skills, size_t row) {
    program->attackWithoutMana = number(skills, row, "AttackNoMana") != 0;
    spec.weapon->spear = std::move(program);
    catalog.skills.at(spec.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spec));
}
}
void loadAmazonSpearSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles, const DataTable &sounds, Archives &archives) {
    const auto row = named(skills, "skill", "Jab");
    if (number(skills, row, "srvstfunc") != 5 || number(skills, row, "srvdofunc") != 7 ||
        skills.value(row, "anim") != "SQ" || number(skills, row, "seqnum") != 1 ||
        skills.value(row, "calc1") != "ln34" || skills.value(row, "itypea1") != "spea")
        throw std::runtime_error("Unsupported Jab rules");
    SkillSpec spec;
    spec.sourceId = number(skills, row, "Id"); spec.effect = SkillBehavior::WeaponProjectile;
    spec.mana = number(skills, row, "mana"); spec.minimumMana = number(skills, row, "minmana");
    spec.manaPerLevel = number(skills, row, "lvlmana"); spec.manaShift = number(skills, row, "manashift");
    spec.weapon = WeaponSkillSpec{};
    auto &weapon = *spec.weapon;
    weapon.interruptible = number(skills, row, "interrupt") != 0;
    weapon.requiredType = std::string(skills.value(row, "itypea1"));
    weapon.damagePercent = number(skills, row, "Param3"); weapon.damagePerLevel = number(skills, row, "Param4");
    weapon.attackRating = number(skills, row, "ToHit"); weapon.attackRatingPerLevel = number(skills, row, "LevToHit");
    auto program = std::make_shared<SpearSkillSpec>();
    program->oneHand = std::make_shared<SpearSequence>(amazonWeaponSequence(1,"1ht"));
    program->twoHand = std::make_shared<SpearSequence>(amazonWeaponSequence(1,"2ht"));
    install(catalog, std::move(spec), std::move(program), skills, row);
    const auto power = named(skills, "skill", "Power Strike");
    if (number(skills, power, "srvstfunc") != 6 || number(skills, power, "srvdofunc") != 2 ||
        skills.value(power, "anim") != "A1" || skills.value(power, "itypea1") != "spea" ||
        skills.value(power, "EType") != "ltng" || number(skills, power, "SrcDam") != 128)
        throw std::runtime_error("Unsupported Power Strike rules");
    auto strike = base(skills, power); elemental(strike, skills, power);
    auto powerProgram = std::make_shared<SpearSkillSpec>(); powerProgram->kind = SpearSkillSpec::Kind::Power;
    install(catalog, std::move(strike), std::move(powerProgram), skills, power);
    const auto poison = named(skills, "skill", "Poison Javelin");
    const auto jav = named(missiles, "Missile", skills.value(poison, "srvmissile"));
    const auto cloud = named(missiles, "Missile", missiles.value(jav, "SubMissile1"));
    if (number(skills, poison, "srvstfunc") != 4 || number(skills, poison, "decquant") != 1 ||
        skills.value(poison, "itypea1") != "jave" || skills.value(poison, "EType") != "pois" ||
        number(missiles, jav, "pSrvDoFunc") != 2 || number(missiles, jav, "SrvCalc1") != 0 ||
        number(missiles, cloud, "pSrvDoFunc") != 3 || number(missiles, cloud, "SrcDamage") != -1 ||
        missiles.value(cloud, "Skill") != "Poison Javelin") throw std::runtime_error("Unsupported Poison Javelin rules");
    auto toxic = base(skills, poison); elemental(toxic, skills, poison);
    toxic.poisonFrames = number(skills, poison, "ELen");
    for (int tier = 0; tier < 3; ++tier) toxic.poisonFramesPerLevel[tier] = number(skills, poison, "ELevLen" + std::to_string(tier + 1));
    projectile(toxic, skills, poison, missiles, jav, sounds, archives);
    auto poisonProgram = std::make_shared<SpearSkillSpec>(); poisonProgram->kind = SpearSkillSpec::Kind::Poison;
    const auto cloudArt = loadProjectileResource(missiles, cloud, archives); toxic.submissileResources.push_back(cloudArt);
    PoisonCloudSpec trail; trail.missileId = cloudArt.id; trail.lifetimeFrames = number(missiles, cloud, "Range");
    trail.size = number(missiles, cloud, "Size"); trail.damageFromSkill = true;
    poisonProgram->poisonTrail = trail; install(catalog, std::move(toxic), std::move(poisonProgram), skills, poison);

    const auto impale = named(skills, "skill", "Impale");
    if (number(skills, impale, "srvstfunc") != 7 || number(skills, impale, "srvdofunc") != 2 ||
        number(skills, impale, "seqnum") != 8 || skills.value(impale, "anim") != "SQ" ||
        skills.value(impale, "calc1") != "ln12" || skills.value(impale, "calc2") != "par6-dm34" ||
        skills.value(impale, "calc3") != "par5") throw std::runtime_error("Unsupported Impale rules");
    auto thrust = base(skills, impale);
    thrust.weapon->damagePercent = number(skills, impale, "Param1"); thrust.weapon->damagePerLevel = number(skills, impale, "Param2");
    thrust.castSoundArt = sound(sounds, skills.value(impale, "stsound"), archives);
    auto impaleProgram = std::make_shared<SpearSkillSpec>(); impaleProgram->kind = SpearSkillSpec::Kind::Impale;
    impaleProgram->wearChance = number(skills, impale, "Param6");
    impaleProgram->wearMinimum = number(skills, impale, "Param3"); impaleProgram->wearMaximum = number(skills, impale, "Param4");
    impaleProgram->wearAmount = number(skills, impale, "Param5");
    impaleProgram->oneHand = std::make_shared<SpearSequence>(amazonWeaponSequence(8,"1ht"));
    impaleProgram->twoHand = std::make_shared<SpearSequence>(amazonWeaponSequence(8,"2ht"));
    install(catalog, std::move(thrust), std::move(impaleProgram), skills, impale);

    const auto bolt = named(skills, "skill", "Lightning Bolt");
    const auto boltMissile = named(missiles, "Missile", skills.value(bolt, "srvmissile"));
    if (skills.value(bolt, "anim") != "TH" || skills.value(bolt, "itypea1") != "jave" ||
        number(skills, bolt, "decquant") != 1 || skills.value(bolt, "EType") != "ltng" ||
        number(missiles, boltMissile, "pSrvDoFunc") != 1 || number(missiles, boltMissile, "pSrvDmgFunc") != 12 ||
        missiles.value(boltMissile, "DmgCalc1") != "dl12") throw std::runtime_error("Unsupported Lightning Bolt rules");
    auto lightning = base(skills, bolt); elemental(lightning, skills, bolt);
    projectile(lightning, skills, bolt, missiles, boltMissile, sounds, archives);
    auto boltProgram = std::make_shared<SpearSkillSpec>(); boltProgram->kind = SpearSkillSpec::Kind::Bolt;
    boltProgram->sourceDamage = number(skills, bolt, "SrcDam");
    boltProgram->conversionPercent = number(missiles, boltMissile, "dParam1");
    boltProgram->conversionPerLevel = number(missiles, boltMissile, "dParam2");
    boltProgram->automaticHit = number(missiles, boltMissile, "ToHit") == 0;
    install(catalog, std::move(lightning), std::move(boltProgram), skills, bolt);

    const auto charged = named(skills, "skill", "Charged Strike");
    const auto chargedMissile = named(missiles, "Missile", skills.value(charged, "srvmissilea"));
    if (number(skills, charged, "srvstfunc") != 6 || number(skills, charged, "srvdofunc") != 11 ||
        skills.value(charged, "anim") != "A1" || skills.value(charged, "itypea1") != "spea" ||
        skills.value(charged, "calc1") != "par1+lvl/par2" || number(skills, charged, "Param2") <= 0 ||
        number(missiles, chargedMissile, "pSrvDoFunc") != 1 || number(missiles, chargedMissile, "CollideKill") != 1)
        throw std::runtime_error("Unsupported Charged Strike rules");
    auto charge = base(skills, charged); elemental(charge, skills, charged);
    projectile(charge, skills, charged, missiles, chargedMissile, sounds, archives);
    auto chargedProgram = std::make_shared<SpearSkillSpec>(); chargedProgram->kind = SpearSkillSpec::Kind::Charged;
    chargedProgram->countBase = number(skills, charged, "Param1"); chargedProgram->countDivisor = number(skills, charged, "Param2");
    chargedProgram->activateFrames = number(missiles, chargedMissile, "Activate");
    install(catalog, std::move(charge), std::move(chargedProgram), skills, charged);

    const auto fend = named(skills, "skill", "Fend");
    if (number(skills, fend, "srvstfunc") != 9 || number(skills, fend, "srvdofunc") != 13 ||
        skills.value(fend, "anim") != "A1" || skills.value(fend, "calc2") != "ln34" ||
        skills.value(fend, "itypea1") != "spea" || number(skills, fend, "SrcDam") != 128)
        throw std::runtime_error("Unsupported Fend rules");
    auto combo = base(skills, fend);
    combo.weapon->attackLimit = number(skills, fend, "calc1");
    if (combo.weapon->attackLimit <= 0) throw std::runtime_error("Invalid Fend attack limit");
    combo.weapon->damagePercent = number(skills, fend, "Param3"); combo.weapon->damagePerLevel = number(skills, fend, "Param4");
    combo.weapon->rollbackPercent = number(skills, fend, "Param2");
    auto fendProgram = std::make_shared<SpearSkillSpec>(); fendProgram->kind = SpearSkillSpec::Kind::Fend;
    install(catalog, std::move(combo), std::move(fendProgram), skills, fend);

    const auto chain = named(skills, "skill", "Lightning Strike");
    const auto chainMissile = named(missiles, "Missile", skills.value(chain, "srvmissilea"));
    if (number(skills, chain, "srvstfunc") != 10 || number(skills, chain, "srvdofunc") != 14 ||
        skills.value(chain, "anim") != "A1" || skills.value(chain, "itypea1") != "spea" ||
        skills.value(chain, "calc2") != "ln34" || skills.value(chain, "aurarangecalc") != "par1" ||
        number(missiles, chainMissile, "pSrvDoFunc") != 1 || number(missiles, chainMissile, "pSrvHitFunc") != 12 ||
        number(missiles, chainMissile, "pCltDoFunc") != 8) throw std::runtime_error("Unsupported Lightning Strike rules");
    auto chainStrike = base(skills, chain); elemental(chainStrike, skills, chain);
    projectile(chainStrike, skills, chain, missiles, chainMissile, sounds, archives);
    chainStrike.weapon->damagePercent = number(skills, chain, "calc1"); // SrvSt10 enhanced melee damage.
    ArcSpec arc; arc.range = number(skills, chain, "Param1"); arc.count = number(skills, chain, "Param3");
    arc.countPerLevel = number(skills, chain, "Param4"); arc.nextDelay = chainStrike.missileNextDelay;
    arc.subloops = number(missiles, chainMissile, "CltParam1");
    const auto segment = loadProjectileResource(missiles, named(missiles, "Missile", missiles.value(chainMissile, "CltSubMissile1")), archives);
    arc.visualId = segment.id; chainStrike.submissileResources.push_back(segment); chainStrike.arc = arc;
    auto chainProgram = std::make_shared<SpearSkillSpec>(); chainProgram->kind = SpearSkillSpec::Kind::Strike;
    chainProgram->targetRadius = number(skills, chain, "calc1"); // Initial target search radius, distinct from the chain count.
    install(catalog, std::move(chainStrike), std::move(chainProgram), skills, chain);

    const auto fury = named(skills, "skill", "Lightning Fury");
    const auto furyMissile = named(missiles, "Missile", skills.value(fury, "srvmissile"));
    const auto furyChild = named(missiles, "Missile", missiles.value(furyMissile, "HitSubMissile1"));
    if (number(skills, fury, "srvstfunc") != 4 || skills.value(fury, "anim") != "TH" ||
        skills.value(fury, "itypea1") != "jave" || number(skills, fury, "decquant") != 1 ||
        number(skills, fury, "SrcDam") != 128 || skills.value(fury, "calc1") != "ln12" ||
        skills.value(fury, "aurarangecalc") != "par3" || number(skills, fury, "aurafilter") != 0xA583 ||
        number(missiles, furyMissile, "pSrvHitFunc") != 20 || number(missiles, furyMissile, "sHitPar1") != 0 ||
        number(missiles, furyMissile, "sHitPar2") != 0 || number(missiles, furyChild, "pSrvDoFunc") != 1 ||
        number(missiles, furyChild, "SrcDamage") != -1 || missiles.value(furyChild, "Skill") != "Lightning Fury")
        throw std::runtime_error("Unsupported Lightning Fury rules");
    auto storm = base(skills, fury); elemental(storm, skills, fury);
    projectile(storm, skills, fury, missiles, furyMissile, sounds, archives);
    const auto childResource = loadProjectileResource(missiles, furyChild, archives);
    storm.submissileResources.push_back(childResource);
    storm.impactSoundArt = sound(sounds, missiles.value(furyChild, "TravelSound"), archives);
    storm.missileImpact = MissileImpactSpec{};
    auto furyProgram = std::make_shared<SpearSkillSpec>(); furyProgram->kind = SpearSkillSpec::Kind::Fury;
    furyProgram->countBase = number(skills, fury, "Param1"); furyProgram->countPerLevel = number(skills, fury, "Param2");
    furyProgram->targetRadius = number(skills, fury, "Param3");
    furyProgram->automaticHit = number(missiles, furyMissile, "ToHit") == 0;
    furyProgram->childId = childResource.id; furyProgram->childFrames = number(missiles, furyChild, "Range");
    furyProgram->childRangePerLevel = number(missiles, furyChild, "LevRange");
    furyProgram->childVelocity = number(missiles, furyChild, "Vel"); furyProgram->childVelocityPerLevel = number(missiles, furyChild, "VelLev");
    furyProgram->childKillOnHit = number(missiles, furyChild, "CollideKill") != 0;
    install(catalog, std::move(storm), std::move(furyProgram), skills, fury);

}
} // namespace d2x
