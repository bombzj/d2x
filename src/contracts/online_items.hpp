#pragma once
#include "core/bytes.hpp"
#include "contracts/online_context.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct OnlineItemStat { uint16_t id{}; int64_t value{}; uint32_t parameter{}; };
// Native client serialization, not a D2S or a locally generated item.
struct OnlineItem {
    uint32_t id{}, flags{};
    uint64_t revision{};
    uint64_t socketAssignmentRevision{};
    // A native drop action survives later property/ONGROUND updates so the
    // common renderer can finish its one-shot flippy without restarting it.
    uint64_t groundAnimationRevision{}, groundAnimationReceivedMilliseconds{};
    uint8_t action{}, component{}, mode{}, body{}, page{}, x{}, y{};
    uint16_t groundX{}, groundY{};
    std::optional<uint8_t> ownerType;
    std::optional<uint32_t> owner;
    std::string code;
    Bytes packed;
    struct StatUpdate { uint32_t value{}; bool base{}; };
    std::map<std::pair<uint16_t, uint16_t>, StatUpdate> statUpdates;
};
inline bool onlineItemOwnedBy(const OnlineItem &item, std::optional<uint32_t> player) {
    return player && item.ownerType == 0 && item.owner == player;
}
inline bool onlineCursorItem(const OnlineItem &item, std::optional<uint32_t> player) {
    return item.mode == 4 && onlineItemOwnedBy(item, player);
}
inline bool onlineHasCursorItem(const std::map<uint32_t, OnlineItem> &items, std::optional<uint32_t> player) {
    if (!player) return false;
    for (const auto &[id, item] : items) {
        (void)id;
        if (onlineCursorItem(item, player)) return true;
    }
    return false;
}
enum class OnlineItemAction {
    Pickup, Take, Place, Drop, Equip, Unequip, Swap, Use, BeltPlace, BeltSwap,
    Stack, Book, Socket, Identify, SwitchWeapons,
    CubeOpen, StorageClose, Transmute, GoldDeposit, GoldWithdraw, GoldDrop,
    TradeOpen, Buy, Sell, Repair, RepairAll, IdentifyAll, QuestService, HirelingEquipment
};
struct OnlineItemCommand {
    OnlineItemAction action{};
    uint32_t item{}, target{}, npc{}, amount{};
    uint64_t itemRevision{}, targetRevision{};
    uint8_t x{}, y{}, page{}, body{}, beltSlot{};
    uint8_t equipVariant{}; // MPQ adapter selects normal / remove opposite / double swap.
    bool toCursor{}, mercenary{};
    bool gamble{}, multibuy{};
    std::optional<OnlineIntentContext> context;
};
struct OnlineItemRequest {
    enum class State { Pending, Updated, TimedOut, Interrupted, Rejected, SentNoAck };
    uint64_t sequence{};
    OnlineItemCommand command;
    State state{}; // Updated means a related server update, not guaranteed success.
};
enum class OnlineStorageKind { None, Stash, Cube };
struct OnlineStorageContext {
    OnlineStorageKind kind{}, requested{};
    std::optional<uint32_t> source, requestedSource;
    uint64_t revision{};
};
struct OnlineTradeResult {
    uint64_t revision{};
    uint8_t result{}, flags{};
    uint32_t item{}, gold{};
};
struct OnlineDecodedItem {
    uint32_t id{};
    uint64_t revision{};
    bool decoded{}, identified{}, compact{}, gamble{};
    std::string reason, name, artKey;
    int width{}, height{};
    uint16_t format{}, runeword{};
    uint8_t quality{2}, level{}, graphic{}, filledSockets{}, sockets{};
    bool hasGraphic{};
    uint16_t autoAffix{}, fileIndex{}, rarePrefix{}, rareSuffix{}, book{};
    std::array<uint16_t, 3> prefixes{}, suffixes{};
    std::string personalizedName, earName;
    uint8_t earClass{}, earLevel{};
    std::optional<uint32_t> quantity, durability, maxDurability, defense, gold, questDifficulty;
    // Property lists use serialized units (before ItemStatCost.ValShift).
    // Incremental base stats retain the original server stat units separately.
    std::vector<OnlineItemStat> stats, runewordStats, baseStats;
    std::array<std::vector<OnlineItemStat>, 5> setStats;
};
struct OnlineInventoryView {
    uint64_t revision{}, gameGeneration{};
    std::map<uint32_t, OnlineDecodedItem> items;
    std::optional<uint32_t> cursor;
    int columns{}, rows{}, beltSlots{4}, stashColumns{}, stashRows{}, cubeColumns{}, cubeRows{}, tradeColumns{}, tradeRows{};
    unsigned weaponSet{};
};
} // namespace d2x
