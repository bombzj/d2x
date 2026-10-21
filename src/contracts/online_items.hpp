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
    uint32_t stashPage{}; // One-based CN page within the personal/account storage domain.
    bool sharedStash{};
    bool positionKnown = true; // A native grid rebuild can invalidate placement.
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
    TradeOpen, Buy, Sell, Repair, RepairAll, IdentifyAll, QuestService, HirelingEquipment,
    BeltAuto, CubeStore, StoragePage
};
enum class OnlineStorageKind { None, Stash, Cube };
// Fixed mdll1 sample: zero-based page IDs, limits at RVA 0x3D108/0x3D10C.
// This bounds accepted metadata; it does not advertise the server's page count.
inline constexpr uint32_t onlineCnStashPageLimit = 500;
enum class OnlineCnStashNavigation { Previous, Next, Personal, Shared };
inline std::optional<OnlineCnStashNavigation> onlineCnStashCommand(uint32_t page, bool shared,
                                                                uint32_t target, bool targetShared) {
    if (!page || page > onlineCnStashPageLimit || !target || target > onlineCnStashPageLimit) return {};
    if (shared != targetShared) {
        if (target != 1) return {};
        return targetShared ? OnlineCnStashNavigation::Shared : OnlineCnStashNavigation::Personal;
    }
    if (target + 1 == page) return OnlineCnStashNavigation::Previous;
    if (target == page + 1) return OnlineCnStashNavigation::Next;
    return {}; // Native navigation is relative; do not synthesize a direct page selector.
}
// Prepared from the selected MPQ Inventory rows; networking never reads tables.
struct OnlineStashLayout {
    unsigned columns{}, rows{}, cacheColumns{}, cacheRows{};
    bool valid() const {
        return columns && columns <= 16 && rows && rows <= 16 && cacheColumns <= 255 && cacheRows <= 255 &&
            cacheColumns / columns > 1 && cacheRows / rows > 1;
    }
};
// CN item requests share the native contract. Guild/preview inventories and
// services remain separate from the player's currently authorized containers.
inline bool onlinePd2ItemAction(OnlineItemAction action) {
    switch (action) {
    case OnlineItemAction::Pickup: case OnlineItemAction::Take: case OnlineItemAction::Place:
    case OnlineItemAction::Drop: case OnlineItemAction::Swap:
    case OnlineItemAction::Equip: case OnlineItemAction::Unequip:
    case OnlineItemAction::BeltPlace: case OnlineItemAction::BeltSwap:
    case OnlineItemAction::BeltAuto: case OnlineItemAction::Use:
    case OnlineItemAction::Stack: case OnlineItemAction::Book: case OnlineItemAction::Identify:
    case OnlineItemAction::Socket: case OnlineItemAction::SwitchWeapons:
    case OnlineItemAction::CubeOpen: case OnlineItemAction::CubeStore:
    case OnlineItemAction::StorageClose: case OnlineItemAction::StoragePage: return true;
    default: return false;
    }
}
inline bool onlineBackpackItem(const OnlineItem &item, std::optional<uint32_t> player) {
    return onlineItemOwnedBy(item, player) && item.mode == 0 && item.page == 1 &&
        !(item.flags & (0x40000000u | 0x2000000u));
}
inline bool onlineActiveEquipmentItem(const OnlineItem &item, std::optional<uint32_t> player) {
    return onlineItemOwnedBy(item, player) && item.mode == 1 && item.body >= 1 && item.body <= 10;
}
inline bool onlinePd2StoredItem(const OnlineItem &item, std::optional<uint32_t> player, OnlineStorageKind storage) {
    return onlineItemOwnedBy(item, player) && item.mode == 0 &&
        !(item.flags & (0x40000000u | 0x2000000u)) &&
        (item.page == 1 || (item.page == 5 && storage == OnlineStorageKind::Stash) ||
            (item.page == 4 && storage == OnlineStorageKind::Cube));
}
inline bool onlinePd2ItemActionSource(OnlineItemAction action, const OnlineItem &item, std::optional<uint32_t> player,
                                     OnlineStorageKind storage) {
    if (item.flags & (0x40000000u | 0x2000000u)) return false;
    if (action == OnlineItemAction::Pickup) return item.mode == 3;
    if (action == OnlineItemAction::CubeStore && item.mode == 3) return true;
    if (action == OnlineItemAction::Take) return onlinePd2StoredItem(item, player, storage) ||
        onlineActiveEquipmentItem(item, player) || (onlineItemOwnedBy(item, player) && item.mode == 2);
    if (action == OnlineItemAction::Unequip) return onlineActiveEquipmentItem(item, player);
    if (action == OnlineItemAction::BeltAuto) return onlineBackpackItem(item, player);
    if (action == OnlineItemAction::Use || action == OnlineItemAction::Identify)
        return onlinePd2StoredItem(item, player, storage) || (onlineItemOwnedBy(item, player) && item.mode == 2);
    if (action == OnlineItemAction::CubeOpen) return onlinePd2StoredItem(item, player, storage);
    if (action == OnlineItemAction::Stack && onlinePd2StoredItem(item, player, storage)) return true;
    return onlinePd2ItemAction(action) && onlineCursorItem(item, player);
}
inline bool onlinePd2ItemActionTarget(OnlineItemAction action, const OnlineItem &item, std::optional<uint32_t> player,
                                     OnlineStorageKind storage) {
    if (item.flags & (0x40000000u | 0x2000000u)) return false;
    if (action == OnlineItemAction::Swap || action == OnlineItemAction::Book || action == OnlineItemAction::CubeStore)
        return onlinePd2StoredItem(item, player, storage);
    if (action == OnlineItemAction::BeltSwap) return onlineItemOwnedBy(item, player) && item.mode == 2;
    if (action == OnlineItemAction::Equip) return onlineActiveEquipmentItem(item, player);
    if (action == OnlineItemAction::Stack || action == OnlineItemAction::Socket || action == OnlineItemAction::Identify)
        return onlinePd2StoredItem(item, player, storage) || onlineActiveEquipmentItem(item, player);
    return false;
}
struct OnlineItemCommand {
    OnlineItemAction action{};
    uint32_t item{}, target{}, npc{}, amount{};
    uint64_t itemRevision{}, targetRevision{};
    uint8_t x{}, y{}, body{}, beltSlot{};
    uint32_t page{}; // Container page for items; one-based page for StoragePage.
    bool sharedStash{}; // StoragePage only; never a container/ownership flag.
    uint8_t equipVariant{}; // MPQ adapter selects normal / remove opposite / double swap.
    uint32_t oppositeItem{}; // Local precondition only; native server selects this hand.
    uint64_t oppositeRevision{};
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
inline bool onlinePd2StackCompatible(const OnlineItem &sourceWire, const OnlineDecodedItem &source,
                                     const OnlineItem &targetWire, const OnlineDecodedItem &target) {
    if (sourceWire.code != targetWire.code || source.quality < 1 || source.quality > 3 ||
        source.quality != target.quality || source.fileIndex != target.fileIndex || source.sockets || target.sockets ||
        ((sourceWire.flags ^ targetWire.flags) & 0x400000u) || !source.quantity || !target.quantity ||
        source.stats.size() != target.stats.size()) return false;
    for (size_t i = 0; i < source.stats.size(); ++i) {
        const auto &a = source.stats[i], &b = target.stats[i];
        if (a.id != b.id || a.value != b.value || a.parameter != b.parameter) return false;
    }
    return true;
}
struct OnlineInventoryView {
    uint64_t revision{}, gameGeneration{};
    std::map<uint32_t, OnlineDecodedItem> items;
    std::optional<uint32_t> cursor;
    int columns{}, rows{}, beltSlots{4}, stashColumns{}, stashRows{}, cubeColumns{}, cubeRows{}, tradeColumns{}, tradeRows{};
    unsigned weaponSet{};
};
} // namespace d2x
