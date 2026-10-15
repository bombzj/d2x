#pragma once
#include "gameplay/effects/unit_event.hpp"
#include <string_view>
#include "gameplay/rules/formula.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/equipment.hpp"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <span>
#include <utility>

namespace d2x {
enum class FormulaDomain { Skill,Missile };
// Prepared table values only. No archive, presentation or actor ownership.
struct SkillFormulaRecord {
    int id{};
    int classId{-1},page{};
    int passiveState{-1},suppressionState{-1};
    int auraState{-1},auraTargetState{-1};
    int reactionOverlay{-1},procOverlay{-1};
    uint16_t passiveLayer{};
    uint8_t elementType{};
    std::map<std::string,int32_t,std::less<>> numbers;
    std::map<std::string,RuleFormula,std::less<>> formulas;
    std::map<std::string,std::string,std::less<>> references;
    std::array<int,3> descriptionMissiles{-1,-1,-1};
    SkillRestoreRule restore;
    std::array<SkillUnitEventBinding,3> unitEvents;
    bool useThrowMastery{}; // D2Common_11024: current used skill's Range/ITypeA[0].
    int32_t number(std::string_view) const;
    std::string_view reference(std::string_view) const;
};
struct SkillFormulaCatalog {
    int maximumSkillRank{}; // Experience.MaxLvl for native class zero.
    int skillShrineState{-1}; // Current States identity, not an allskills stat.
    int corpseNoDrawState{-1},corpseNoSelectState{-1};
    int infernoState{-1};
    std::map<int,SkillFormulaRecord> skills,missiles;
    std::map<std::string,int,std::less<>> skillIds,missileIds;
    std::map<uint16_t,std::string> itemTypes;
};
struct SkillFormulaRanks {std::map<int,int> base,effective;};
SkillFormulaRanks prepareSkillFormulaRanks(const std::map<int,int> &base,const std::map<int,int> &effective,
    const std::set<int> &innate,std::span<const ChargedSkill>,int maximum);
struct SkillFormulaInput {
    const std::map<int,int> &baseRanks,&effectiveRanks;
    const StatRuleCatalog *statRules{};
    const StatList *actor{};
    const StatSnapshot *stats{};
    // Mutable resources are borrowed at the call site, never cached in Totals.
    std::map<StatKey,int32_t> currentResources;
    int attackRatingClassFactor{};
    RuleRandom random;
    std::optional<SkillFormulaRanks> referenceRanks;
    std::set<std::string,std::less<>> unavailableStats;
    const StatSnapshot *weaponStats{};
    bool twoHanded{};
    // Server adapters may sample mutable actor/weapon totals for this call.
    // Shared ownership keeps the pointers valid when the input is returned.
    std::shared_ptr<const StatSnapshot> sampledStats,sampledWeaponStats;
};
// Owned nonrandom inputs at one synchronous attribute callback. Later sources
// must not change an immediate skill's inputs; periodic execution samples anew.
struct SkillFormulaSnapshot {
    std::map<int,int> baseRanks,effectiveRanks;
    std::shared_ptr<const StatList> actor;
    StatSnapshot stats;
    std::optional<StatSnapshot> weaponStats;
    std::map<StatKey,int32_t> currentResources;
    int attackRatingClassFactor{};
    std::optional<SkillFormulaRanks> referenceRanks;
    std::set<std::string,std::less<>> unavailableStats;
    bool twoHanded{};
    // The caller owns the unit RNG candidate at this same execution point.
    // A callback closure borrowed from the original input is never retained.
    SkillFormulaInput input(const StatRuleCatalog &,RuleRandom random={}) const &;
    SkillFormulaInput input(const StatRuleCatalog &,RuleRandom random={}) const &&=delete;
};
SkillFormulaSnapshot captureSkillFormulaInput(const SkillFormulaInput &);
class SkillFormulaValues {
    const SkillFormulaCatalog &catalog_;
    const SkillFormulaInput &input_;
    unsigned depth_{};
    RuleNumber lookup(FormulaDomain,const SkillFormulaRecord &,int,const RuleReference &);
    RuleNumber parameter(FormulaDomain,const SkillFormulaRecord &,int,std::string_view);
    RuleNumber damage(FormulaDomain,const SkillFormulaRecord &,int,bool,bool,bool);
    RuleNumber calculation(FormulaDomain,const SkillFormulaRecord &,int,std::string_view,
        std::optional<std::pair<int,int>> missileArguments={});
  public:
    SkillFormulaValues(const SkillFormulaCatalog &catalog,const SkillFormulaInput &input):catalog_(catalog),input_(input) {}
    RuleNumber stat(std::string_view name,bool base=false) const;
    RuleNumber parameter(FormulaDomain,int id,int rank,std::string_view token);
    // Missile Rand aliases the evaluator's by-value id/rank arguments in 1.13c.
    // Overrides preserve calls that resolve the missile through (-1,-1).
    RuleNumber calculation(FormulaDomain,int id,int rank,std::string_view field,
        std::optional<std::pair<int,int>> missileArguments={});
    std::optional<std::pair<int32_t,int32_t>> physicalRange(int id,int rank,bool includeWeapon);
    std::vector<StatValue> auraStats(int id,int rank,int slots=6,bool skipZero=false,bool stopAtMissing=false);
    std::optional<StatKey> auraStatKey(int id,int slot) const;
    std::vector<StatValue> passiveBuffStats(int id,int rank,bool skipZero=true);
    std::vector<std::pair<int,int>> summonSkills(int id,int rank);
    const StatRuleCatalog &statRules() const;
    std::span<const SkillUnitEventBinding> unitEvents(int skill) const;
    std::string_view elementalCode(int skill) const;
    int32_t skillNumber(int skill,std::string_view field) const;
    int skillOverlay(int skill,bool proc) const;
};
}
