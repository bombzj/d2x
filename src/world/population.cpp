#include "gameplay/monsters/implementation.hpp"
#include "population.hpp"
#include "core/random.hpp"
// Population rules adapted with reference to D2MOO (MIT), commit 5596f5cb6c5251a0a07c6637d26458b06099d516.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Spatial placement and stream scheduling remain project adapters; see docs/gameplay/monsters/POPULATION.md.
#include <algorithm>
#include <ostream>
#include <optional>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
class Random {
    uint64_t state_;
  public:
    explicit Random(uint32_t seed) : state_(initialRandom(seed)) {}
    uint32_t below(uint32_t bound) { return limitedRandom(state_, bound); }
    uint64_t state() const { return state_; }
    int between(int first, int last) {
        if (first < 0 || last < first || last > 1024)
            throw std::runtime_error("Invalid MPQ monster group range");
        return first + int(below(uint32_t(last - first + 1)));
    }
};
class Planner {
    const MonsterCatalog &catalog_;
    const LevelRecord *level_;
    const PresetRecord &preset_;
    const Map &map_;
    PopulationSettings settings_;
    PopulationPlan result_;
    Random random_;
    std::vector<const MonsterRecord *> roster_;
    std::set<std::string> diagnostics_;
    std::set<std::string> fixedUniques_;
    std::vector<uint8_t> occupied_;
    uint32_t group_ = 0;
    const Map::RoomBounds *densityRoom_ = nullptr;
    static constexpr size_t maxActors = 8192;

