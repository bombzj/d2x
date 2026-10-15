#pragma once
#include "stat_list.hpp"
#include "gameplay/character/attributes.hpp"
#include <string_view>
#include <span>

namespace d2x {
struct ModifierStatSource {
    StatSource source;
    CharacterModifiers modifiers;
    std::optional<std::map<StatKey,int32_t>> rawValues;
};
std::shared_ptr<const StatList> prepareModifierStatSource(const StatRuleCatalog &,const ModifierStatSource &,StatOwnerKind);
StatSnapshot evaluateStatListWithSources(const StatRuleCatalog &,const StatList &,std::span<const ModifierStatSource>,StatQueryContext={});
void appendNativeModifierValues(StatList &,const StatRuleCatalog &,const CharacterModifiers &);
void addNativeStat(StatList &,const StatRuleCatalog &,std::string_view,int32_t,uint16_t layer=0,bool raw=false);
void setNativeStat(StatList &,const StatRuleCatalog &,std::string_view,int32_t,uint16_t layer=0,bool raw=false);
int32_t nativeStatValue(const StatSnapshot &,const StatRuleCatalog &,std::string_view,uint16_t layer=0);
int32_t logicalStatValue(const StatSnapshot &,const StatRuleCatalog &,std::string_view,uint16_t layer=0);
CombatModifiers nativeCombatModifiers(const StatSnapshot &,const StatRuleCatalog &,const CombatModifiers &procedures = {});
CharacterModifiers nativeCharacterModifiers(const StatSnapshot &,const StatRuleCatalog &,const CharacterModifiers &procedures={});
StatSnapshot evaluateStatListWithModifiers(const StatRuleCatalog &,const StatList &,const CharacterModifiers &,
    StatQueryContext = {},StatSource source = {});
}
