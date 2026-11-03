#include "prelude_rules.hpp"

namespace d2x {
constexpr std::array<QuestPreludeDefinition, std::size_t(QuestPreludeId::Count)> questPreludes{{
    {QuestPreludeId::LutGholeinArrival, 1, 8, "jerhyn", 253, 253, {}},
    {QuestPreludeId::KurastArrival, 2, 16, "hratli", 465, 466, "Sorceress"},
    {QuestPreludeId::EncampmentArrival, 0, 0, "warriv1", 0, 1, "Paladin"},
    {QuestPreludeId::FortressArrival, 3, 24, "tyrael2", 664, 664, {}},
}};

uint16_t QuestPreludeDefinition::messageFor(std::string_view character) const {
    return character == alternateClass ? alternate : message;
}

const QuestPreludeDefinition *questPreludeForNpc(int act, std::string_view code) {
    for (const auto &prelude : questPreludes) {
        if (prelude.act == act && prelude.npcClass == code) return &prelude;
    }
    return nullptr;
}
} // namespace d2x