    void diagnostic(std::string message) { diagnostics_.insert(std::move(message)); }
    bool valid(Vec p, bool protectArrival, MovementCollisionRule rule) const {
        if (!map_.grid.walkable(p, rule))
            return false;
        if (protectArrival) {
            // Protect original warp arrivals where available; WarpDist is squared.
            Vec delta = p - map_.spawn;
            int distance = level_ ? std::max(0, level_->population.warpDistanceSquared) : 0;
            if (map_.warpArrivals.empty() && delta.x * delta.x + delta.y * delta.y < distance)
                return false;
            for (auto arrival : map_.warpArrivals) {
                delta = p - arrival;
                if (delta.x * delta.x + delta.y * delta.y < distance)
                    return false;
            }
        }
        int x = int(p.x), y = int(p.y);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int xx = x + dx, yy = y + dy;
                if (xx >= 0 && yy >= 0 && xx < map_.grid.width && yy < map_.grid.height &&
                    occupied_[size_t(yy) * map_.grid.width + xx])
                    return false;
            }
        return true;
    }
    std::optional<Vec> near(Vec center, int radius, bool protectArrival, MovementCollisionRule rule) {
        if (valid(center, protectArrival, rule))
            return center;
        // Bounded local adjustment only: never move a fixed boss to a remote chamber.
        std::vector<Vec> candidates;
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x) {
                Vec p = center + Vec{float(x), float(y)};
                if (valid(p, protectArrival, rule) && map_.grid.segment(center, p, {}, {rule.mask, 1}))
                    candidates.push_back(p);
            }
        if (candidates.empty())
            return {};
        return candidates[random_.below(uint32_t(candidates.size()))];
    }
    std::optional<Vec> randomPosition(MovementCollisionRule rule) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            int x = densityRoom_ ? densityRoom_->x : 0, y = densityRoom_ ? densityRoom_->y : 0;
            int w = densityRoom_ ? densityRoom_->width : map_.grid.width;
            int h = densityRoom_ ? densityRoom_->height : map_.grid.height;
            Vec pos{x + float(random_.below(uint32_t(w))) + .5f, y + float(random_.below(uint32_t(h))) + .5f};
            if (valid(pos, true, rule))
                return pos;
        }
        ++result_.rejectedPlacements;
        return {};
    }
    const MonsterRecord *lookup(std::string_view code) {
        auto monster = catalog_.find(code);
        if (!monster)
            diagnostic("Unresolved MonStats identity: " + std::string(code));
        return monster;
    }
    std::optional<Vec> add(const MonsterRecord &monster, Vec pos, int radius, MonsterRank rank,
                           SpawnOrigin origin, const std::string &key, const std::string &unique = {}) {
        if (!monster.hostile()) {
            ++result_.ignoredFriendly;
            return {};
        }
        if (result_.spawns.size() >= maxActors) {
            diagnostic("Population safety limit reached (8192 actors); remaining placements skipped.");
            return {};
        }
        auto location = near(pos, radius, origin == SpawnOrigin::Density, monster.spawnRule());
        if (!location) {
            ++result_.rejectedPlacements;
            return {};
        }
        occupied_[size_t(int(location->y)) * map_.grid.width + int(location->x)] = 1;
        result_.spawns.push_back({{monster.id, unique, key, rank, origin, group_},
                                  monsterImplementation(monster.id).kind,
                                  *location});
        return location;
    }
    void party(const MonsterRecord &leader, Vec pos, SpawnOrigin origin, const std::string &key) {
        if (leader.minions[0].empty() || leader.partyMax <= 0)
            return;
        int count = random_.between(leader.partyMin, leader.partyMax);
        int classes = leader.minions[1].empty() ? 1 : 2;
        for (int i = 0; i < count; ++i)
            if (auto minion = lookup(leader.minions[i % classes]))
                // A normal monster's party uses normal monster creation. Flag 64
                // suppresses recursion; it does not grant an elite minion rank.
                if (add(*minion, pos, 4, MonsterRank::Normal, origin, key + ".party." + std::to_string(i)) &&
                    leader.ownsParty)
                    result_.spawns.back().identity.ownerSpawnKey = key;
    }
    const MonsterRecord *choose(bool elite) {
        const MonsterRecord *monster = nullptr;
        if (elite && settings_.difficulty == 0 && level_) {
            const auto &pool = level_->population.unique;
            if (!pool.empty())
                monster = lookup(pool[random_.below(uint32_t(pool.size()))]);
        } else {
            uint32_t total = 0;
            for (auto entry : roster_)
                total += uint32_t(std::max(0, entry->rarity));
            if (total) {
                uint32_t roll = random_.below(total);
                for (auto entry : roster_) {
                    auto weight = uint32_t(std::max(0, entry->rarity));
                    if (roll < weight) {
                        monster = entry;
                        break;
                    }
                    roll -= weight;
                }
            }
        }
        if (monster && monster->placeSpawn && !monster->spawn.empty() &&
            random_.below(100) > uint32_t(elite ? 0 : 20))
            monster = lookup(monster->spawn);
        return monster && monster->hostile() ? monster : nullptr;
    }
    void selectRoster() {
        if (!level_ || !level_->population.supported)
            return;
        const auto &p = level_->population;
        auto pool = settings_.difficulty == 0 ? p.normal : p.nightmareHell;
        int count = std::min({std::max(p.types, 0), 13, int(pool.size())});
        for (int i = 0; i < count; ++i) {
            size_t index = random_.below(uint32_t(pool.size()));
            if (i == 0 && p.rangedFirst)
                for (int attempt = 0; attempt < 20; ++attempt) {
                    auto candidate = lookup(pool[index]);
                    if (candidate && candidate->ranged)
                        break;
                    index = random_.below(uint32_t(pool.size()));
                }
            auto monster = lookup(pool[index]);
            pool.erase(pool.begin() + index);
            if (monster && monster->randomSpawn && monster->hostile()) {
                roster_.push_back(monster);
                result_.roster.push_back(monster->id);
            }
        }
    }
    void elite(const MonsterRecord &monster, Vec pos, SpawnOrigin origin, const std::string &key,
               const SuperUniqueRecord *unique = nullptr, bool forceChampion = false) {
        auto rank = unique ? MonsterRank::SuperUnique
                    : forceChampion || random_.below(100) < uint32_t(catalog_.championChance())
                        ? MonsterRank::Champion
                        : MonsterRank::Unique;
        auto leader = add(monster, pos, 4, rank, origin, key + ".leader", unique ? unique->id : "");
        if (!leader)
            return;
        if (unique && unique->id == "The Countess")
            for (const auto &object : map_.terrain.data.objects)
                if (object.type == 1 && catalog_.preset(map_.terrain.data.act, object.id, map_.terrain.data.version, object.nativeIdentity).id == unique->id)
                    for (const auto &node : object.path)
                        result_.spawns.back().skillPositions.push_back({node.x + .5f, node.y + .5f});
        ++result_.eliteGroups;
        int low = 3, high = 6; // Original random unique minion group range.
        if (rank == MonsterRank::Champion) {
            low = 1;
            high = 3;
        }
        if (unique) {
            low = unique->minGroup;
            high = unique->maxGroup;
            if (low && high) {
                low += settings_.difficulty;
                high += settings_.difficulty;
            }
        }
        auto minion = rank == MonsterRank::Champion || monster.minions[0].empty()
                          ? &monster
                          : lookup(monster.minions[0]);
        int count = random_.between(low, high);
        for (int i = 0; minion && i < count; ++i)
            if (add(*minion, *leader, 4, rank == MonsterRank::Champion ? rank : MonsterRank::Minion, origin,
                    key + ".minion." + std::to_string(i))) {
                if (rank == MonsterRank::Champion) result_.spawns.back().identity.championVariantAllowed = false;
                else result_.spawns.back().identity.ownerSpawnKey = key + ".leader";
            }
    }
    const MonsterRecord *classVariant(const std::string &base) {
        if (!level_)
            return nullptr;
        auto result = lookup(base);
        // D2Common_11063 uses the NORMAL level roster, including in N/H. If the
        // family is absent, advance NextInClass up to normal area level + 1.
        for (const auto &code : level_->population.normal) {
            auto candidate = lookup(code);
            if (candidate && candidate->base == base) {
                result = candidate;
                break;
            }
        }
        if (result && result->id == base && !level_->population.normal.empty()) {
            bool inPool = std::find(level_->population.normal.begin(), level_->population.normal.end(),
                                    base) != level_->population.normal.end();
            std::set<std::string> visited;
            while (!inPool && !result->next.empty() && visited.insert(result->id).second) {
                auto next = lookup(result->next);
                if (!next || next->normalLevel > level_->population.level[0] + 1)
                    break;
                result = next;
            }
        }
        // Original Act I preset overrides: Black Marsh, Tamoe Highland, Pit 1/2.
        if (base == "fallen1") {
            if (level_->id == 6)
                result = lookup("fallen2");
            if (level_->id == 7 || level_->id == 12 || level_->id == 16)
                result = lookup("fallen3");
        } else if (base == "fallenshaman1") {
            if (level_->id == 6 || level_->id == 7)
                result = lookup("fallenshaman2");
            if (level_->id == 12 || level_->id == 16)
                result = lookup("fallenshaman3");
        }
        return result;
    }
    void fixed() {
        for (size_t i = 0; i < map_.terrain.data.objects.size(); ++i) {
            const auto &o = map_.terrain.data.objects[i];
            if (o.type != 1)
                continue;
            if (o.flags & 1u) {
                diagnostic("DS1 presets with already-spawned flag skipped.");
                continue;
            }
            auto unit = catalog_.preset(map_.terrain.data.act, o.id, map_.terrain.data.version, o.nativeIdentity);
            std::string key = "ds1." + std::to_string(i);
            Vec pos{o.x + .5f, o.y + .5f};
            ++group_;
            const MonsterRecord *monster = nullptr;
            if (unit.kind == MonsterPresetKind::SuperUnique) {
                auto unique = catalog_.superUnique(unit.id);
                if (!unique->stacks && fixedUniques_.contains(unique->id)) continue;
                const auto before = result_.spawns.size();
                elite(*catalog_.find(unique->monster), pos, SpawnOrigin::Preset, key, unique);
                if (result_.spawns.size() != before) fixedUniques_.insert(unique->id);
                continue;
            }
            if (unit.kind == MonsterPresetKind::Monster)
                monster = lookup(unit.id);
            else if (unit.kind == MonsterPresetKind::Place) {
                if (unit.id == "place_nothing" || unit.id == "place_npc_pack")
                    continue;
                if (unit.id == "place_unique_pack" || unit.id == "place_champion") {
                    if (auto selected = choose(true))
                        elite(*selected, pos, SpawnOrigin::Preset, key, nullptr, unit.id == "place_champion");
                    else
                        diagnostic("Deferred " + unit.id + ": no parent level monster pool.");
                    continue;
                }
                if (unit.id == "place_bloodraven")
                    monster = lookup("bloodraven");
                // These markers need native level class-chain selection, not DS1 filenames.
                else if (level_ && (unit.id == "place_fallen" || unit.id == "place_fallenshaman")) {
                    auto base = unit.id == "place_fallen" ? "fallen1" : "fallenshaman1";
                    monster = classVariant(base);
                    if (!monster)
                        diagnostic("Deferred " + unit.id + ": native class-chain rule required.");
                } else
                    diagnostic("Deferred MonPlace rule: " + unit.id);
            } else
                diagnostic("Unknown DS1 monster preset " + std::to_string(o.id));
            if (!monster)
                continue;
            auto point = add(*monster, pos, 4, monster->boss ? MonsterRank::Boss : MonsterRank::Normal,
                             SpawnOrigin::Preset, key);
            if (point)
                party(*monster, *point, SpawnOrigin::Preset, key);
        }
    }
    void density() {
        if (!level_ || !level_->population.supported || !preset_.populate)
            return;
        const auto &p = level_->population;
        int density = std::clamp(p.density[settings_.difficulty], 0, 10000);
        if (density == 0 || roster_.empty())
            return;
        // Generated maps use actual room footprints, excluding empty gaps and DS1 border rows.
        auto rooms = map_.rooms;
        if (rooms.empty())
            rooms.push_back({0, 0, (map_.terrain.data.width - 1) * 5, (map_.terrain.data.height - 1) * 5, true});
        for (const auto &room : rooms) {
            if (!room.populate)
                continue;
            densityRoom_ = &room;
            int trials = (room.width / 3) * room.height / 3;
            for (int trial = 0; trial < trials; ++trial) {
                ++result_.densityTrials;
                if (random_.below(100000) > uint32_t(density))
                    continue;
                auto monster = choose(false);
                if (!monster)
                    continue;
                bool boss =
                    result_.eliteGroups < p.uniqueMin[settings_.difficulty] ||
                    (result_.eliteGroups < p.uniqueMax[settings_.difficulty] && random_.below(100) <= 5);
                auto key = "density." + std::to_string(result_.densityTrials - 1);
                ++group_;
                if (boss) {
                    auto selected = choose(true);
                    auto pos = selected ? randomPosition(selected->spawnRule()) : std::nullopt;
                    if (selected && pos)
                        elite(*selected, *pos, SpawnOrigin::Density, key);
                    continue;
                }
                if (monster->sparse && random_.below(100) > uint32_t(monster->sparse))
                    continue;
                int count = monster->base == "fallen1" || monster->base == "scarab1"
                                ? 1
                                : random_.between(monster->minGroup, monster->maxGroup);
                if (!count)
                    continue;
                auto pos = randomPosition(monster->spawnRule());
                if (!pos)
                    continue;
                for (int member = 0; member < count; ++member) {
                    auto memberKey = key + "." + std::to_string(member);
                    auto point = add(*monster, *pos, 3, MonsterRank::Normal, SpawnOrigin::Density, memberKey);
                    if (point)
                        party(*monster, *point, SpawnOrigin::Density, memberKey);
                }
            }
        }
        densityRoom_ = nullptr;
    }

  public:
    Planner(const MonsterCatalog &catalog, const LevelRecord *level, const PresetRecord &preset,
            const Map &map, PopulationSettings settings, uint32_t seed)
        : catalog_(catalog), level_(level), preset_(preset), map_(map), settings_(settings), random_(seed),
          occupied_(map.grid.blocked.size()) {
        result_.sceneSeed = seed;
    }
    PopulationPlan run() {
        if (!catalog_.supported()) {
            result_.diagnostics = catalog_.diagnostics();
            return result_;
        }
        for (const auto &message : catalog_.diagnostics())
            diagnostic(message);
        if (level_ && level_->id == 1)
            return result_; // Town is never a hostile population.
        selectRoster();
        result_.componentRandom = random_.state();
        fixed();
        density();
        if (!level_)
            diagnostic("Template preview: no native level roster/density; fixed presets only.");
        if (level_ && !level_->population.supported)
            diagnostic("Unsupported Levels population schema.");
        result_.diagnostics.assign(diagnostics_.begin(), diagnostics_.end());
        return std::move(result_);
    }
};
} // namespace
PopulationPlan planPopulation(const MonsterCatalog &catalog, const LevelRecord *level,
                              const PresetRecord &preset, const Map &map, PopulationSettings settings) {
    if (settings.difficulty < 0 || settings.difficulty > 2)
        throw std::runtime_error("Population difficulty must be 0..2");
    auto parent = initialRandom(settings.seed);
    const uint32_t seed = rollRandom(parent) + uint32_t(level ? level->id : preset.id);
    return Planner(catalog, level, preset, map, settings, seed).run();
}
void writePopulationReport(std::ostream &out, const PopulationPlan &plan, const LevelRecord *level,
                           const PresetRecord &preset, PopulationSettings settings) {
    out << "Population: " << (level ? level->name : preset.name) << " | difficulty=" << settings.difficulty
        << " seed=" << settings.seed << " sceneSeed=" << plan.sceneSeed << " Populate=" << preset.populate
        << '\n';
    if (level) {
        const auto &p = level->population;
        out << "  NumMon=" << p.types << " MonDen=" << p.density[settings.difficulty]
            << " MonUMin/Max=" << p.uniqueMin[settings.difficulty] << '/' << p.uniqueMax[settings.difficulty]
            << " WarpDist(squared)=" << p.warpDistanceSquared << " areaLevel=" << p.level[settings.difficulty]
            << '\n';
        for (int i = 0; i < 4; ++i)
            if (!p.critters[i].empty())
                out << "  Ambient rule (deferred, never an enemy): " << p.critters[i]
                    << " cpct=" << p.critterChance[i] << " camt=" << p.critterAmount[i] << '\n';
    }
    out << "  Selected roster:";
    for (const auto &code : plan.roster)
        out << ' ' << code;
    out << "\n  PlannedActors=" << plan.spawns.size() << " eliteGroups=" << plan.eliteGroups
        << " trials=" << plan.densityTrials << " rejected=" << plan.rejectedPlacements
        << " friendlyIgnored=" << plan.ignoredFriendly << '\n';
    std::map<std::string, int> counts;
    for (const auto &spawn : plan.spawns) {
        auto label = spawn.identity.monster + " [" + monsterRankName(spawn.identity.rank) + "]";
        if (!spawn.identity.superUnique.empty())
            label += " " + spawn.identity.superUnique;
        label += spawn.identity.origin == SpawnOrigin::Preset ? " (DS1)" : " (density)";
        if (monsterImplementation(spawn.identity.monster).substitute)
            label += " -> Fallen substitute";
        ++counts[label];
    }
    for (const auto &[name, count] : counts)
        out << "  " << count << " x " << name << '\n';
    for (const auto &message : plan.diagnostics)
        out << "  " << message << '\n';
    out << "  Plan only; runtime creates nearby room groups. Original population seed streams and elite "
           "modifiers pending.\n";
}
} // namespace d2x
