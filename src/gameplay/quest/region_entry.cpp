#include "region_entry.hpp"
#include "module.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
std::vector<QuestEntryStep> planQuestEntry(std::span<const QuestRecord> book, const QuestEntryFacts &facts) {
    if (book.size() != size_t(QuestId::Count)) throw std::invalid_argument("Incomplete quest records");
    DifficultyQuests records;
    std::copy(book.begin(), book.end(), records.begin());
    std::vector<QuestEntryStep> result;
    for (const auto &module : questModules()) module.entry(records, facts, result);
    return result;
}
} // namespace d2x
