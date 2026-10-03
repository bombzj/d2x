#pragma once

namespace d2x {
enum class MonsterRank { Normal, Minion, Champion, Unique, SuperUnique, Boss };
const char *monsterRankName(MonsterRank rank);
} // namespace d2x
