#include "vendor_stock.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
namespace d2x {
namespace {
unsigned below(uint64_t &seed,unsigned bound) { return limitedRandom(seed,bound); }
void sortGambleSelection(std::vector<const ItemDefinition *> &items) {
    const auto level=[&](ptrdiff_t index) {return *items[size_t(index)]->base.level;};
    const auto sort=[&](auto &&self,ptrdiff_t low,ptrdiff_t high)->void {
        if(low>=high) return;
        if(high-low+1<=8) {
            for(auto end=high;end>low;--end) {
                auto maximum=low;
                for(auto index=low+1;index<=end;++index) if(level(index)>level(maximum)) maximum=index;
                std::swap(items[size_t(maximum)],items[size_t(end)]);
            }
            return;
        }
        auto middle=low+(high-low+1)/2;
        if(level(low)>level(middle)) std::swap(items[size_t(low)],items[size_t(middle)]);
        if(level(low)>level(high)) std::swap(items[size_t(low)],items[size_t(high)]);
        if(level(middle)>level(high)) std::swap(items[size_t(middle)],items[size_t(high)]);
        auto left=low,right=high;
        for(;;) {
            if(middle>left) do {++left;} while(left<middle && level(left)<=level(middle));
            if(middle<=left) do {++left;} while(left<=high && level(left)<=level(middle));
            do {--right;} while(right>middle && level(right)>level(middle));
            if(left>right) break;
            std::swap(items[size_t(left)],items[size_t(right)]);
            if(middle==right) middle=left;
        }
        ++right;
        if(middle<right) do {--right;} while(right>middle && level(right)==level(middle));
        if(middle>=right) do {--right;} while(right>low && level(right)==level(middle));
        self(self,low,right);self(self,left,high);
    };
    if(!items.empty()) sort(sort,0,ptrdiff_t(items.size())-1);
}
}
bool npcCanGamble(std::string_view npc) { return npc=="gheed" || npc=="elzix" || npc=="alkor" || npc=="jamella" || npc=="nihlathak" || npc=="drehya"; }
void generateGambleStock(const ClassicData &data,unsigned playerLevel,
    int difficulty,uint64_t &seed,const GambleItemSource &create) {
    if (difficulty < 0 || difficulty > 2 || !playerLevel || playerLevel > 99 || !create)
        throw std::runtime_error("Invalid gamble parameters");
    const auto &pool = data.tables.at("gamble");
    const auto &chances = data.tables.at("difficultylevels");
    const auto chance = [&](const char *field) {
        auto value = chances.number(size_t(difficulty), field);
        if (!value || *value < 0) throw std::runtime_error("Missing original gambling chance");
        return unsigned(*value);
    };
    const unsigned uniqueChance = chance("GambleUnique"), setChance = chance("GambleSet"),
                   rareChance = chance("GambleRare");
    std::vector<const ItemDefinition *> selection;
    for(size_t row=0;row<pool.rows().size();++row) {
        const auto *item=data.items.find(pool.value(row,"code"));
        if(!item || !item->base.level) throw std::runtime_error("Missing original gamble item record");
        selection.push_back(item);
    }
    sortGambleSelection(selection);
    for (unsigned index = 0; index < 14; ++index) {
        const int level = std::clamp(int(playerLevel) + int(below(seed, 10)) - 5, 5, 99);
        const auto limit=std::find_if(selection.begin(),selection.end(),[&](const auto *item){return *item->base.level>level;});
        const auto count=unsigned(limit-selection.begin());
        if(!count) throw std::runtime_error("Empty original gamble pool");
        const auto *base = selection[below(seed,count)];
        if(index<2) base=data.items.find(index==0?"rin":"amu");
        if(!base) throw std::runtime_error("Missing original fixed gamble item");
        const auto *item = base;
        const auto &source = data.tables.at(base->base.sourceTable);
        if(const auto *exceptional=data.items.find(source.value(base->base.sourceRow,"ubercode"))) {
            const auto odds=nativeAdd(nativeMultiply(nativeNarrow(chance("GambleUber")),level-exceptional->base.level.value_or(100)),1);
            if(odds>0) {
                if(int(below(seed,10000))<odds) item=exceptional;
                else if(const auto *elite=data.items.find(source.value(base->base.sourceRow,"ultracode"))) {
                    const auto eliteOdds=nativeAdd(nativeMultiply(nativeNarrow(chance("GambleUltra")),level-elite->base.level.value_or(100)),1);
                    if(eliteOdds>0 && int(below(seed,10000))<eliteOdds) item=elite;
                }
            }
        }
        ItemQuality quality=ItemQuality::Magic;
        const auto total=nativeNarrow(uniqueChance+setChance+rareChance);
        if(total>0) {
            const auto roll=int(below(seed,100000));
            if(roll<total) quality=roll<nativeNarrow(uniqueChance)?ItemQuality::Unique:roll<nativeNarrow(uniqueChance+setChance)?ItemQuality::Set:ItemQuality::Rare;
        }
        if(!create(item->code,base->code,quality,unsigned(level))) return;
    }
}
} // namespace d2x
