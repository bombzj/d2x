#pragma once
#include <string>

namespace d2x {
struct ClassicData;
class MonsterCatalog;
class WorldCatalog;
struct LootRequest;
enum class LootEntryStatus { Ready, Empty, Deferred };
struct MonsterLootEntry {
    LootEntryStatus status = LootEntryStatus::Deferred;
    std::string treasureClass, reason;
    int itemLevel = 0, upgradeLevel = 0;
};
MonsterLootEntry resolveMonsterLoot(const ClassicData &data, const MonsterCatalog &monsters,
                                  const WorldCatalog &world, const LootRequest &request);
} // namespace d2x