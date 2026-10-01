#include "state_data.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
std::string_view shrineStateName(int code) {
    // D2MOO ObjMode.cpp gpShrineTable: engine code -> States.txt identity.
    static constexpr std::array<std::string_view, 10> names{
        "shrine_armor", "shrine_combat", "shrine_resist_fire", "shrine_resist_cold",
        "shrine_resist_lightning", "shrine_resist_poison", "shrine_skill",
        "shrine_mana_regen", "shrine_stamina", "shrine_experience"};
    return code >= 6 && code <= 15 ? names[size_t(code - 6)] : std::string_view{};
}
CombatStateCatalog loadCombatStates(const DataTable &states) {
    for (const char *field : {"state", "id", "group", "remhit", "stambarblue", "curse", "plrstaydeath",
                              "monstaydeath", "bossstaydeath", "hide", "shatter", "udead", "overlay1", "overlay2"})
        if (!states.has(field)) throw std::runtime_error("Missing States.txt field: " + std::string(field));
    CombatStateCatalog result;
    std::set<int> ids;
    for (size_t row = 0; row < states.rows().size(); ++row) {
        const auto name = states.value(row, "state");
        const auto id = states.number(row, "id");
        if (name.empty() || name == "Expansion") continue;
        if (!id || *id < 0 || !ids.insert(*id).second)
            throw std::runtime_error("Invalid or duplicate States.txt identity");
        auto flag = [&](const char *field) {
            if (states.value(row, field).empty()) return false; // Native empty flag is zero.
            const auto value = states.number(row, field);
            if (!value || (*value != 0 && *value != 1))
                throw std::runtime_error("Invalid States.txt flag: " + std::string(field));
            return *value != 0;
        };
        const auto group = states.number(row, "group");
        if ((!states.value(row, "group").empty() && !group) || (group && *group < 0))
            throw std::runtime_error("Invalid States.txt group");
        CombatStateRecord record{{*id, group.value_or(0), flag("remhit"),
            {flag("plrstaydeath"), flag("monstaydeath"), flag("bossstaydeath")},
            flag("stambarblue"), flag("curse"), flag("hide"), flag("shatter"), flag("udead")},
            std::string(states.value(row, "overlay1")),
            std::string(states.value(row, "overlay2"))};
        if (!result.emplace(std::string(name), std::move(record)).second)
            throw std::runtime_error("Duplicate States.txt name");
    }
    return result;
}
} // namespace d2x
