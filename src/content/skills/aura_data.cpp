#include "aura_data.hpp"
#include "content/classic_data.hpp"
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace d2x {
namespace {
int number(const DataTable &table, size_t row, std::string_view field) {
    return table.number(row, field).value_or(0);
}
size_t numberedRow(const DataTable &table, int id) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.number(row, "Id") == id) return row;
    throw std::runtime_error("Missing original monster modifier ID: " + std::to_string(id));
}
AuraFormula formula(std::string_view source) {
    if (source.size() >= 2 && source.front() == '"' && source.back() == '"')
        source = source.substr(1, source.size() - 2);
    constexpr std::pair<std::string_view, AuraFormula> formulas[]{
        {"ln12", AuraFormula::Linear12}, {"ln34", AuraFormula::Linear34}, {"ln56", AuraFormula::Linear56},
        {"par5", AuraFormula::Parameter5}, {"1", AuraFormula::One}, {"ln56/2", AuraFormula::HalfLinear56},
        {"dm34", AuraFormula::Diminishing34}, {"100-dm34", AuraFormula::RemainingDiminishing34},
        {"dm56", AuraFormula::Diminishing56}, {"edns", AuraFormula::ElementalMinimum},
        {"skill('Prayer'.edns)", AuraFormula::PrayerMinimum}, {"-dm34", AuraFormula::NegativeDiminishing34},
        {"-dm56", AuraFormula::NegativeDiminishing56}, {"-par5", AuraFormula::NegativeParameter5},
        {"-min(ln34,150)", AuraFormula::NegativeCappedLinear34}, {"toht", AuraFormula::AttackRating},
        {"skill('Resist Fire'.blvl)", AuraFormula::BaseRank}, {"skill('Resist Cold'.blvl)", AuraFormula::BaseRank},
        {"skill('Resist Lightning'.blvl)", AuraFormula::BaseRank}};
    for (const auto &[text, kind] : formulas)
        if (source == text) return kind;
    throw std::runtime_error("Unsupported monster aura formula: " + std::string(source));
}
std::optional<AuraStat> stat(std::string_view source, int skill, int state) {
    constexpr std::pair<std::string_view, AuraStat> stats[]{
        {"item_poisonlengthresist", AuraStat::PoisonLength}, {"hitpoints", AuraStat::Life},
        {"staminarecoverybonus", AuraStat::StaminaRecovery}, {"skill_staminapercent", AuraStat::StaminaPercent},
        {"manarecoverybonus", AuraStat::ManaRecovery}, {"damagepercent", AuraStat::DamagePercent},
        {"item_tohit_percent", AuraStat::AttackRatingPercent}, {"attackrate", AuraStat::AttackRate},
        {"other_animrate", AuraStat::AnimationRate}, {"velocitypercent", AuraStat::Velocity},
        {"skill_armor_percent", AuraStat::DefensePercent}, {"fireresist", AuraStat::FireResist},
        {"coldresist", AuraStat::ColdResist}, {"lightresist", AuraStat::LightningResist},
        {"damageresist", AuraStat::PhysicalResist}, {"maxfireresist", AuraStat::FireMaximum},
        {"maxcoldresist", AuraStat::ColdMaximum}, {"maxlightresist", AuraStat::LightningMaximum},
        {"thorns_percent", AuraStat::Thorns}, {"skill_concentration", AuraStat::Concentration}};
    for (const auto &[text, kind] : stats)
        if (source == text) return kind;
    if (skill == 119 && state < 0) return std::nullopt;
    throw std::runtime_error("Unsupported monster aura stat: " + std::string(source));
}
SkillDamageCurve damageCurve(const DataTable &table, size_t row, bool maximum) {
    SkillDamageCurve curve;
    curve.base = number(table, row, maximum ? "EMax" : "EMin");
    for (int index = 0; index < 5; ++index)
        curve.perLevel[size_t(index)] = number(table, row,
            std::string(maximum ? "EMaxLev" : "EMinLev") + std::to_string(index + 1));
    return curve;
}
AuraSkillSpec prepare(const ClassicData &data, int skill, int prayerRank) {
    const auto &table = data.tables.at("skills");
    const auto row = numberedRow(table, skill);
    auto value = [&](std::string_view field) { return table.value(row, field); };
    auto n = [&](std::string_view field) { return number(table, row, field); };
    AuraSkillSpec spec;
    spec.skill = skill;
    for (int index = 0; index < 8; ++index)
        spec.parameters[size_t(index)] = n("Param" + std::to_string(index + 1));
    spec.mana = n("mana"); spec.manaPerLevel = n("lvlmana"); spec.manaShift = n("manashift");
    spec.filter = uint32_t(n("aurafilter"));
    spec.hitClass = n("HitClass") ? n("HitClass") : 13;
    spec.resultFlags = uint32_t(n("ResultFlags")) | 0x20;
    spec.radius = formula(value("aurarangecalc"));
    if (skill == 66) spec.period = formula(value("auralencalc"));
    else spec.periodFrames = n("perdelay");
    if (skill == 124)
        for (int index = 0; index < 3; ++index)
            spec.redemption[size_t(index)] = formula(value("calc" + std::to_string(index + 1)));
    if (const auto state = value("aurastate"); !state.empty()) spec.ownerState = data.states.at(std::string(state)).definition;
    if (const auto state = value("auratargetstate"); !state.empty()) spec.state = data.states.at(std::string(state)).definition;
    bool needsPrayer = spec.radius == AuraFormula::PrayerMinimum ||
        (spec.period && *spec.period == AuraFormula::PrayerMinimum);
    if (skill == 124)
        for (auto calculation : spec.redemption) needsPrayer |= calculation == AuraFormula::PrayerMinimum;
    for (int slot = 1; slot <= 6; ++slot) {
        const auto field = value("aurastat" + std::to_string(slot));
        if (field.empty()) continue;
        const auto calculation = formula(value("aurastatcalc" + std::to_string(slot)));
        needsPrayer |= calculation == AuraFormula::PrayerMinimum;
        spec.stats.push_back({calculation, stat(field, skill, spec.state.id)});
    }
    spec.toHit = n("ToHit"); spec.toHitPerLevel = n("LevToHit");
    spec.minimumDamage = damageCurve(table, row, false);
    spec.maximumDamage = damageCurve(table, row, true);
    spec.damageShift = n("HitShift");
    // Keep Prayer's lazy lookup: the legacy formula does not consult it at rank 0.
    if (needsPrayer && prayerRank > 0) {
        const auto prayer = numberedRow(table, 99);
        spec.prayerMinimum = damageCurve(table, prayer, false);
        spec.prayerShift = number(table, prayer, "HitShift");
    }
    if (const auto type = value("EType"); !type.empty()) {
        spec.element = type == "fire" ? 2 : type == "ltng" ? 3 : type == "cold" ? 4 : type == "mag" ? 1 : -1;
        if (spec.element < 0) throw std::runtime_error("Unsupported monster aura damage");
    }
    return spec;
}
} // namespace
AuraSkillSpec prepareAura(const ClassicData &data, int skill) { return prepare(data, skill, 1); }
void loadAuraSkills(ClassicData &data) {
    const auto &table = data.tables.at("skills");
    for (int skill : {98, 99, 100, 102, 103, 104, 105, 108, 109, 110, 113, 114, 115, 118, 119, 120, 122, 123, 124, 125}) {
        const auto definition = resolveBaseAura(data, skill, 1);
        if (definition.periodFrames < 5 || definition.ownerState.id < 0)
            throw std::runtime_error("Original aura lacks state or periodic data");
        auto &record = data.skills.skills.at(skill);
        const auto row = numberedRow(table, skill);
        record.auraImplemented = true;
        record.auraImmediate = number(table, row, "immediate") != 0;
        if (skill == 108) {
            if (table.value(row, "passivecalc1") != "skill('Blessed Aim'.blvl) * par8")
                throw std::runtime_error("Unsupported original Blessed Aim passive formula");
            record.passiveContribution.attackRatingPerBaseRank = number(table, row, "Param8");
            record.passiveSuppressedByState = definition.ownerState.id;
            record.passiveContribution.suppressedByState = definition.ownerState.id;
        }
        if (skill == 100 || skill == 105 || skill == 110) {
            const std::string expected = skill == 100 ? "skill('Resist Fire'.blvl)/2" :
                skill == 105 ? "skill('Resist Cold'.blvl)/2" : "skill('Resist Lightning'.blvl)/2";
            if (table.value(row, "passivecalc1") != expected)
                throw std::runtime_error("Unsupported original resistance aura passive formula");
            record.passiveSuppressedByState = definition.ownerState.id;
            record.passiveContribution.maxResistElement = skill == 100 ? 2 : skill == 105 ? 4 : 3;
            record.passiveContribution.suppressedByState = definition.ownerState.id;
        }
    }
}
AuraDefinition resolveBaseAura(const ClassicData &data, int skill, int rank) {
    return evaluateBaseAura(prepare(data, skill, 0), rank);
}
std::optional<AuraDefinition> resolveAura(const ClassicData &data, int skill, int rank,
    const std::map<int, int> &ranks, int fireMasteryPercent,
    int lightningMasteryPercent, int coldDamagePercent, int prayerRank) {
    const auto *record = data.skills.find(skill);
    if (!record || !record->auraImplemented || rank <= 0) return std::nullopt;
    return evaluateAura(prepare(data, skill, prayerRank), rank, ranks, fireMasteryPercent,
        lightningMasteryPercent, coldDamagePercent, prayerRank);
}
} // namespace d2x
