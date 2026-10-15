#include "mode_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
std::shared_ptr<const StatList> prepareMonsterModeStats(const MonsterModeTable &table,const StatRuleCatalog &rules,const MonsterModeInput &input,uint64_t &random) {
    if(input.mode>=16 || input.difficulty<0 || input.difficulty>2 || input.level<0 || !table.levels || table.levels->empty())
        throw std::invalid_argument("Missing native monster mode inputs");
    const auto level=std::min(input.level,table.levels->rbegin()->first);
    const auto row=table.levels->find(level);if(row==table.levels->end()) throw std::runtime_error("Missing native MonLvl row");
    const auto &ratio=row->second.at(size_t(input.difficulty+(input.gameType?3:0)));
    const auto scale=[&](int value,int base){return table.noRatio?value:nativePercentage(base,value,100);};
    const unsigned profile=input.mode==5?1:input.mode==6 || input.mode==7 || input.mode==8?2:0;
    const auto &physical=table.physical.at(profile);
    int multiplier=0;
    const auto count=std::max(1u,input.playerCount);
    if(input.evil && input.difficulty && count>=2) multiplier=count>=9?nativeSubtract(nativeMultiply(int(count),8),16):int(8*(count-1));
    const auto multiplayer=[&](int value){return nativeAdd(value,nativeMultiplyDivide(multiplier,value,128));};
    auto list=std::make_shared<StatList>();list->extended=false;list->ownerKind=StatOwnerKind::Monster;
    list->source={StatSourceKind::Mode,input.owner,int(input.mode),input.occurrence};
    const auto set=[&](std::string_view name,int value) {
        const auto found=rules.names.find(name);if(found==rules.names.end()) throw std::runtime_error("Missing native mode stat: "+std::string(name));
        const StatKey key{found->second,0};
        writeNativeBaseStat(list->base,rules,key,value);
    };
    set("mindamage",multiplayer(nativeAdd(scale(physical.minimum,ratio.damage),input.ownerSkill.minimum)));
    set("maxdamage",multiplayer(nativeAdd(scale(physical.maximum,ratio.damage),input.ownerSkill.maximum)));
    set("tohit",multiplayer(nativeAdd(scale(physical.rating,ratio.rating),input.ownerSkill.rating)));
    for(const auto &element:table.elements) {
        if(!element.mode || element.mode!=input.mode || !element.chance) continue;
        if(element.chance<0 || element.chance>255) throw std::runtime_error("Invalid native element probability");
        if(element.chance<100 && int(rollRandom(random)%100)>=element.chance) continue;
        auto type=element.type;
        auto duration=multiplayer(element.duration);
        const auto minimum=multiplayer(scale(element.minimum,ratio.damage)),maximum=multiplayer(scale(element.maximum,ratio.damage));
        if(type=="rand") {
            const std::array<std::string_view,5> types{"fire","ltng","mag","cold","pois"};
            type=types.at(rollRandom(random)%5);if(!duration) duration=25;
        }
        // MonsterMode SetStat overwrites fields from an earlier same-type slot.
        if(type=="fire") {set("firemindam",minimum);set("firemaxdam",maximum);}
        else if(type=="ltng") {set("lightmindam",minimum);set("lightmaxdam",maximum);}
        else if(type=="mag") {set("magicmindam",minimum);set("magicmaxdam",maximum);}
        else if(type=="cold") {set("coldmindam",minimum);set("coldmaxdam",maximum);set("coldlength",duration);}
        else if(type=="pois") {set("poisonmindam",nativeMultiply(minimum,10));set("poisonmaxdam",nativeMultiply(maximum,10));set("poisonlength",nativeMultiply(duration,2));}
        else if(type=="life") {set("lifedrainmindam",minimum);set("lifedrainmaxdam",maximum);}
        else if(type=="mana") {set("manadrainmindam",minimum);set("manadrainmaxdam",maximum);}
        else if(type=="stam") {set("stamdrainmindam",minimum);set("stamdrainmaxdam",maximum);}
        else if(type=="stun") set("stunlength",duration);
        else if(type=="burn") {set("burningmin",minimum);set("burningmax",maximum);set("firelength",duration);}
    }
    return list;
}
}
