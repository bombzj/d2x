#pragma once
#include "description_data.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <set>

namespace d2x {
enum class SkillDescriptionContext { Picker, TreeLevel, TreeGeneral, TreeBonus };
enum class DescriptionMastery { Exclude, Include };
struct SkillDescriptionInput {
    int skill{};
    std::optional<int> rank;
    const std::map<int,int> &baseRanks, &effectiveRanks;
    const std::map<std::string,double,std::less<>> &stats;
    bool baseRanksComplete{};
    std::optional<unsigned> difficulty{};
    std::optional<int> fireDamagePercent{}, lightningDamagePercent{}, coldDamagePercent{};
    std::optional<int> quantity{};
    // Native weapon min/max stat values, before Skill.SrcDam and skill bonuses. A
    // partial equipment snapshot is not enough to invent a weapon result.
    std::optional<std::pair<int,int>> weaponDamage{};
};
struct DescriptionRange { DescriptionNumber minimum, maximum; };
class SkillDescriptionValues {
    struct DamageOptions {
        bool maximum{}, elemental{}, mastery{}, missile{}, includeWeapon{};
    };
    const SkillDescriptionCatalog &catalog_;
    const SkillDescriptionInput &input_;
    const SkillDescriptionSource &skill_;
    unsigned depth_{};
    DescriptionNumber parameter(const SkillDescriptionSource &,std::optional<int>,std::string_view,bool missile=false);
    DescriptionNumber calculation(const SkillDescriptionSource &,std::optional<int>,std::string_view,bool missile=false);
    DescriptionNumber lookup(const SkillDescriptionSource &,std::optional<int>,const DescriptionReference &,bool missile);
    DescriptionNumber damage(const SkillDescriptionSource &,std::optional<int>,DamageOptions);
    DescriptionNumber monsterStat(const SkillDescriptionSource &,std::string_view field,std::string_view ratio) const;
    DescriptionRange elementalRange(bool missile,unsigned slot,int multiplier,DescriptionMastery);
  public:
    SkillDescriptionValues(const SkillDescriptionCatalog &,const SkillDescriptionInput &,const SkillDescriptionSource &);
    DescriptionNumber formula(const DescriptionFormula &);
    DescriptionNumber passiveContribution(std::string_view stat);
    DescriptionNumber parameter(std::string_view);
    DescriptionNumber previousParameter(std::string_view);
    DescriptionNumber stat(std::string_view) const;
    DescriptionNumber mana(bool minimumClamp=true) const;
    DescriptionNumber missileRange(unsigned slot,bool child=false,bool previousRank=false) const;
    DescriptionNumber missileParameter(unsigned slot,unsigned parameter) const;
    DescriptionRange elementalDamage(int multiplier=1);
    DescriptionRange missileElementalDamage(unsigned slot=0,int multiplier=1,DescriptionMastery=DescriptionMastery::Exclude);
    std::string_view missileElement(unsigned slot=0) const;
    DescriptionRange physicalDamage(bool includeWeapon=false);
    DescriptionRange petDamage(bool auraOnly=false);
    DescriptionRange petElementDamage(DescriptionNumber minimum,DescriptionNumber maximum) const;
    DescriptionNumber petLife(const DescriptionNumber &percent,const DescriptionNumber &flat);
    DescriptionNumber curseDuration(const DescriptionNumber &) const;
    DescriptionNumber quantity() const { return input_.quantity; }
    std::optional<int> rank() const { return input_.rank; }
    std::string_view element() const { return skill_.reference("etype"); }
    const SkillDescriptionSource &source() const { return skill_; }
};
// Additional client-side passive contributions, excluding stat lists already
// observed in native states. Equipment/state sums are prepared by the adapter.
DescriptionNumber projectDescriptionPassiveStat(const SkillDescriptionCatalog &,const SkillDescriptionInput &,
    std::string_view stat,const std::set<int> &accountedSkills);
} // namespace d2x
