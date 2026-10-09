#include "remote_inventory.hpp"
#include "gameplay/items/quality.hpp"
#include "network/protocol/bits.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

namespace d2x {
using net::protocol::BitReader;
RemoteInventory::RemoteInventory(Archives &archives) : strings_(archives) {
    for (const char *name : {"weapons", "armor", "misc", "itemtypes", "itemstatcost", "inventory", "belts", "books", "charstats", "bodylocs", "npc", "monstats"})
        tables_.emplace(name, DataTable(archives.read("data/global/excel/" + std::string(name) + ".txt")));
    for (const char *name : {"weapons", "armor", "misc"}) {
        const auto &table = tables_.at(name);
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (auto code = table.value(row, "code"); !code.empty()) bases_.emplace(std::string(code), Base{name, row});
    }
    const auto &types = tables_.at("itemtypes"), &stats = tables_.at("itemstatcost");
    for (size_t row = 0; row < types.rows().size(); ++row)
        if (auto code = types.value(row, "Code"); !code.empty()) typeRows_.emplace(std::string(code), row);
    for (size_t row = 0; row < stats.rows().size(); ++row)
        if (auto id = stats.number(row, "ID"); id && *id >= 0 && *id < 511) statRows_.emplace(unsigned(*id), row);
    const auto &inventory = tables_.at("inventory");
    for (size_t row = 0; row < inventory.rows().size(); ++row) {
        const auto name = inventory.value(row, "class");
        if (name == "Big Bank Page2") {
            view_.stashColumns = inventory.number(row, "gridX").value_or(0);
            view_.stashRows = inventory.number(row, "gridY").value_or(0);
        } else if (name == "Transmogrify Box2") {
            view_.cubeColumns = inventory.number(row, "gridX").value_or(0);
            view_.cubeRows = inventory.number(row, "gridY").value_or(0);
        } else if (name == "Trade Page 2-2") {
            view_.tradeColumns = inventory.number(row, "gridX").value_or(0);
            view_.tradeRows = inventory.number(row, "gridY").value_or(0);
        }
    }
}
int RemoteInventory::baseNumber(const OnlineItem &item, std::string_view column) const {
    const auto found = bases_.find(item.code);
    return found == bases_.end() ? 0 : tables_.at(found->second.table).number(found->second.row, column).value_or(0);
}
bool RemoteInventory::isType(const OnlineItem &item, std::string_view wanted) const {
    const auto base = bases_.find(item.code);
    if (base == bases_.end()) return false;
    const auto &table = tables_.at(base->second.table), &types = tables_.at("itemtypes");
    std::set<std::string, std::less<>> visited;
    std::function<bool(std::string_view)> visit = [&](std::string_view code) {
        if (code.empty() || !visited.emplace(code).second) return false;
        if (code == wanted) return true;
        const auto row = typeRows_.find(code);
        return row != typeRows_.end() && (visit(types.value(row->second, "Equiv1")) || visit(types.value(row->second, "Equiv2")));
    };
    return visit(table.value(base->second.row, "type")) || visit(table.value(base->second.row, "type2"));
}
OnlineDecodedItem RemoteInventory::decode(const OnlineItem &wire) const {
    OnlineDecodedItem item; item.id = wire.id; item.revision = wire.revision;
    const auto base = bases_.find(wire.code);
    if (base == bases_.end()) { item.reason = "Current MPQ has no matching item base"; return item; }
    const auto &table = tables_.at(base->second.table), &cost = tables_.at("itemstatcost");
    item.width = baseNumber(wire, "invwidth"); item.height = baseNumber(wire, "invheight");
    item.name = strings_.find(table.value(base->second.row, "namestr"));
    const auto icon = table.value(base->second.row, "invfile");
    if (!icon.empty()) item.artKey = "data/global/items/" + std::string(icon) + ".dc6";
    try {
        // D2MOO ITEMS_SerializeItem*(bServer=false), MIT; docs/licenses/D2MOO.txt.
        // No JM signature, seed or realm tail; unidentified quality fields are omitted.
        BitReader bits(wire.packed);
        const uint32_t flags = bits.read(32);
        item.identified = (flags & 0x10) != 0; item.compact = (flags & 0x200000) != 0;
        item.gamble = (flags & 0x2000000) != 0;
        item.format = uint16_t(bits.read(10));
        const auto mode = bits.read(3);
        if (mode == 3 || mode == 5) { bits.read(16); bits.read(16); }
        else { bits.read(4); bits.read(4); bits.read(4); bits.read(3); }
        auto name = [&] {
            std::string value;
            for (unsigned i = 0; i < 16; ++i) {
                const auto c = bits.read(7); if (!c) return value;
                if (i == 15) throw std::runtime_error("Native item name exceeds 15 characters");
                value += char(c);
            }
            return value;
        };
        auto statValue = [&](unsigned id) -> int64_t {
            const auto row = statRows_.find(id);
            if (row == statRows_.end()) throw std::runtime_error("Unknown native item stat");
            const auto width = cost.number(row->second, "Save Bits").value_or(0);
            if (width < 1 || width > 32) throw std::runtime_error("Invalid native item stat width");
            return int64_t(bits.read(unsigned(width))) - cost.number(row->second, "Save Add").value_or(0);
        };
        auto unsignedStat = [&](unsigned id) {
            const auto value = statValue(id);
            if (value < 0 || value > UINT32_MAX) throw std::runtime_error("Invalid native item base stat");
            return uint32_t(value);
        };
        auto gold = [&] { item.gold = bits.read(bits.read(1) ? 32 : 12); };
        if (item.compact && (flags & 0x10000)) {
            item.earClass = uint8_t(bits.read(3)); item.earLevel = uint8_t(bits.read(7)); item.earName = name();
        } else {
            bits.read(32); // Base code already validated in the bounded wire prefix.
            if (item.compact) {
                if (isType(wire, "gold")) gold();
                if (baseNumber(wire, "quest") && baseNumber(wire, "questdiffcheck")) item.questDifficulty = unsignedStat(356);
            } else if (!item.gamble) {
                item.filledSockets = uint8_t(bits.read(3)); item.level = uint8_t(bits.read(7));
                item.quality = uint8_t(bits.read(4));
                if (!itemQualityFromNative(item.quality)) throw std::runtime_error("Unsupported native item quality");
                item.hasGraphic = bits.read(1) != 0;
                if (item.hasGraphic) item.graphic = uint8_t(bits.read(3));
                if (bits.read(1)) item.autoAffix = uint16_t(bits.read(11));
                switch (item.quality) {
                case 1: case 3: item.fileIndex = uint16_t(bits.read(3)); break;
                case 2:
                    if (isType(wire, "char") && item.identified)
                        (bits.read(1) ? item.prefixes[0] : item.suffixes[0]) = uint16_t(bits.read(11));
                    if (isType(wire, "body") && !isType(wire, "play")) item.fileIndex = uint16_t(bits.read(10));
                    if (isType(wire, "book") || isType(wire, "scro")) item.book = uint16_t(bits.read(5));
                    break;
                case 4:
                    if (item.identified) { item.prefixes[0] = uint16_t(bits.read(11)); item.suffixes[0] = uint16_t(bits.read(11)); }
                    break;
                case 5: case 7: if (item.identified) item.fileIndex = uint16_t(bits.read(12)); break;
                case 6: case 8: case 9:
                    if (item.identified) { item.rarePrefix = uint16_t(bits.read(8)); item.rareSuffix = uint16_t(bits.read(8)); }
                    if (item.quality != 9)
                        for (size_t i = 0; i < 3; ++i) {
                            if (bits.read(1)) item.prefixes[i] = uint16_t(bits.read(11));
                            if (bits.read(1)) item.suffixes[i] = uint16_t(bits.read(11));
                        }
                    break;
                }
                if (flags & 0x4000000) item.runeword = uint16_t(bits.read(16));
                if (flags & 0x10000) { item.earClass = uint8_t(bits.read(3)); item.earLevel = uint8_t(bits.read(7)); item.earName = name(); }
                else if (flags & 0x1000000) item.personalizedName = name();
                if (base->second.table == "armor") item.defense = unsignedStat(31);
                if (base->second.table != "misc") {
                    item.maxDurability = unsignedStat(73);
                    if (*item.maxDurability) item.durability = unsignedStat(72);
                } else if (isType(wire, "gold")) gold();
                if (baseNumber(wire, "stackable")) item.quantity = bits.read(9);
                if (flags & 0x800) {
                    const auto row = statRows_.find(194);
                    if (row == statRows_.end()) throw std::runtime_error("Socket count format is unavailable");
                    const int width = cost.number(row->second, "Save Bits").value_or(0);
                    if (width < 1 || width > 8) throw std::runtime_error("Invalid native socket count width");
                    item.sockets = uint8_t(bits.read(unsigned(width))); // Native socket count has no Save Add.
                }
                if (item.identified) {
                    const auto setMask = item.quality == 5 ? bits.read(5) : 0;
                    auto stats = [&](std::vector<OnlineItemStat> &out) {
                        for (unsigned count = 0; count < 512; ++count) {
                            const auto id = bits.read(9); if (id == 511) return;
                            const unsigned extra = id == 17 || id == 48 || id == 50 || id == 52 ? 1
                                : id == 54 || id == 57 ? 2 : 0;
                            for (unsigned n = 0; n <= extra; ++n) {
                                const auto row = statRows_.find(id + n);
                                if (row == statRows_.end()) throw std::runtime_error("Unknown native item property");
                                const auto width = extra ? 0 : cost.number(row->second, "Save Param Bits").value_or(0);
                                if (width < 0 || width > 32) throw std::runtime_error("Invalid native item parameter width");
                                const auto parameter = bits.read(unsigned(width));
                                out.push_back({uint16_t(id + n), statValue(id + n), parameter});
                            }
                        }
                        throw std::runtime_error("Native item property list exceeds limit");
                    };
                    stats(item.stats);
                    for (size_t i = 0; i < item.setStats.size(); ++i) if (setMask & (1u << i)) stats(item.setStats[i]);
                    if (flags & 0x4000000) stats(item.runewordStats);
                }
            }
        }
        if (item.hasGraphic) {
            const auto type = typeRows_.find(table.value(base->second.row, "type"));
            if (type != typeRows_.end()) {
                const auto art = tables_.at("itemtypes").value(type->second, "InvGfx" + std::to_string(item.graphic + 1));
                if (!art.empty()) item.artKey = "data/global/items/" + std::string(art) + ".dc6";
            }
        }
        for (const auto &[key, update] : wire.statUpdates) {
            const auto [id, parameter] = key;
            const auto definition = statRows_.find(id);
            if (definition != statRows_.end() && cost.value(definition->second, "Stat") == "item_charged_skill") {
                if (update.value > 65535 || (update.value & 0xff) > (update.value >> 8))
                    throw std::runtime_error("Invalid incremental charged skill counts");
                unsigned matches = 0;
                auto replace = [&](auto &stats) {
                    for (auto &stat : stats) {
                        if (stat.id != id || stat.parameter != parameter) continue;
                        stat.value = update.value;
                        ++matches;
                    }
                };
                replace(item.stats);
                replace(item.runewordStats);
                for (auto &stats : item.setStats) replace(stats);
                if (matches != 1) throw std::runtime_error("Charged skill update has no unambiguous assigned property");
                continue;
            }
            if (update.base) {
                item.baseStats.push_back({id, int64_t(update.value), parameter});
                if (parameter) continue;
                if (id == 70) item.quantity = update.value;
                else if (id == 72) item.durability = update.value;
                else if (id == 73) item.maxDurability = update.value;
                else if (id == 31) item.defense = update.value;
                else if (id == 14) item.gold = update.value;
                continue;
            }
            const auto row = statRows_.find(id);
            if (row == statRows_.end()) throw std::runtime_error("Unknown incremental item property");
            const int shift = cost.number(row->second, "ValShift").value_or(0);
            if (shift < 0 || shift >= 32) throw std::runtime_error("Invalid native item property scale");
            unsigned matches = 0;
            auto replace = [&](auto &stats) {
                for (auto &stat : stats) {
                    if (stat.id != id || stat.parameter != parameter) continue;
                    stat.value = int64_t(int32_t(update.value)) >> unsigned(shift);
                    ++matches;
                }
            };
            replace(item.stats);
            replace(item.runewordStats);
            for (auto &stats : item.setStats) replace(stats);
            if (matches > 1) throw std::runtime_error("Item property update has ambiguous list ownership");
            if (!matches) item.stats.push_back({id, int64_t(int32_t(update.value)) >> unsigned(shift), parameter});
        }
        item.decoded = true;
    } catch (const std::exception &error) { item.reason = error.what(); }
    return item;
}
void RemoteInventory::update(const OnlineView &online) {
    std::optional<uint16_t> playerClass;
    if (online.load.playerUnitId)
        if (const auto found = online.world.units.find({0, *online.load.playerUnitId}); found != online.world.units.end())
            playerClass = found->second.classId;
    if (sourceRevision_ == online.world.itemRevision && generation_ == online.gameGeneration &&
        player_ == online.load.playerUnitId && playerClass_ == playerClass) return;
    player_ = online.load.playerUnitId; playerClass_ = playerClass;
    sourceRevision_ = online.world.itemRevision; generation_ = online.gameGeneration;
    view_.revision = sourceRevision_; view_.gameGeneration = generation_; view_.items.clear();
    view_.cursor.reset(); view_.beltSlots = 4; view_.weaponSet = online.world.weaponSet;
    view_.columns = view_.rows = 0;
    if (online.load.playerUnitId) {
        const auto player = online.world.units.find({0, *online.load.playerUnitId});
        if (player != online.world.units.end() && player->second.classId) {
            const auto &characters = tables_.at("charstats"), &inventory = tables_.at("inventory");
            const size_t classId = *player->second.classId;
            if (classId < characters.rows().size()) {
                const auto name = std::string(characters.value(classId, "class")) + "2";
                for (size_t row = 0; row < inventory.rows().size(); ++row)
                    if (inventory.value(row, "class") == name) {
                        view_.columns = inventory.number(row, "gridX").value_or(0);
                        view_.rows = inventory.number(row, "gridY").value_or(0); break;
                    }
            }
        }
    }
    for (const auto &[id, wire] : online.world.items) {
        view_.items.emplace(id, decode(wire));
        if (!onlineItemOwnedBy(wire, online.load.playerUnitId)) continue;
        if (wire.mode == 4) view_.cursor = id;
        if (wire.mode == 1 && wire.body == 8) {
            const int index = baseNumber(wire, "belt");
            const auto &belts = tables_.at("belts");
            if (index >= 0 && size_t(index) < belts.rows().size()) view_.beltSlots = belts.number(size_t(index), "numboxes").value_or(4);
        }
    }
}
bool RemoteInventory::reject(std::string reason) { reason_ = std::move(reason); return false; }
bool RemoteInventory::submit(net::RealmSession &session, OnlineItemCommand command) {
    update(session.read());
    const auto &online = session.read();
    if (!command.context) command.context = onlineIntentContext(online);
    if (!onlineInteractionMatches(*command.context, online))
        return reject("Queued item intent belongs to a previous game, area or interaction");
    const auto &world = online.world;
    if (online.stage != OnlineStage::ProtocolReady || !online.load.serverLoadComplete ||
        !online.load.playerUnitId || !world.playerPosition || onlinePlayerDead(world))
        return reject("The server player is unavailable for item operations");
    if (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending && command.action != OnlineItemAction::StorageClose)
        return reject("Wait for the pending item request's server update or timeout");
    auto send = [&] {
        if (!session.submit_item(command)) return reject(session.read().error ? session.read().error->message : "Item command is rate limited");
        reason_.clear(); return true;
    };
    auto npcIdentity = [&]() -> std::string_view {
        if (!world.npcConversation || world.npcRequested != world.npcConversation->source) return {};
        if (command.npc && command.npc != world.npcConversation->source) return {};
        command.npc = world.npcConversation->source;
        const auto unit = world.units.find({1, command.npc});
        if (unit == world.units.end() || !unit->second.classId) return {};
        const auto &monsters = tables_.at("monstats");
        for (size_t row = 0; row < monsters.rows().size(); ++row)
            if (monsters.number(row, "hcIdx") == *unit->second.classId) return monsters.value(row, "Id");
        return {};
    };
    if (command.action >= OnlineItemAction::TradeOpen) {
        const auto identity = npcIdentity();
        const bool sellingCursor = (command.action == OnlineItemAction::Sell || command.action==OnlineItemAction::QuestService) && view_.cursor == command.item;
        if(command.action==OnlineItemAction::QuestService) {
            if((identity!="charsi" && identity!="larzuk" && identity!="drehya") || view_.cursor!=command.item) return reject("Original quest service requires the owned cursor item and reward NPC");
            const auto wire=world.items.find(command.item);const auto decoded=view_.items.find(command.item);
            if(wire==world.items.end() || decoded==view_.items.end() || !decoded->second.decoded || (command.itemRevision && command.itemRevision!=wire->second.revision)) return reject("Quest item changed before submission");
            command.itemRevision=wire->second.revision;return send();
        }
        if (identity.empty() || (view_.cursor && !sellingCursor)) return reject("NPC service requires an active server conversation and a compatible cursor");
        bool vendor = false;
        const auto &npcs = tables_.at("npc");
        for (size_t row = 0; row < npcs.rows().size(); ++row) vendor |= npcs.value(row, "npc") == identity;
        if (command.action == OnlineItemAction::IdentifyAll) {
            if (identity != "cain2" && identity != "cain3" && identity != "cain4" && identity != "cain5" && identity != "cain6")
                return reject("The original identify-all service requires a town Cain");
            return send();
        }
        if (command.gamble) {
            if (identity != "gheed" && identity != "elzix" && identity != "alkor" && identity != "jamella" && identity != "drehya" && identity != "nihlathak")
                return reject("This original NPC does not offer gambling");
        } else if (!vendor || identity == "nihlathak") return reject("Current MPQ and original rules do not identify a normal trader");
        if (command.action == OnlineItemAction::TradeOpen) return send();
        if (command.action == OnlineItemAction::Buy && command.gamble != world.shopGamble) return reject("Purchase belongs to a different vendor service");
        if (world.shopSource != command.npc) return reject("Wait for the current NPC's server shelf before trading");
        if (command.action == OnlineItemAction::Repair || command.action == OnlineItemAction::RepairAll) {
            if (identity != "charsi" && identity != "fara" && identity != "hratli" && identity != "halbu" && identity != "larzuk")
                return reject("This original NPC does not repair equipment");
            if (command.action == OnlineItemAction::RepairAll) return send();
        }
    }
    if (command.action == OnlineItemAction::SwitchWeapons || command.action == OnlineItemAction::StorageClose ||
        command.action == OnlineItemAction::Transmute || command.action == OnlineItemAction::GoldDeposit ||
        command.action == OnlineItemAction::GoldWithdraw || command.action == OnlineItemAction::GoldDrop) {
        if (command.action == OnlineItemAction::StorageClose) {
            if (world.storage.kind == OnlineStorageKind::None && world.storage.requested == OnlineStorageKind::None)
                return reject("No stash or cube context is open or pending");
            return send();
        }
        if (view_.cursor || world.townPortalPending) return reject("Clear the cursor and pending portal before switching weapons");
        if (command.action == OnlineItemAction::Transmute) {
            if (world.storage.kind != OnlineStorageKind::Cube) return reject("Wait for a server-opened cube");
            if (std::none_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
                return entry.second.ownerType == 0 && entry.second.owner == online.load.playerUnitId &&
                    entry.second.mode == 0 && entry.second.page == 4;
            })) return reject("Cube has no server-reported ingredients");
        }
        if (command.action == OnlineItemAction::GoldDeposit || command.action == OnlineItemAction::GoldWithdraw ||
            command.action == OnlineItemAction::GoldDrop) {
            if (!command.amount || command.amount > INT32_MAX) return reject("Gold amount must be a positive original signed DWORD");
            if (command.action != OnlineItemAction::GoldDrop && world.storage.kind != OnlineStorageKind::Stash)
                return reject("Gold transfer requires a server-opened stash");
            const auto balance = world.playerAttributes.find(command.action == OnlineItemAction::GoldWithdraw ? 15 : 14);
            if (balance == world.playerAttributes.end() || balance->second < command.amount) return reject("Insufficient server-reported gold");
        }
        return send();
    }
    const auto found = world.items.find(command.item);
    if (found == world.items.end()) return reject("Item is not assigned in the current game");
    const auto &wire = found->second;
    const auto &decoded = view_.items.at(command.item);
    if (!decoded.decoded || (decoded.gamble && (command.action != OnlineItemAction::Buy || !command.gamble))) return reject("Item data cannot be operated: " + decoded.reason);
    if (command.itemRevision && command.itemRevision != wire.revision) return reject("Stale item revision");
    command.itemRevision = wire.revision;
    auto owned = [&](const OnlineItem &item) { return onlineItemOwnedBy(item, online.load.playerUnitId); };
    auto backpack = [&](const OnlineItem &item) { return owned(item) && item.mode == 0 && item.page == 1; };
    auto accessible = [&](const OnlineItem &item) {
        return owned(item) && item.mode == 0 && (item.page == 1 ||
            (item.page == 3 && world.playerTrade.phase == OnlinePlayerTrade::Phase::Open) ||
            (item.page == 4 && world.storage.kind == OnlineStorageKind::Cube) ||
            (item.page == 5 && world.storage.kind == OnlineStorageKind::Stash));
    };
    auto bodyItem = [&](uint8_t body) -> const OnlineItem * {
        for (const auto &[id, item] : world.items)
            if (owned(item) && item.mode == 1 && item.body == body) return &item;
        return nullptr;
    };
    auto cursor = [&] { return owned(wire) && wire.mode == 4 && view_.cursor == command.item; };
    const OnlineItem *target = nullptr;
    const OnlineDecodedItem *targetData = nullptr;
    const bool needsTarget = command.action == OnlineItemAction::Swap || command.action == OnlineItemAction::BeltSwap ||
        command.action == OnlineItemAction::Stack || command.action == OnlineItemAction::Book ||
        command.action == OnlineItemAction::Socket || command.action == OnlineItemAction::Identify;
    if (needsTarget) {
        const auto entry = world.items.find(command.target);
        if (entry == world.items.end() || command.target == command.item || !owned(entry->second))
            return reject("Target must be a different item belonging to this player");
        target = &entry->second; targetData = &view_.items.at(command.target);
        if (!targetData->decoded || targetData->gamble) return reject("Target item data is unavailable");
        if (command.targetRevision && command.targetRevision != target->revision) return reject("Stale target item revision");
        command.targetRevision = target->revision;
    }
    auto placement = [&](uint32_t replaced = UINT32_MAX) {
        const int columns = command.page == 0 ? view_.columns : command.page == 2 ? view_.tradeColumns : command.page == 3 ? view_.cubeColumns : view_.stashColumns;
        const int rows = command.page == 0 ? view_.rows : command.page == 2 ? view_.tradeRows : command.page == 3 ? view_.cubeRows : view_.stashRows;
        if ((command.page != 0 && command.page != 2 && command.page != 3 && command.page != 4) ||
            (command.page == 2 && world.playerTrade.phase != OnlinePlayerTrade::Phase::Open) ||
            (command.page == 3 && world.storage.kind != OnlineStorageKind::Cube) ||
            (command.page == 4 && world.storage.kind != OnlineStorageKind::Stash) ||
            (world.storage.kind == OnlineStorageKind::Cube && world.storage.source == command.item) ||
            decoded.width < 1 || decoded.height < 1 || int(command.x) + decoded.width > columns || int(command.y) + decoded.height > rows) return false;
        for (const auto &[id, item] : world.items) {
            if (id == replaced || !owned(item) || item.mode != 0 || item.page != command.page + 1) continue;
            const auto &data = view_.items.at(id);
            if (!data.decoded || data.width < 1 || data.height < 1) return false;
            if (command.x < item.x + data.width && command.x + decoded.width > item.x &&
                command.y < item.y + data.height && command.y + decoded.height > item.y) return false;
        }
        return true;
    };
    switch (command.action) {
    case OnlineItemAction::Pickup: {
        if (wire.mode != 3 || view_.cursor) return reject("Pickup requires an assigned ground item and an empty cursor");
        const auto point = *world.playerPosition;
        if (nativeUnitDistance({float(point.x), float(point.y)}, 2,
            {float(wire.groundX), float(wire.groundY)}, 1) > 50) return reject("Ground item exceeds native pickup range");
        break;
    }
    case OnlineItemAction::HirelingEquipment:
        if(!world.hireling || world.deadHirelingName || (command.body!=1 && command.body!=3 && command.body!=4))
            return reject("No living hireling or invalid equipment slot");
        if(cursor()) {
            if(command.mercenary && (isType(wire,"hpot") || isType(wire,"rpot") || isType(wire,"apot") || isType(wire,"wpot"))) break;
            if(!decoded.identified || (decoded.maxDurability && *decoded.maxDurability && decoded.durability==0) ||
                !(isType(wire,"helm") || isType(wire,"tors") || isType(wire,"bow"))) return reject("Item is not eligible Rogue equipment");
        } else if(view_.cursor || wire.ownerType!=1 || wire.owner!=world.hireling->id || wire.mode!=1 || wire.body!=command.body)
            return reject("Hireling equipment is no longer assigned");
        break;
    case OnlineItemAction::Take:
        if (!owned(wire) || view_.cursor || !(accessible(wire) || wire.mode == 2 ||
            (wire.mode == 1 && wire.body >= 1 && wire.body <= 10))) return reject("Only this player's backpack, belt or active equipment can be taken");
        if (world.storage.kind == OnlineStorageKind::Cube && world.storage.source == command.item)
            return reject("Close the cube before moving its host item");
        command.body = wire.body; break;
    case OnlineItemAction::Place:
        if (!cursor() || !placement()) return reject("Cursor item does not fit the requested accessible container cells");
        break;
    case OnlineItemAction::Drop:
        if (!cursor()) return reject("Only the server cursor item can be dropped");
        break;
    case OnlineItemAction::Equip: {
        if (!cursor() || !decoded.identified || command.body < 1 || command.body > 10)
            return reject("Equip requires an identified cursor item and a native active body slot 1-10");
        const auto &base = bases_.at(wire.code); const auto &table = tables_.at(base.table), &types = tables_.at("itemtypes");
        const auto type = typeRows_.find(table.value(base.row, "type"));
        const auto &locations = tables_.at("bodylocs");
        if (type == typeRows_.end() || command.body >= locations.rows().size()) return reject("Original body location is unavailable");
        const auto code = locations.value(command.body, "Code");
        if (code.empty() || (types.value(type->second, "BodyLoc1") != code && types.value(type->second, "BodyLoc2") != code))
            return reject("MPQ item type does not fit that body slot");
        break; // Attribute/class/two-handed combinations are finally validated by the original server.
    }
    case OnlineItemAction::Unequip:
        if (!owned(wire) || wire.mode != 1 || wire.body < 1 || wire.body > 10 || view_.cursor)
            return reject("Unequip requires an active equipped item and an empty cursor");
        command.body = wire.body; break;
    case OnlineItemAction::Swap:
        command.page = target->page ? target->page - 1 : 255;
        if (!cursor() || !accessible(*target) || !placement(command.target))
            return reject("Swap requires the cursor item and a fitting backpack destination");
        break;
    case OnlineItemAction::Use:
        if (!owned(wire) || !(backpack(wire) || wire.mode == 2) || !baseNumber(wire, "useable"))
            return reject("Use requires a usable item in this player's backpack or belt");
        if (command.mercenary && wire.mode != 2) return reject("Native mercenary item use requires a belt item");
        if (baseNumber(wire, "pSpell") == 7)
            return reject("Cube UI context and transmutation are not connected yet");
        for (size_t row = 0; row < tables_.at("books").rows().size(); ++row) {
            const auto &books = tables_.at("books");
            if (books.value(row, "ScrollSkill") == "Scroll of Townportal" &&
                (books.value(row, "ScrollSpellCode") == wire.code || books.value(row, "BookSpellCode") == wire.code))
                return reject("Use online-town-portal to cast an available native portal skill");
        }
        break;
    case OnlineItemAction::BeltPlace: case OnlineItemAction::BeltSwap:
        if (!cursor() || !baseNumber(wire, "belt")) return reject("Only an allowed cursor consumable can enter the belt");
        if (target) {
            if (target->mode != 2) return reject("Belt swap target is not in this player's belt");
            command.beltSlot = target->x;
        }
        if (command.beltSlot >= view_.beltSlots) return reject("Belt slot is outside MPQ belt capacity");
        if (!target)
            for (const auto &[id, item] : world.items)
                if (owned(item) && item.mode == 2 && item.x == command.beltSlot) return reject("Belt slot is occupied; use belt-swap");
        break;
    case OnlineItemAction::Stack:
        if (!owned(wire) || !(cursor() || backpack(wire)) || !(target->mode == 1 || backpack(*target)) ||
            wire.code != target->code || !baseNumber(wire, "stackable")) return reject("Stack requires compatible owned stackable items");
        if (targetData->quantity && *targetData->quantity >= unsigned(std::max(0, baseNumber(*target, "maxstack"))))
            return reject("Target stack is full");
        break;
    case OnlineItemAction::Book: {
        if (!owned(wire) || !(cursor() || backpack(wire)) || !backpack(*target)) return reject("Scroll/book transfer requires owned accessible items");
        const auto &books = tables_.at("books"); bool pair = false;
        for (size_t row = 0; row < books.rows().size(); ++row)
            pair |= books.value(row, "ScrollSpellCode") == wire.code && books.value(row, "BookSpellCode") == target->code;
        if (!pair) return reject("Current MPQ has no matching scroll and tome pair");
        break;
    }
    case OnlineItemAction::Socket:
        if (!cursor() || !decoded.identified || !isType(wire, "sock") ||
            !(backpack(*target) || target->mode == 1) || !targetData->identified || targetData->sockets <= targetData->filledSockets)
            return reject("Socket requires an identified cursor filler and a known empty socket in an owned item");
        break;
    case OnlineItemAction::Identify: {
        if (!backpack(wire) || view_.cursor || targetData->identified || !(backpack(*target) || target->mode == 1))
            return reject("Identify requires an empty cursor, a backpack source and an unidentified backpack or equipped target");
        const auto &books = tables_.at("books"); bool source = false;
        for (size_t row = 0; row < books.rows().size(); ++row)
            if ((books.value(row, "ScrollSpellCode") == wire.code || books.value(row, "BookSpellCode") == wire.code) &&
                books.value(row, "ScrollSkill") == "Scroll of Identify") source = true;
        if (!source || (decoded.quantity && !*decoded.quantity)) return reject("No available original identify scroll or tome");
        break;
    }
    case OnlineItemAction::SwitchWeapons: break;
    case OnlineItemAction::CubeOpen:
        if (!backpack(wire) || view_.cursor || !isType(wire, "ques") || baseNumber(wire, "pSpell") != 7)
            return reject("Open cube requires this player's original usable backpack cube and empty cursor");
        break;
    case OnlineItemAction::Buy:
        if (wire.action != 11 || wire.ownerType != 1 || wire.owner != command.npc || wire.mode != 0)
            return reject("Purchase requires an assigned item on the active server vendor shelf");
        if (decoded.gamble != command.gamble) return reject("Item identity belongs to a different vendor service");
        if(command.multibuy && command.gamble) return reject("Native gambling does not support multibuy");
        break;
    case OnlineItemAction::Sell:
        if (!owned(wire) || !(backpack(wire) || wire.mode == 1 || wire.mode == 4) || baseNumber(wire, "quest") || (wire.flags & 0x1000u))
            return reject("Sell requires an owned non-quest backpack, equipped or cursor item");
        break;
    case OnlineItemAction::Repair: {
        const auto charged = std::find_if(statRows_.begin(), statRows_.end(), [&](const auto &entry) {
            return tables_.at("itemstatcost").value(entry.second, "Stat") == "item_charged_skill";
        });
        bool missingCharges = false;
        auto charges = [&](const auto &stats) {
            for (const auto &stat : stats) {
                if (charged == statRows_.end() || stat.id != charged->first) continue;
                if (stat.value < 0 || stat.value > 65535) return false;
                const unsigned current = unsigned(stat.value) & 0xff, maximum = unsigned(stat.value) >> 8;
                if (current > maximum) return false;
                missingCharges |= current < maximum;
            }
            return true;
        };
        if (!charges(decoded.stats) || !charges(decoded.runewordStats)) return reject("Charged skill counts are invalid");
        if (!owned(wire) || !(backpack(wire) || wire.mode == 1) || !decoded.identified || (wire.flags & 0x400000) ||
            (bases_.at(wire.code).table == "misc" && !missingCharges)) return reject("Repair requires owned non-ethereal equipment or depleted charged skills");
        break;
    }
    case OnlineItemAction::StorageClose: case OnlineItemAction::Transmute:
    case OnlineItemAction::GoldDeposit: case OnlineItemAction::GoldWithdraw: case OnlineItemAction::GoldDrop:
    case OnlineItemAction::TradeOpen: case OnlineItemAction::RepairAll: case OnlineItemAction::IdentifyAll: case OnlineItemAction::QuestService:
        return reject("Invalid item command branch");
    }
    if (command.action == OnlineItemAction::Equip && bodyItem(command.body)) {
        command.target = bodyItem(command.body)->id; command.targetRevision = bodyItem(command.body)->revision;
    }
    if (command.action == OnlineItemAction::Equip && (command.body == 4 || command.body == 5)) {
        const auto opposite = bodyItem(command.body == 4 ? 5 : 4);
        const bool twoHanded = baseNumber(wire, "2handed") && !baseNumber(wire, "1or2handed");
        // D2MOO ItemMode::sub_6FC43280: matching bow/quiver hands are
        // compatible even though the bow itself occupies both weapon hands.
        const bool ammunitionPair = opposite &&
            ((isType(wire, "bow") && isType(*opposite, "bowq")) ||
             (isType(wire, "bowq") && isType(*opposite, "bow")) ||
             (isType(wire, "xbow") && isType(*opposite, "xboq")) ||
             (isType(wire, "xboq") && isType(*opposite, "xbow")));
        if (opposite && !ammunitionPair && (twoHanded || (baseNumber(*opposite, "2handed") && !baseNumber(*opposite, "1or2handed")))) {
            command.equipVariant = bodyItem(command.body) && twoHanded ? 2 : 1;
            if (!command.targetRevision) { command.target = opposite->id; command.targetRevision = opposite->revision; }
        }
    }
    if (!session.submit_item(command)) return reject(online.error ? online.error->message : "Item command is rate limited");
    reason_.clear(); return true;
}
} // namespace d2x
