#include "owner_values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
namespace {
bool dynamicDamage(const StatRuleCatalog &rules,const StatList &list) {
    for(const auto &[key,value]:list.base) if(value) {
        const auto &name=rules.definitions.at(key.id).name;
        if(name=="item_dmgpercent_pereth" || name=="item_dmgpercent_permissinghppercent") return true;
    }
    for(const auto &child:list.contributions) if(child && dynamicDamage(rules,*child)) return true;
    return false;
}
std::shared_ptr<StatList> uncached(const StatList &source) {
    auto result=std::make_shared<StatList>(source);result->fullValues.reset();
    for(auto &child:result->contributions) child=uncached(*child);
    return result;
}
int missingLife(int32_t life,int32_t maximum) {
    if(!uint32_t(maximum)) return 0;
    const auto value=(1.0-double(uint32_t(life))/double(uint32_t(maximum)))*100.0;
    return value<double(INT32_MIN) || value>=2147483648.0?INT32_MIN:int(value);
}
}
Pd2OwnerStatValues samplePd2OwnerStats(const StatRuleCatalog &rules,const StatList &source,
    const StatSnapshot &full,int32_t life,int ethereals,StatQueryContext context) {
    auto root=std::make_shared<StatList>(source);
    const auto maximum=nativeStatValue(full,rules,"maxhp"),missing=missingLife(life,maximum);
    const auto assign=[&](std::string_view name,int value) {
        writeNativeBaseStat(root->base,rules,{rules.names.at(std::string(name)),0},value);
    };
    assign("equipped_eth",ethereals);assign("missing_hp",missing);
    StatSnapshot stored=full;
    if(dynamicDamage(rules,*root)) {
        root=uncached(*root);
        context.unit=root.get();context.storedRoot=nullptr;context.storedValues=nullptr;
        stored=evaluateStoredStatList(rules,*root,context);
    }
    stored.values.insert_or_assign({rules.names.at("equipped_eth"),0},ethereals);
    stored.values.insert_or_assign({rules.names.at("missing_hp"),0},missing);
    root->fullValues=stored.values;
    auto values=projectNativeStatValues(rules,*root,stored);
    const auto radius=nativeStatValue(values,rules,"inc_splash_radius");
    const auto coefficient=nativeStatValue(values,rules,"inc_splash_radius_permissinghp");
    // Native GetTotalStat adds this only when the original radius and both
    // owner inputs are positive. The 32-bit product has only an upper cap.
    if(radius>0 && missing>0 && coefficient>0)
        values.values.insert_or_assign({rules.names.at("inc_splash_radius"),0},
            nativeAdd(radius,std::min(40,nativeMultiply(missing,coefficient))));
    return {std::move(root),std::move(stored),std::move(values)};
}
}
