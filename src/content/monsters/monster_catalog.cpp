#include "resources/archive.hpp"
#include "monster_catalog.hpp"
#include "monster_combat.hpp"
#include "monster_ai_data.hpp"
#include "monster_spell_data.hpp"
#include "monster_animation.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace d2x {
int monsterMovementPercent(const MonsterRecord &record, int difficulty, int percentage, bool chilled) {
    // Cold is another velocity stat, not a multiplier on the AI's boosted speed.
    return std::max(25, percentage + (chilled ? record.coldEffect.at(size_t(difficulty)) : 0));
}
MonsterCatalog::MonsterCatalog(Archives &archives, const DataTable &stats) {
    if (!stats.has("Id") || !stats.has("hcIdx") || !stats.has("MonStatsEx")) {
        diagnostics_.push_back("Population disabled: this MonStats schema needs a version-specific adapter.");
        return;
    }
    for (auto file : {"monstats2", "monpreset", "superuniques", "monplace", "monumod"}) {
        auto path = std::string("data/global/excel/") + file + ".txt";
        if (!archives.contains(path))
            diagnostics_.push_back("Population disabled: missing " + path +
                                   "; rebuild the compact MPQ from mpq2.");
    }
    if (!diagnostics_.empty())
        return;
    DataTable extended(archives.read("data/global/excel/monstats2.txt"));
    DataTable missiles(archives.read("data/global/excel/missiles.txt"));
    DataTable skills(archives.read("data/global/excel/skills.txt"));
    DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (missiles.value(row, "Missile") == "towerchestspawner") {
            towerReward_ = TowerReward{missiles.number(row, "Range").value_or(0),
                missiles.number(row, "Param1").value_or(0),
                std::max(1, 4 * missiles.number(row, "Param2").value_or(0)),
                missiles.number(row, "Param3").value_or(0)};
            if (towerReward_->lifetimeFrames <= towerReward_->openingFrame ||
                towerReward_->openingFrame < 0 || towerReward_->radius < 0 || towerReward_->radius > 100)
                throw std::runtime_error("Invalid tower chest reward data");
        }
    for (size_t skillRow = 0; skillRow < skills.rows().size(); ++skillRow)
        if (skills.value(skillRow, "skill") == "CountessFirewall" && skills.number(skillRow, "srvdofunc") == 24) {
            std::optional<size_t> maker, fire;
            for (size_t missileRow = 0; missileRow < missiles.rows().size(); ++missileRow) {
                if (missiles.value(missileRow, "Missile") == skills.value(skillRow, "srvmissilea")) maker = missileRow;
                if (missiles.value(missileRow, "Missile") == skills.value(skillRow, "srvmissileb")) fire = missileRow;
            }
            if (!maker || !fire || missiles.number(*maker, "pSrvDoFunc") != 6 ||
                missiles.number(*fire, "pSrvDoFunc") != 5) throw std::runtime_error("Unsupported Countess firewall data");
            auto number = [&](size_t row, const char *field) { return missiles.number(row, field).value_or(0); };
            countessFirewall_ = MonsterFirewall{number(*maker, "Id"), number(*fire, "Id"),
                number(*maker, "Range"), number(*fire, "Range"), float(number(*maker, "Vel")),
                number(*fire, "EMin"), number(*fire, "EMax"), number(*fire, "HitShift"), number(*fire, "Size")};
            if (countessFirewall_->makerFrames <= 0 || countessFirewall_->fireFrames <= 0 ||
                countessFirewall_->velocity <= 0 || countessFirewall_->maximumDamage < countessFirewall_->minimumDamage ||
                countessFirewall_->hitShift < 0 || countessFirewall_->hitShift > 8)
                throw std::runtime_error("Invalid Countess firewall parameters");
        }
    std::optional<DataTable> levels;
    if (archives.contains("data/global/excel/monlvl.txt"))
        levels.emplace(archives.read("data/global/excel/monlvl.txt"));
    std::map<std::string, size_t, std::less<>> extendedRows;
    for (size_t row = 0; row < extended.rows().size(); ++row)
        extendedRows.emplace(extended.value(row, "Id"), row);
    for (size_t row = 0; row < stats.rows().size(); ++row) {
        if (stats.value(row, "hcIdx").empty())
            continue;
        auto n = [&](std::string_view field) { return stats.number(row, field).value_or(0); };
        MonsterRecord m;
        m.id = stats.value(row, "Id");
        m.sourceRow = row;
        m.index = n("hcIdx");
        m.base = stats.value(row, "BaseId");
        m.next = stats.value(row, "NextInClass");
        m.name = stats.value(row, "NameStr");
        m.token = stats.value(row, "Code");
        m.ai = stats.value(row, "AI");
        m.sound = stats.value(row, "MonSound");
        m.spawn = stats.value(row, "spawn");
        m.minions = {std::string(stats.value(row, "minion1")), std::string(stats.value(row, "minion2"))};
        m.rarity = n("Rarity");
        m.minGroup = n("MinGrp");
        m.maxGroup = n("MaxGrp");
        m.partyMin = n("PartyMin");
        m.partyMax = n("PartyMax");
        m.sparse = n("sparsePopulate");
        m.normalLevel = n("Level");
        m.primeEvil = n("primeevil") != 0;
        m.transLevel = n("TransLvl");
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            const char *field = difficulty == 0 ? "ColdEffect" :
                                difficulty == 1 ? "ColdEffect(N)" : "ColdEffect(H)";
            m.coldEffect[difficulty] = stats.number(row, field).value_or(0);
        }
        m.normalCombat = loadMonsterNormalCombat(stats, row, levels ? &*levels : nullptr);
        for (int difficulty = 0; difficulty < 3; ++difficulty)
            m.aiProfiles[difficulty] = loadMonsterAiProfile(stats, row, m.ai, difficulty);
        m.walkVelocity = stats.number(row, "Velocity");
        m.runVelocity = stats.number(row, "Run");
        auto loadProjectile = [&](std::string_view missileName, std::string &artPath)
            -> std::optional<MonsterProjectile> {
            if (missileName.empty()) return std::nullopt;
            for (size_t missileRow = 0; missileRow < missiles.rows().size(); ++missileRow)
                if (missiles.value(missileRow, "Missile") == missileName) {
                    const auto id = missiles.number(missileRow, "Id");
                    const auto velocity = missiles.number(missileRow, "Vel");
                    const auto range = missiles.number(missileRow, "Range");
                    const auto minimum = missiles.number(missileRow, "MinDamage").value_or(0);
                    const auto maximum = missiles.number(missileRow, "MaxDamage").value_or(0);
                    const auto sourceDamage = missiles.number(missileRow, "SrcDamage").value_or(0);
                    auto file = std::string(missiles.value(missileRow, "CelFile"));
                    std::transform(file.begin(), file.end(), file.begin(),
                                   [](unsigned char ch) { return char(std::tolower(ch)); });
                    const auto art = "data/global/missiles/" + file + ".dcc";
                    if (id && velocity && *velocity > 0 && range && *range > 0 &&
                        minimum >= 0 && maximum >= minimum && maximum <= 1000000 &&
                        sourceDamage >= 0 && sourceDamage <= 255 &&
                        !file.empty() && archives.contains(art)) {
                        artPath = art;
                        return MonsterProjectile{*id, float(*velocity), float(*range) / 25.f,
                                                 minimum, maximum, sourceDamage};
                    }
                    return std::nullopt;
                }
            return std::nullopt;
        };
        m.attack1Projectile = loadProjectile(stats.value(row, "MissA1"), m.attack1ProjectileArt);
        // RogueMissile is the hireling's ordinary attack (SkillMonst SrvDo110),
        // referenced by MonStats.Skill1, not by the empty MissA1 column.
        if (m.ai == "Hireable" && !m.attack1Projectile)
            for (size_t skillRow = 0; skillRow < skills.rows().size(); ++skillRow)
                if (skills.value(skillRow, "skill") == stats.value(row, "Skill1") &&
                    skills.number(skillRow, "srvdofunc") == 110 && stats.value(row, "Sk1mode") == "A1") {
                    m.attack1Projectile = loadProjectile(skills.value(skillRow, "srvmissilea"), m.attack1ProjectileArt);
                    if (m.attack1Projectile)
                        m.attack1Projectile->velocity = float(int(m.attack1Projectile->velocity) * 256 * 75 / 100) * 25.f / 4096.f;
                    break;
                }
        m.attack2Projectile = loadProjectile(stats.value(row, "MissA2"), m.attack2ProjectileArt);
        if (m.walkVelocity && (*m.walkVelocity < 0 || *m.walkVelocity > 255))
            throw std::runtime_error("Unsupported monster Velocity: " + m.id);
        if (m.runVelocity && (*m.runVelocity < 0 || *m.runVelocity > 255))
            throw std::runtime_error("Unsupported monster Run: " + m.id);
        auto attackRating = [&](std::string_view field) -> std::optional<int> {
            auto attack = stats.number(row, field);
            if (!attack || *attack < 0 || m.normalLevel <= 0) return std::nullopt;
            if (n("noRatio"))
                return *attack;
            if (levels && !levels->rows().empty()) {
                const auto levelRow = std::min(size_t(m.normalLevel), levels->rows().size() - 1);
                if (auto base = levels->number(levelRow, "L-TH"); base && *base >= 0) {
                    const auto rating = int64_t(*base) * *attack / 100;
                    if (rating > std::numeric_limits<int>::max())
                        throw std::runtime_error("Monster attack rating overflow: " + m.id);
                    return int(rating);
                }
            }
            return std::nullopt;
        };
        m.normalAttackRating = attackRating("A1TH");
        m.normalAttackRating2 = attackRating("A2TH");
        if (auto armor = stats.number(row, "AC"); armor && *armor >= 0 && m.normalLevel > 0) {
            if (n("noRatio"))
                m.normalDefense = *armor;
            else if (levels && !levels->rows().empty()) {
                const auto levelRow = std::min(size_t(m.normalLevel), levels->rows().size() - 1);
                if (auto base = levels->number(levelRow, "L-AC"); base && *base >= 0) {
                    const auto defense = int64_t(*base) * *armor / 100;
                    if (defense > std::numeric_limits<int>::max())
                        throw std::runtime_error("Monster defense overflow: " + m.id);
                    m.normalDefense = int(defense);
                }
            }
        }
        m.alignment = n("Align");
        m.enabled = n("enabled") != 0;
        m.randomSpawn = n("isSpawn") != 0;
        m.ranged = n("rangedtype") != 0;
        m.placeSpawn = n("placespawn") != 0;
        m.killable = n("killable") != 0;
        m.npc = n("npc") != 0;
        m.interact = n("interact") != 0;
        m.boss = n("boss") != 0 || n("primeevil") != 0;
        m.ownsParty = n("setboss") != 0 || m.base == "tentaclehead1";
        m.demon = n("demon") != 0;
        m.flying = n("flying") != 0;
        m.undead = n("lUndead") != 0 || n("hUndead") != 0;
        auto extra = extendedRows.find(stats.value(row, "MonStatsEx"));
        if (extra == extendedRows.end())
            throw std::runtime_error("Missing MonStats2 record for " + m.id);
        m.critter = extended.number(extra->second, "critter").value_or(0) != 0;
        m.inert = extended.number(extra->second, "inert").value_or(0) != 0;
        m.localBlood = extended.number(extra->second, "localBlood").value_or(0);
        m.bleed = extended.number(extra->second, "Bleed").value_or(0);
        m.lightRadius = extended.number(extra->second, "Light").value_or(0);
        m.lightColor = {extended.number(extra->second, "light-r").value_or(0),
                        extended.number(extra->second, "light-g").value_or(0),
                        extended.number(extra->second, "light-b").value_or(0)};
        m.castsShadow = extended.number(extra->second, "Shadow").value_or(0) != 0;
        m.overlayHeight = extended.number(extra->second, "OverlayHeight").value_or(0);
        m.uniqueTrans = {extended.number(extra->second, "Utrans").value_or(-1),
            extended.number(extra->second, "Utrans(N)").value_or(-1),
            extended.number(extra->second, "Utrans(H)").value_or(-1)};
        m.collisionSize = extended.number(extra->second, "SizeX").value_or(0);
        m.spawnCollision = extended.number(extra->second, "spawnCol").value_or(0);
        m.hitClass = extended.number(extra->second, "HitClass").value_or(0);
        m.corpseSelectable = extended.number(extra->second, "corpseSel").value_or(0) != 0;
        m.resurrectionMode = normalize(std::string(extended.value(extra->second, "ResurrectMode")));
        m.getHitMode = extended.number(extra->second, "mGH").value_or(0) != 0;
        m.curseable = extended.number(extra->second, "mA1").value_or(0) != 0;
        m.switchAi = stats.number(row, "switchai").value_or(0) != 0 && extended.number(extra->second, "mWL").value_or(0) != 0;
        m.deadMode = extended.number(extra->second, "mDD").value_or(0) != 0;
        m.skill2Mode = extended.number(extra->second, "mS2").value_or(0) != 0;
        m.runMode = extended.number(extra->second, "mRN").value_or(0) != 0;
        m.castMode = extended.number(extra->second, "mSC").value_or(0) != 0;
        m.sequenceMode = extended.number(extra->second, "mSQ").value_or(0) != 0;
        m.baseWeapon = extended.value(extra->second, "BaseW");
        const int meleeRange = extended.number(extra->second, "MeleeRng").value_or(0);
        if (meleeRange < 0 || meleeRange > 255)
            throw std::runtime_error("Unsupported monster MeleeRng: " + m.id);
        for (auto &profile : m.aiProfiles)
            if (profile)
                profile->meleeRange = meleeRange == 255 ? (m.baseWeapon == "2ht" ? 2 : 0) : meleeRange;
        auto firstVariant = [&](std::string_view field) {
            auto variant = std::string(extended.value(extra->second, field));
            if (variant.starts_with('"')) variant.erase(0, 1);
            if (auto separator = variant.find(','); separator != std::string::npos)
                variant.resize(separator);
            if (variant.ends_with('"')) variant.pop_back();
            return variant;
        };
        auto shields = std::string(extended.value(extra->second, "SHv"));
        std::erase(shields, '"');
        for (size_t start = 0; start < shields.size();) {
            const auto end = shields.find(',', start);
            m.shieldVariants.push_back(shields.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        m.rightHandVariant = firstVariant("RHv");
        constexpr const char *componentFields[]{"HDv","TRv","LGv","RAv","LAv","RHv","LHv","SHv",
            "S1v","S2v","S3v","S4v","S5v","S6v","S7v","S8v"};
        for (size_t component = 0; component < m.components.size(); ++component)
            m.components[component] = firstVariant(componentFields[component]);
        m.leftHandVariant = firstVariant("LHv");
        for (int index = 0; index < 8; ++index)
            m.specialVariants[size_t(index)] = firstVariant("S" + std::to_string(index + 1) + "v");
        if (m.castMode || m.sequenceMode) {
            m.spells = loadMonsterSpells(archives, stats, row, skills, missiles, sequences);
            m.resurrection = loadMonsterResurrection(stats, row, skills, sequences);
            m.nest = loadMonsterNest(stats, row, skills, sequences);
            m.web = loadMonsterWeb(archives, stats, row, skills, missiles);
        }
        if (!indices_.emplace(m.index, m.id).second)
            throw std::runtime_error("Duplicate MonStats hcIdx: " + std::to_string(m.index));
        if (!monsters_.emplace(m.id, m).second) {
            // Some shipped tables repeat Act V names (e.g. cr_lancer8). Do not
            // guess which native row a string reference meant, or block Act I.
            ambiguous_.insert(m.id);
            diagnostics_.push_back("Ambiguous duplicate MonStats Id disabled: " + m.id);
        }
    }
    if (archives.contains("data/global/animdata.d2")) {
        AnimDataTable animations(archives.read("data/global/animdata.d2"));
        auto animationRate = [&](const MonsterRecord &actor, std::string_view mode) -> std::optional<int> {
            const auto weapon = monsterModeWeapon(archives, actor.token, mode, actor.baseWeapon);
            if (weapon.empty()) return std::nullopt;
            auto key = actor.token + std::string(mode) + weapon;
            for (char &ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            const auto *record = animations.find(key);
            return record && record->speed > 0 ? std::optional<int>{int(record->speed)} : std::nullopt;
        };
        // D2MOO MonsterTbls: variants scale base-ID rates by Velocity/Run;
        // pre-expansion RN uses half the base WL rate, not the RN AnimData rate.
        for (auto &[id, actor] : monsters_) {
            if (monsterImplementation(id).substitute && actor.ai != "Hireable") continue;
            const auto base = monsters_.find(actor.base);
            if (base == monsters_.end()) continue;
            const auto walk = animationRate(base->second, "wl");
            const auto run = actor.index < 410
                ? (walk ? std::optional<int>{*walk / 2} : std::nullopt)
                : animationRate(base->second, "rn");
            auto scaled = [&](std::optional<int> rate, std::optional<int> velocity,
                              std::optional<int> baseVelocity) -> std::optional<int> {
                if (!rate) return std::nullopt;
                if (actor.id != base->second.id && baseVelocity && *baseVelocity > 0) {
                    if (!velocity) return std::nullopt;
                    *rate = *rate * *velocity / *baseVelocity;
                }
                return std::clamp(*rate, 0, 32767);
            };
            actor.walkAnimationRate = scaled(walk, actor.walkVelocity, base->second.walkVelocity);
            actor.runAnimationRate = scaled(run, actor.runVelocity, base->second.runVelocity);
        }
        for (const auto &[id, actor] : monsters_)
            if (actor.ai == "Hireable") {
                auto weapon = monsterModeWeapon(archives, actor.token, "a1", actor.baseWeapon);
                if (!weapon.empty())
                    if (auto timing = loadMonsterAttackTiming(
                            animations, actor.token, 1, weapon,
                            actor.attack1Projectile ? 2 : 1))
                        hirelingAttacks_.emplace(actor.index, *timing);
                for (auto mode : {"nu", "wl", "gh", "dt", "dd"}) {
                    const auto modeWeapon = monsterModeWeapon(archives, actor.token, mode, actor.baseWeapon);
                    if (!modeWeapon.empty())
                        if (auto timing = loadMonsterMotionTiming(animations, actor.token, mode, modeWeapon))
                            hirelingMotions_[actor.index].emplace(mode, *timing);
                }
            }
        std::map<MonsterKind, const MonsterRecord *> actors;
        for (const auto &[id, record] : monsters_) {
            auto implementation = monsterImplementation(id);
            if (!implementation.substitute) actors.emplace(implementation.kind, &record);
        }
        for (const auto &[kind, actor] : actors) {
            for (auto mode : {"nu", "wl", "rn", "a1", "a2", "sc", "gh", "dt", "dd", "s1", "s2"}) {
                auto weapon = monsterModeWeapon(archives, actor->token, mode, actor->baseWeapon);
                if (weapon.empty()) continue;
                modeWeapons_[kind].emplace(mode, weapon);
                if (std::string_view(mode) == "a1") {
                    if (kind == MonsterKind::BloodRaven)
                        if (auto timing = loadMonsterSequenceTiming(animations, sequences,
                                stats.value(actor->sourceRow, "Sk2mode"), actor->token, mode, weapon, 2))
                            quickAttacks_.emplace(kind, *timing);
                    if (auto timing = loadMonsterAttackTiming(
                            animations, actor->token, 1, weapon,
                            actor->attack1Projectile || actor->ai == "Hydra" ? 2 : 1))
                        attacks_.emplace(kind, *timing);
                } else if (std::string_view(mode) == "a2") {
                    if (kind == MonsterKind::Brute || kind == MonsterKind::Skeleton ||
                        kind == MonsterKind::HellBovine ||
                        kind == MonsterKind::Zombie || kind == MonsterKind::Fallen ||
                        kind == MonsterKind::QuillRat || kind == MonsterKind::Bighead)
                        if (auto timing = loadMonsterAttackTiming(
                                animations, actor->token, 2, weapon,
                                actor->attack2Projectile ? 2 : 1))
                            attacks2_.emplace(kind, *timing);
                    if (kind == MonsterKind::FallenShaman && actor->sequenceMode &&
                        actor->spells[1] && actor->resurrection &&
                        actor->spells[1]->mode == "A2" &&
                        actor->resurrection->mode == "A2")
                        if (auto timing = loadMonsterActionTiming(
                                animations, actor->token, mode, weapon, 2))
                            casts_.emplace(kind, *timing);
                    if (kind == MonsterKind::Arach && actor->web &&
                        actor->web->mode == "A2")
                        if (auto timing = loadMonsterActionTiming(
                                animations, actor->token, mode, weapon, 2))
                            casts_.emplace(kind, *timing);
                } else if (std::string_view(mode) == "sc" &&
                           kind == MonsterKind::Vampire && actor->castMode) {
                    if (auto timing = loadMonsterActionTiming(
                            animations, actor->token, mode, weapon, 2))
                        casts_.emplace(kind, *timing);
                } else if (std::string_view(mode) == "sc" && kind == MonsterKind::Andariel && actor->spells[0]) {
                    if (auto timing = loadMonsterSequenceTiming(animations, sequences,
                            stats.value(actor->sourceRow, "Sk1mode"), actor->token, mode, weapon, 2))
                        casts_.emplace(kind, *timing);
                } else if (std::string_view(mode) == "s1" &&
                           (kind == MonsterKind::FoulCrowNest || kind == MonsterKind::BloodRaven) && actor->nest &&
                           actor->nest->mode == "S1") {
                    if (auto timing = loadMonsterSequenceTiming(
                            animations, sequences, actor->nest->sequence,
                            actor->token, mode, weapon, 4))
                        casts_.emplace(kind, *timing);
                } else if (std::string_view(mode) != "s2" || kind == MonsterKind::Fallen || actor->ai == "Hydra") {
                    if (auto timing = loadMonsterMotionTiming(animations, actor->token, mode, weapon))
                        motions_[kind].emplace(mode, *timing);
                }
            }
        }
    }
    DataTable uniques(archives.read("data/global/excel/superuniques.txt"));
    for (size_t row = 0; row < uniques.rows().size(); ++row) {
        if (uniques.value(row, "hcIdx").empty())
            continue;
        SuperUniqueRecord u;
        u.id = uniques.value(row, "Superunique");
        u.name = uniques.value(row, "Name");
        u.monster = uniques.value(row, "Class");
        u.index = uniques.number(row, "hcIdx").value();
        u.minGroup = uniques.number(row, "MinGrp").value_or(0);
        u.maxGroup = uniques.number(row, "MaxGrp").value_or(0);
        u.autoPosition = uniques.number(row, "AutoPos").value_or(0) != 0;
        u.stacks = uniques.number(row, "Stacks").value_or(0) != 0;
        u.uniqueTrans = {uniques.number(row, "Utrans").value_or(0),
            uniques.number(row, "Utrans(N)").value_or(0), uniques.number(row, "Utrans(H)").value_or(0)};
        for (int i = 0; i < 3; ++i)
            u.modifiers[i] = uniques.number(row, "Mod" + std::to_string(i + 1)).value_or(0);
        u.treasureClasses = {std::string(uniques.value(row, "TC")), std::string(uniques.value(row, "TC(N)")),
                             std::string(uniques.value(row, "TC(H)"))};
        if (!find(u.monster) || !uniques_.emplace(u.id, u).second)
            throw std::runtime_error("Invalid SuperUniques record: " + u.id);
    }
    DataTable places(archives.read("data/global/excel/monplace.txt"));
    for (size_t row = 0; row < places.rows().size(); ++row)
        places_.emplace(places.value(row, "code"));
    DataTable presets(archives.read("data/global/excel/monpreset.txt"));
    for (size_t row = 0; row < presets.rows().size(); ++row) {
        auto act = presets.number(row, "Act");
        if (!act)
            continue;
        if (*act < 1 || *act > 5)
            throw std::runtime_error("Invalid MonPreset Act");
        // Native DS1 IDs are zero-based within each act; duplicates are intentional.
        presets_[*act - 1].emplace_back(presets.value(row, "Place"));
    }
    DataTable modifiers(archives.read("data/global/excel/monumod.txt"));
    championChance_ = modifiers.number(0, "constants").value_or(0);
    if (championChance_ < 0 || championChance_ > 100)
        throw std::runtime_error("Invalid MonUMod champion chance");
    supported_ = true;
}
const MonsterRecord *MonsterCatalog::find(std::string_view id) const {
    if (ambiguous_.contains(id))
        return nullptr;
    auto it = monsters_.find(id);
    return it == monsters_.end() ? nullptr : &it->second;
}
const SuperUniqueRecord *MonsterCatalog::superUnique(std::string_view id) const {
    auto it = uniques_.find(id);
    return it == uniques_.end() ? nullptr : &it->second;
}
MonsterPreset MonsterCatalog::preset(int act, int index, int version) const {
    if (version <= 4) {
        auto it = indices_.find(index);
        return it == indices_.end() ? MonsterPreset{} : MonsterPreset{MonsterPresetKind::Monster, it->second};
    }
    if (act < 0 || act >= 5 || index < 0 || size_t(index) >= presets_[act].size())
        return {};
    const auto &id = presets_[act][index];
    // Same linker priority as the original table loader; case is significant here.
    if (superUnique(id))
        return {MonsterPresetKind::SuperUnique, id};
    if (find(id))
        return {MonsterPresetKind::Monster, id};
    if (places_.contains(id))
        return {MonsterPresetKind::Place, id};
    return {MonsterPresetKind::Unknown, id};
}
} // namespace d2x
