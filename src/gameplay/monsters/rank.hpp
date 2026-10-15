#pragma once

namespace d2x {
enum class MonsterRank { Normal, Minion, Champion, Unique, SuperUnique, Boss };
// SpawnMonster_6FC6F220 sets UNIQUE before champion conversion; that
// conversion adds CHAMPION without clearing UNIQUE. Display ranks are not
// mutually exclusive native nTypeFlag bits.
constexpr bool nativeUniqueMonster(MonsterRank rank) noexcept {
    return rank==MonsterRank::Champion || rank==MonsterRank::Unique || rank==MonsterRank::SuperUnique;
}
const char *monsterRankName(MonsterRank rank);
} // namespace d2x
