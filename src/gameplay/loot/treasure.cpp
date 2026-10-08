#include "core/random.hpp"
#include "loot.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

namespace d2x {
int adjustedNoDrop(int noDrop, int itemWeight, unsigned effectivePlayers) {
    if(noDrop<=0 || itemWeight<=0 || effectivePlayers<=1) return std::max(0,noDrop);
    // D2Game ITEMS_DropItemByMonster: repeated multiplication, integer truncation.
    const double base=double(noDrop)/(double(itemWeight)+noDrop); double ratio=base;
    for(unsigned i=1;i<std::min(effectivePlayers,8u);++i) ratio*=base;
    const double inverse=1.0-ratio;
    return inverse==0.0?0:int(double(itemWeight)/inverse*(1.0-inverse));
}
TreasureRoll selectTreasure(std::span<const TreasureClass> classes, std::string_view root, uint64_t seed,
                           int level, const TreasureVisitor &visitor, unsigned effectivePlayers) {
    std::map<std::string_view, const TreasureClass *, std::less<>> lookup;
    for (const auto &record : classes)
        if (!lookup.emplace(record.name, &record).second)
            throw std::runtime_error("Duplicate treasure class: " + record.name);
    auto entry = lookup.find(root);
    if (entry == lookup.end())
        throw std::runtime_error("Unknown treasure class: " + std::string(root));
    const auto *selectedRoot = entry->second;
    if (level > 0 && selectedRoot->group.value_or(0))
        while (selectedRoot + 1 != classes.data() + classes.size()) {
            const auto *next = selectedRoot + 1;
            if (next->group != selectedRoot->group || next->level.value_or(0) > level)
                break;
            selectedRoot = next;
        }
    TreasureRoll result;
    result.root = selectedRoot->name;
    result.randomState = seed;
    std::vector<std::string> path;
    unsigned work = 0;
    bool stopped = false;
    std::function<void(const TreasureClass &, std::array<int, 4>)> visit;
    visit = [&](const TreasureClass &record, std::array<int, 4> quality) {
        if (path.size() >= 64 || std::find(path.begin(), path.end(), record.name) != path.end())
            throw std::runtime_error("Cyclic or excessive treasure nesting: " + record.name);
        if (!record.picks || record.codes.size() != record.weights.size() ||
            *record.picks == std::numeric_limits<int>::min() || record.noDrop.value_or(0) < 0)
            throw std::runtime_error("Unsupported treasure selection record: " + record.name);
        int64_t total = 0;
        for (int weight : record.weights) {
            if (weight < 0)
                throw std::runtime_error("Negative treasure weight: " + record.name);
            total += weight;
        }
        if(total>std::numeric_limits<int>::max()) throw std::runtime_error("Treasure weight overflow: "+record.name);
        const int noDrop=adjustedNoDrop(record.noDrop.value_or(0),int(total),effectivePlayers);
        const int64_t bound = total + noDrop;
        if (bound > std::numeric_limits<int>::max())
            throw std::runtime_error("Treasure weight overflow: " + record.name);
        if (!total || *record.picks == 0)
            return;
        for (size_t index = 0; index < quality.size(); ++index)
            quality[index] = std::max(quality[index], record.quality[index].value_or(0));
        path.push_back(record.name);
        const int picks = std::abs(*record.picks);
        for (int pick = 0; pick < picks && !stopped; ++pick) {
            if (++work > 10000 || result.selections.size() >= 4096)
                throw std::runtime_error("Treasure selection exceeds supported work budget");
            int64_t selected = pick;
            if (*record.picks < 0) {
                if (selected >= total)
                    break;
            } else {
                rollRandom(result.randomState);
                selected = uint32_t(result.randomState) % uint32_t(bound);
                selected -= noDrop;
                if (selected < 0) {
                    ++result.noDrops;
                    continue;
                }
            }
            for (size_t index = 0; index < record.codes.size(); ++index) {
                if (selected >= record.weights[index]) {
                    selected -= record.weights[index];
                    continue;
                }
                const auto &code = record.codes[index];
                auto child = lookup.find(code);
                if (child != lookup.end())
                    visit(*child->second, quality);
                else {
                    result.selections.push_back({code, quality, path});
                    if (visitor)
                        stopped = !visitor(result.selections.back(), result.randomState);
                }
                break;
            }
        }
        path.pop_back();
    };
    visit(*selectedRoot, {});
    return result;
}
} // namespace d2x
