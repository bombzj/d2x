#include "resources/archive.hpp"
#include "sorceress_data.hpp"
#include "missile_effects.hpp"
#include "frozen_orb_data.hpp"
#include "blizzard_data.hpp"
#include "glacial_spike_data.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing original sorceress field: " + std::string(field));
    return *value;
}
std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return result;
}
} // namespace
void loadSorceressEffects(SkillCatalog &catalog, const DataTable &skills,
                          const DataTable &missiles, const DataTable &overlays,
                          const DataTable &sounds, const CombatStateCatalog &states, Archives &archives) {
    constexpr struct { std::string_view name; SkillBehavior effect; } supported[] = {
        {"Teleport", SkillBehavior::Teleport}, {"Fire Bolt", SkillBehavior::FireBolt},
        {"Fire Ball", SkillBehavior::Fireball}, {"Frost Nova", SkillBehavior::FrostNova},
        {"Ice Bolt", SkillBehavior::IceBolt}, {"Nova", SkillBehavior::Nova},
        {"Ice Blast", SkillBehavior::IceBlast}, {"Charged Bolt", SkillBehavior::ChargedBolt}, {"Frozen Armor", SkillBehavior::FrozenArmor},
        {"Inferno", SkillBehavior::Inferno}, {"Static Field", SkillBehavior::StaticField},
        {"Frozen Orb", SkillBehavior::FrozenOrb}, {"Blizzard", SkillBehavior::Blizzard},
        {"Glacial Spike", SkillBehavior::GlacialSpike}, {"Shiver Armor", SkillBehavior::ShiverArmor},
        {"Chilling Armor", SkillBehavior::ChillingArmor}};
    const auto warmth = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Warmth"; });
    if (warmth == catalog.skills.end() || !warmth->second.passive)
        throw std::runtime_error("Original Warmth passive is missing");
    size_t warmthRow = 0;
    for (; warmthRow < skills.rows().size(); ++warmthRow)
        if (skills.number(warmthRow, "Id") == warmth->first) break;
    if (warmthRow == skills.rows().size() ||
        skills.value(warmthRow, "passivestat1") != "manarecoverybonus" ||
        skills.value(warmthRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Warmth passive");
    warmth->second.manaRecoveryPerRank = std::pair{required(skills, warmthRow, "Param1"),
                                                   required(skills, warmthRow, "Param2")};
    const auto mastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Fire Mastery"; });
    if (mastery == catalog.skills.end() || !mastery->second.passive)
        throw std::runtime_error("Original Fire Mastery passive is missing");
    size_t masteryRow = 0;
    for (; masteryRow < skills.rows().size(); ++masteryRow)
        if (skills.number(masteryRow, "Id") == mastery->first) break;
    if (masteryRow == skills.rows().size() ||
        skills.value(masteryRow, "passivestat1") != "passive_fire_mastery" ||
        skills.value(masteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Fire Mastery passive");
    mastery->second.fireMasteryPerRank = std::pair{required(skills, masteryRow, "Param1"),
                                                  required(skills, masteryRow, "Param2")};
    const auto lightningMastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Lightning Mastery"; });
    if (lightningMastery == catalog.skills.end() || !lightningMastery->second.passive)
        throw std::runtime_error("Original Lightning Mastery passive is missing");
    size_t lightningMasteryRow = 0;
    for (; lightningMasteryRow < skills.rows().size(); ++lightningMasteryRow)
        if (skills.number(lightningMasteryRow, "Id") == lightningMastery->first) break;
    if (lightningMasteryRow == skills.rows().size() ||
        skills.value(lightningMasteryRow, "passivestat1") != "passive_ltng_mastery" ||
        skills.value(lightningMasteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Lightning Mastery passive");
    lightningMastery->second.lightningMasteryPerRank = std::pair{
        required(skills, lightningMasteryRow, "Param1"),
        required(skills, lightningMasteryRow, "Param2")};
    const auto coldMastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Cold Mastery"; });
    if (coldMastery == catalog.skills.end() || !coldMastery->second.passive)
        throw std::runtime_error("Original Cold Mastery passive is missing");
    size_t coldMasteryRow = 0;
    for (; coldMasteryRow < skills.rows().size(); ++coldMasteryRow)
        if (skills.number(coldMasteryRow, "Id") == coldMastery->first) break;
    if (coldMasteryRow == skills.rows().size() ||
        skills.value(coldMasteryRow, "passivestat1") != "passive_cold_pierce" ||
        skills.value(coldMasteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Cold Mastery passive");
    coldMastery->second.coldPiercePerRank = std::pair{
        required(skills, coldMasteryRow, "Param1"),
        required(skills, coldMasteryRow, "Param2")};
    for (const auto &[name, effect] : supported) {
        const SkillRecord *record = nullptr;
        for (const auto &[id, entry] : catalog.skills)
            if (entry.classCode == "sor" && entry.sourceName == name) record = &entry;
        if (!record) throw std::runtime_error("Original sorceress skill is missing: " + std::string(name));
        size_t row = 0;
        for (; row < skills.rows().size(); ++row)
            if (skills.number(row, "Id") == record->id) break;
        if (row == skills.rows().size()) throw std::runtime_error("Original sorceress skill row is missing");
        SkillSpec spec;
        if (skills.value(row, "anim") != (effect == SkillBehavior::Inferno ? "SQ" : "SC"))
            throw std::runtime_error("Unsupported original sorceress cast mode");
        spec.effect = effect;
        spec.sourceId = record->id;
        spec.mana = required(skills, row, "mana");
        spec.minimumMana = required(skills, row, "minmana");
        spec.manaPerLevel = required(skills, row, "lvlmana");
        spec.manaShift = required(skills, row, "manashift");
        spec.hitShift = required(skills, row, "HitShift");
        if (!skills.value(row, "delay").empty()) {
            spec.delayFrames = required(skills, row, "delay");
            if (spec.delayFrames < 0) throw std::runtime_error("Unsupported original skill delay");
        }
        spec.fireDamage = skills.value(row, "EType") == "fire";
        spec.lightningDamage = skills.value(row, "EType") == "ltng";
        spec.coldDamage = skills.value(row, "EType") == "cold";
        if ((effect == SkillBehavior::FireBolt || effect == SkillBehavior::Fireball) && !spec.fireDamage)
            throw std::runtime_error("Unsupported original fire spell element");
        const auto soundName = skills.value(row, "stsound");
        size_t soundRow = 0;
        for (; soundRow < sounds.rows().size(); ++soundRow)
            if (sounds.value(soundRow, "Sound") == soundName) break;
        if ((soundName.empty() || soundRow == sounds.rows().size()) && effect != SkillBehavior::Inferno)
            throw std::runtime_error("Missing original sorceress cast sound: " + std::string(name));
        if (!soundName.empty() && soundRow < sounds.rows().size()) {
            spec.castSoundArt = "data/global/sfx/" + std::string(sounds.value(soundRow, "FileName"));
            if (!archives.contains(spec.castSoundArt))
                throw std::runtime_error("Missing original sorceress cast sound art: " + std::string(name));
        }
        auto loadOverlay = [&](std::string_view overlayName) {
            SkillSpec::OverlayVisual visual;
            if (overlayName.empty()) return visual;
            size_t overlayRow = 0;
            for (; overlayRow < overlays.rows().size(); ++overlayRow)
                if (overlays.value(overlayRow, "overlay") == overlayName) break;
            if (overlayRow == overlays.rows().size())
                throw std::runtime_error("Missing original skill overlay: " + std::string(overlayName));
            visual.id = int(overlayRow);
            visual.frames = required(overlays, overlayRow, "Frames");
            visual.fps = float(required(overlays, overlayRow, "AnimRate"));
            visual.trans = required(overlays, overlayRow, "Trans");
            visual.preDraw = overlays.number(overlayRow, "PreDraw").value_or(0) != 0;
            visual.offset = {-float(required(overlays, overlayRow, "Xoffset")),
                              float(required(overlays, overlayRow, "Yoffset"))};
            for (int height = 0; height < 4; ++height)
                visual.heights[height] = required(overlays, overlayRow, "Height" + std::to_string(height + 1));
            visual.art = "data/global/overlays/" + lower(overlays.value(overlayRow, "Filename")) + ".dcc";
            if (visual.frames <= 0 || visual.fps <= 0 || !archives.contains(visual.art))
                throw std::runtime_error("Missing original skill overlay art: " + visual.art);
            return visual;
        };
        spec.castOverlay = loadOverlay(skills.value(row, "castoverlay"));
        if (effect == SkillBehavior::FrozenArmor || effect == SkillBehavior::ShiverArmor ||
            effect == SkillBehavior::ChillingArmor) {
            const bool shiver = effect == SkillBehavior::ShiverArmor;
            const bool frozen = effect == SkillBehavior::FrozenArmor;
            if (skills.value(row, "aurastat1") != "skill_armor_percent" ||
                skills.value(row, "aurastatcalc1") != "ln12" ||
                skills.value(row, "auraevent1") != (frozen ? "damagedinmelee" : shiver ? "attackedinmelee" : "hitbymissile") ||
                required(skills, row, "auraeventfunc1") != (frozen ? 2 : shiver ? 3 : 1) ||
                skills.value(row, "auralencalc") != (shiver ?
                    "ln34+(skill('Frozen Armor'.blvl)+skill('Chilling Armor'.blvl))*par7" :
                    frozen ? "ln34+(skill('Shiver Armor'.blvl)+skill('Chilling Armor'.blvl))*par7" :
                    "ln34+(skill('Frozen Armor'.blvl)+skill('Shiver Armor'.blvl))*par7") ||
                (frozen && skills.value(row, "calc1") != "ln56*(100+((skill('Shiver Armor'.blvl)+skill('Chilling Armor'.blvl))*par8))/100"))
                throw std::runtime_error("Unsupported original ice armor formula");
            for (int parameter = 0; parameter < 8; ++parameter)
                spec.armorParameters[size_t(parameter)] = !frozen && (parameter == 4 || parameter == 5) ? 0 :
                    required(skills, row, "Param" + std::to_string(parameter + 1));
            for (auto name : {frozen ? "Shiver Armor" : "Frozen Armor", frozen || shiver ? "Chilling Armor" : "Shiver Armor"}) {
                auto found = std::find_if(catalog.skills.begin(), catalog.skills.end(),
                    [&](const auto &entry) { return entry.second.sourceName == name && entry.second.classCode == "sor"; });
                if (found == catalog.skills.end()) throw std::runtime_error("Missing ice armor synergy");
                spec.armorSynergySkills.push_back(found->first);
            }
            const auto state = states.find(skills.value(row, "aurastate"));
            if (state == states.end()) throw std::runtime_error("Missing ice armor state");
            spec.state = state->second.definition;
            spec.stateOverlay = loadOverlay(state->second.overlay);
            if (spec.stateOverlay.id < 0 || spec.state.group != 1)
                throw std::runtime_error("Missing ice armor overlay or original exclusive group");
            spec.hitOverlay = loadOverlay(skills.value(row, "cltoverlaya"));
            for (size_t sound = 0; sound < sounds.rows().size(); ++sound)
                if (sounds.value(sound, "Sound") == skills.value(row, "dosound")) {
                    spec.activationSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
                    break;
                }
            if (spec.activationSoundArt.empty() || !archives.contains(spec.activationSoundArt))
                throw std::runtime_error("Missing ice armor activation sound");
            if (!frozen) {
                if (required(skills, row, "srvdofunc") != 18 ||
                    skills.value(row, "EDmgSymPerCalc") != (shiver ?
                        "(skill('Frozen Armor'.blvl)+skill('Chilling Armor'.blvl))*par8" :
                        "(skill('Frozen Armor'.blvl)+skill('Shiver Armor'.blvl))*par8") ||
                    !skills.value(row, "srvmissile").empty() || !spec.coldDamage)
                    throw std::runtime_error("Unsupported ice armor retaliation");
                spec.minimumDamage = required(skills, row, "EMin");
                spec.maximumDamage = required(skills, row, "EMax");
                for (int tier = 0; tier < 5; ++tier) {
                    spec.minimumPerLevel[tier] = required(skills, row, "EMinLev" + std::to_string(tier + 1));
                    spec.maximumPerLevel[tier] = required(skills, row, "EMaxLev" + std::to_string(tier + 1));
                }
                spec.synergySkills = spec.armorSynergySkills;
                spec.synergyPercent = spec.armorParameters[7];
                spec.coldFrames = required(skills, row, "ELen");
                for (int tier = 0; tier < 3; ++tier)
                    spec.coldFramesPerLevel[tier] = skills.number(row, "ELevLen" + std::to_string(tier + 1)).value_or(0);
            }
            if (shiver) {
                // States.cltactivefunc=87's sparkle placement/initialization is
                // not present in the local client reference. Keep its real art
                // available without running this client-only template as damage.
                if (skills.value(row, "cltmissilea") != "sparkle" ||
                    skills.value(row, "cltcalc1") != "10" || skills.value(row, "cltcalc2") != "5" ||
                    skills.value(row, "cltcalc3") != "3")
                    throw std::runtime_error("Unsupported Shiver Armor client particle link");
                bool particleFound = false;
                for (size_t child = 0; child < missiles.rows().size(); ++child)
                    if (missiles.value(child, "Missile") == skills.value(row, "cltmissilea")) {
                        spec.submissileResources.push_back(loadProjectileResource(missiles, child, archives));
                        particleFound = true;
                        break;
                    }
                if (!particleFound) throw std::runtime_error("Missing Shiver Armor original sparkle");
            }
            if (effect == SkillBehavior::ChillingArmor) {
                const auto missileName = skills.value(row, "srvmissilea");
                if (missileName != "chillingarmorbolt" || skills.value(row, "cltmissilea") != missileName)
                    throw std::runtime_error("Unsupported Chilling Armor missile link");
                size_t bolt = 0;
                for (; bolt < missiles.rows().size(); ++bolt)
                    if (missiles.value(bolt, "Missile") == missileName) break;
                if (bolt == missiles.rows().size()) throw std::runtime_error("Missing Chilling Armor missile");
                const auto expect = [&](std::string_view field, int value) {
                    if (missiles.number(bolt, field).value_or(0) != value)
                        throw std::runtime_error("Unsupported Chilling Armor bolt: " + std::string(field));
                };
                expect("pSrvDoFunc", 1); expect("pCltDoFunc", 1);
                expect("CollideType", 3); expect("CollideKill", 1); expect("LastCollide", 1);
                expect("Size", 1); expect("ReturnFire", 0); expect("ResultFlags", 4);
                expect("NumDirections", 16); expect("LoopAnim", 1); expect("Trans", 1);
                for (const auto field : {"pSrvHitFunc", "pSrvDmgFunc", "pCltHitFunc", "VelLev", "LevRange", "Accel",
                                         "AlwaysExplode", "NextHit", "NextDelay", "ToHit", "SrcDamage", "SrcMissDmg",
                                         "MissileSkill", "MinDamage", "MaxDamage", "EMin", "EMax", "ApplyMastery"})
                    expect(field, 0);
                if (missiles.value(bolt, "Skill") != name || !missiles.value(bolt, "EType").empty() ||
                    !missiles.value(bolt, "ExplosionMissile").empty() ||
                    required(missiles, bolt, "Vel") != required(missiles, bolt, "MaxVel"))
                    throw std::runtime_error("Unsupported Chilling Armor bolt damage or motion");
                const auto resource = loadProjectileResource(missiles, bolt, archives);
                spec.missileId = resource.id; spec.missileArt = resource.art;
                spec.missileLifetime = resource.lifetime;
                spec.missileVelocity = float(required(missiles, bolt, "Vel"));
                spec.missileMaxVelocity = required(missiles, bolt, "MaxVel");
                for (const auto &[field, output] : {std::pair{"TravelSound", &spec.releaseSoundArt},
                                                  std::pair{"HitSound", &spec.impactSoundArt}}) {
                    const auto soundName = missiles.value(bolt, field);
                    for (size_t sound = 0; sound < sounds.rows().size(); ++sound)
                        if (!soundName.empty() && sounds.value(sound, "Sound") == soundName) {
                            *output = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
                            break;
                        }
                    if (output->empty() || !archives.contains(*output))
                        throw std::runtime_error("Missing Chilling Armor original bolt sound");
                }
            }
        } else if (effect == SkillBehavior::StaticField) {
            if (skills.value(row, "calc1") != "par4" || skills.value(row, "calc2") != "par3" ||
                skills.value(row, "aurarangecalc") != "ln12")
                throw std::runtime_error("Unsupported original Static Field formula");
            spec.staticPercent = required(skills, row, "Param4");
            spec.staticMinDamage = required(skills, row, "Param3");
            spec.staticRange = required(skills, row, "Param1");
            spec.staticRangePerLevel = required(skills, row, "Param2");
        } else if (effect != SkillBehavior::Teleport) {
            spec.minimumDamage = required(skills, row, "EMin");
            spec.maximumDamage = required(skills, row, "EMax");
            for (int index = 0; index < 5; ++index) {
                const auto level = std::to_string(index + 1);
                spec.minimumPerLevel[index] = required(skills, row, "EMinLev" + level);
                spec.maximumPerLevel[index] = required(skills, row, "EMaxLev" + level);
            }
            spec.coldFrames = skills.number(row, "ELen").value_or(0);
            if (spec.coldFrames > 0)
                for (int index = 0; index < 3; ++index)
                    spec.coldFramesPerLevel[index] = effect == SkillBehavior::Blizzard ?
                        skills.number(row, "ELevLen" + std::to_string(index + 1)).value_or(0) :
                        required(skills, row, "ELevLen" + std::to_string(index + 1));
            const auto lengthFormula = skills.value(row, "ELenSymPerCalc");
            if (!lengthFormula.empty()) {
                if (effect != SkillBehavior::IceBlast || lengthFormula != "(skill('Glacial Spike'.blvl))*par7")
                    throw std::runtime_error("Unsupported original cold length synergy");
                for (const auto &[id, entry] : catalog.skills)
                    if (entry.classCode == "sor" && entry.sourceName == "Glacial Spike")
                        spec.coldSynergySkill = id;
                if (spec.coldSynergySkill < 0) throw std::runtime_error("Missing cold length synergy skill");
                spec.coldSynergyPercent = required(skills, row, "Param7");
            }
            const auto formula = skills.value(row, "EDmgSymPerCalc");
            if (!formula.empty()) {
                const auto marker = formula.find("*par8");
                if (marker == std::string_view::npos || marker + 5 != formula.size())
                    throw std::runtime_error("Unsupported original damage synergy: " + std::string(name));
                spec.synergyPercent = required(skills, row, "Param8");
                size_t cursor = 0;
                while ((cursor = formula.find("skill('", cursor)) != std::string_view::npos) {
                    cursor += 7;
                    const auto end = formula.find("'.blvl)", cursor);
                    if (end == std::string_view::npos)
                        throw std::runtime_error("Unsupported original damage synergy target");
                    const auto target = formula.substr(cursor, end - cursor);
                    auto found = std::find_if(catalog.skills.begin(), catalog.skills.end(),
                        [&](const auto &pair) { return pair.second.classCode == "sor" &&
                            pair.second.sourceName == target; });
                    if (found == catalog.skills.end())
                        throw std::runtime_error("Missing original synergy skill: " + std::string(target));
                    spec.synergySkills.push_back(found->first);
                    cursor = end + 7;
                }
                if (spec.synergySkills.empty())
                    throw std::runtime_error("Original damage synergy has no supported target");
            }
            auto missileName = skills.value(row, "srvmissile");
            if (effect == SkillBehavior::ChargedBolt) {
                const auto countFormula = skills.value(row, "calc1");
                if ((countFormula != "min(24,ln12)" && countFormula != "\"min(24,ln12)\"") || !spec.lightningDamage)
                    throw std::runtime_error("Unsupported original Charged Bolt formula");
                spec.missileCount = required(skills, row, "Param1");
                spec.missileCountPerLevel = required(skills, row, "Param2");
                spec.missileCountLimit = 24;
            }
            if (effect == SkillBehavior::Inferno) {
                if (skills.value(row, "calc1") != "ln12/2" || required(skills, row, "seqnum") != 6 ||
                    required(skills, row, "seqinput") != 10 || !spec.fireDamage ||
                    skills.number(row, "usemanaondo").value_or(0) != 0)
                    throw std::runtime_error("Unsupported original Inferno sequence");
                spec.startMana = required(skills, row, "startmana");
                spec.flameFrames = required(skills, row, "Param1");
                spec.flameFramesPerLevel = required(skills, row, "Param2");
            }
            if (effect == SkillBehavior::FrostNova || effect == SkillBehavior::Nova || effect == SkillBehavior::ChargedBolt ||
                effect == SkillBehavior::Inferno || effect == SkillBehavior::Blizzard)
                missileName = skills.value(row, "srvmissilea");
            size_t missileRow = 0;
            for (; missileRow < missiles.rows().size(); ++missileRow)
                if (missiles.value(missileRow, "Missile") == missileName) break;
            if (missileName.empty() || missileRow == missiles.rows().size())
                throw std::runtime_error("Missing original sorceress missile: " + std::string(name));
            spec.missileId = required(missiles, missileRow, "Id");
            if (effect == SkillBehavior::Blizzard)
                loadBlizzardMissiles(spec, skills, row, missiles, missileRow, archives);
            if (effect == SkillBehavior::GlacialSpike)
                loadGlacialSpikeMissiles(spec, skills, row, missiles, missileRow, archives);
            if (effect == SkillBehavior::FrozenOrb) {
                if (skills.value(row, "EType") != "cold" ||
                    skills.value(row, "cltmissilea") != missileName ||
                    required(skills, row, "cltdofunc") != 29 ||
                    skills.number(row, "SrcDam").value_or(0) != 0 ||
                    skills.value(row, "anim") != "SC")
                    throw std::runtime_error("Unsupported original Frozen Orb skill rules");
                loadFrozenOrbMissiles(spec, missiles, missileRow, archives);
            }
            spec.hitOverlay = loadOverlay(missiles.value(missileRow, "ProgOverlay"));
            if (effect == SkillBehavior::IceBlast &&
                (skills.value(row, "EType") != "cold" ||
                 required(missiles, missileRow, "pSrvDmgFunc") != 4 ||
                 required(missiles, missileRow, "CollideKill") != 1))
                throw std::runtime_error("Unsupported original Ice Blast missile rules");
            spec.missileVelocity = effect == SkillBehavior::Blizzard ? 0.f : float(required(missiles, missileRow, "Vel"));
            spec.missileVelocityPerLevel = missiles.number(missileRow, "VelLev").value_or(0);
            spec.missileRangePerLevel = missiles.number(missileRow, "LevRange").value_or(0);
            spec.missileAcceleration = missiles.number(missileRow, "Accel").value_or(0);
            spec.missileMaxVelocity = missiles.number(missileRow, "MaxVel").value_or(0);
            spec.missileLifetime = float(required(missiles, missileRow, "Range")) / 25.f;
            if (effect == SkillBehavior::ChargedBolt)
                spec.missileLifetime = float(std::min(77, required(missiles, missileRow, "Range"))) / 25.f;
            if (effect == SkillBehavior::Fireball) {
                if (required(missiles, missileRow, "pSrvHitFunc") != 1)
                    throw std::runtime_error("Unsupported original Fire Ball hit function");
                std::vector<ProjectileResource> resources;
                spec.missileImpact = loadMissileImpact(missiles, missileRow, archives, resources);
                for (const auto &resource : resources)
                    spec.impacts.push_back({resource.id, resource.art, resource.lifetime});
            }
            if (effect == SkillBehavior::Nova &&
                (skills.value(row, "EType") != "ltng" ||
                 required(missiles, missileRow, "NextHit") != 1 ||
                 required(missiles, missileRow, "NextDelay") <= 0))
                throw std::runtime_error("Unsupported original Nova missile rules");
            if (effect == SkillBehavior::Nova || effect == SkillBehavior::FrostNova)
                spec.missileNextDelay = required(missiles, missileRow, "NextDelay");
            auto file = lower(missiles.value(missileRow, "CelFile"));
            if (effect != SkillBehavior::Blizzard)
                spec.missileArt = "data/global/missiles/" + file + ".dcc";
            if (effect != SkillBehavior::Blizzard && (file.empty() || !archives.contains(spec.missileArt)))
                throw std::runtime_error("Missing original sorceress missile art: " + std::string(name));
            const auto travelSound = missiles.value(missileRow, "TravelSound");
            for (size_t sound = 0; sound < sounds.rows().size(); ++sound)
                if (!travelSound.empty() && sounds.value(sound, "Sound") == travelSound) {
                    spec.releaseSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
                    break;
                }
            if (!travelSound.empty() && (spec.releaseSoundArt.empty() || !archives.contains(spec.releaseSoundArt)))
                throw std::runtime_error("Missing original missile release sound: " + std::string(travelSound));
            if (effect == SkillBehavior::FireBolt || effect == SkillBehavior::Fireball ||
                effect == SkillBehavior::IceBolt || effect == SkillBehavior::IceBlast ||
                effect == SkillBehavior::GlacialSpike) {
                const auto impactName = missiles.value(missileRow, "ExplosionMissile");
                for (size_t impactRow = 0; spec.impacts.empty() && impactRow < missiles.rows().size(); ++impactRow)
                    if (!impactName.empty() && missiles.value(impactRow, "Missile") == impactName) {
                        const auto resource = loadProjectileResource(missiles, impactRow, archives);
                        spec.impacts.push_back({resource.id, resource.art, resource.lifetime});
                        break;
                    }
                if (spec.impacts.empty())
                    throw std::runtime_error("Missing original missile impact: " + std::string(impactName));
                if (!spec.missileImpact) spec.missileImpact.emplace();
                spec.missileImpact->visualId = spec.impacts.front().missileId;
                spec.missileImpact->visualDuration = spec.impacts.front().duration;
                const auto hitSound = missiles.value(missileRow, "HitSound");
                for (size_t sound = 0; sound < sounds.rows().size(); ++sound)
                    if (!hitSound.empty() && sounds.value(sound, "Sound") == hitSound) {
                        spec.impactSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
                        break;
                    }
                if (spec.impactSoundArt.empty() || !archives.contains(spec.impactSoundArt))
                    throw std::runtime_error("Missing original missile impact sound: " + std::string(hitSound));
            }
        }
        catalog.skills.at(record->id).spell = std::move(spec);
    }
}
} // namespace d2x
