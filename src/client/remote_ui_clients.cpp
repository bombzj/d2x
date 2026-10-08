#include "gameplay/skills/amazon_passive_spec.hpp"
#include "remote_ui_clients.hpp"
#include "client/character_projection.hpp"
#include "client/quest_projection.hpp"
#include "client/remote_inventory.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "content/classic_data.hpp"
#include "content/character/character_progression.hpp"
#include "content/items/item_display.hpp"
#include "content/items/item_pricing.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/character/attributes.hpp"
#include "contracts/online_scene.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/items/gold_limits.hpp"
#include "gameplay/items/quality.hpp"
#include "network/realm_session.hpp"
#include <algorithm>
#include <deque>
#include <cmath>
#include <chrono>
#include <set>
#include <utility>
#include <tuple>

namespace d2x {
namespace {
// Containers and wire entities occupy disjoint namespaces. Native GUID zero is valid.
EntityId itemId(uint32_t id) { return {uint64_t(id) + 1}; }
uint32_t guid(EntityId id) { return uint32_t(id.value - 1); }
EntityId npcId(uint32_t id) { return {(uint64_t{1} << 32) + id + 1}; }
uint32_t npcGuid(EntityId id) { return uint32_t(id.value - (uint64_t{1} << 32) - 1); }
EntityId containerId(unsigned index) { return {(uint64_t{2} << 32) + index}; }
uint8_t nativeBody(EquipmentSlot slot) {
    if (slot == EquipmentSlot::AlternateRightHand) return 4;
    if (slot == EquipmentSlot::AlternateLeftHand) return 5;
    return uint8_t(slot) + 1;
}
}
struct RemoteUiClients::Impl {
    net::RealmSession &session;
    RemoteInventory &items;
    RemoteCombat &combat;
    RemoteControl &control;
    ClassicData data;
    const OnlineSceneView *scene{};
    OnlineIntentContext context;
    uint64_t revision{}, generation{~uint64_t{}}, areaGeneration{~uint64_t{}};
    bool run{true};
    std::string notice;
    std::deque<std::string> itemNotices;
    struct TransferFeedback {
        OnlineItemAction action{};
        uint64_t sequence{};
        InventoryItemView source;
        std::map<EntityId, unsigned> quantities;
        unsigned gold{}, reported{};
        bool goldKnown{}, isGold{};
        std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::time_point::max();
    };
    std::optional<TransferFeedback> transferFeedback;
    uint64_t feedbackSequence{};
    InventoryView inventoryView;
    CharacterView characterView;
    QuestView questView;
    NpcConversationView npcView;
    NpcSceneView npcScene;
    ShopView shopView;
    HirelingView hirelingView;
    HirelingListView hirelingList;
    MapSceneView mapView;
    mutable TravelMenuView travelView;
    std::array<std::optional<SkillHotkey>, 8> hotkeys{}; // Local preferences sent without a native ACK.
    std::deque<OnlineItemCommand> transactions;
    std::optional<uint64_t> waiting;
    std::optional<OnlineIntentContext> waitingContext;
    std::chrono::steady_clock::time_point waitingUntil;
    using EquipmentStatKey=std::tuple<uint64_t,uint64_t,uint64_t,uint64_t,int,unsigned,bool>;
    struct CachedEquipmentStat { EquipmentStatKey key; std::optional<int> value; };
    mutable std::map<std::string,CachedEquipmentStat,std::less<>> equipmentStats;

    struct Actor final : IActorClient {
        Impl &o; explicit Actor(Impl &owner) : o(owner) {}
        ActorView controlledActor() const override {
            ActorView v; v.id = o.characterView.actor; v.region = o.mapView.region;
            v.dead = o.characterView.dead; v.position = o.mapView.observer;
            v.lightRadius = int(std::clamp(int64_t(CharacterAttributes{}.lightRadius) +
                int64_t(o.stat("item_lightradius").value_or(0)), int64_t(1), int64_t(18)));
            return v;
        }
        bool control(ActorControlIntent intent) override {
            if (intent.action == ActorControlIntent::Action::Interact) {
                if (!intent.target) return false;
                const auto raw = intent.target.value - 1;
                const OnlineUnitKey key{uint8_t(raw >> 32), uint32_t(raw)};
                const bool accepted = o.control.interact(key, o.run || intent.forceRun, o.context, intent.displayOrigin);
                if (!accepted) o.notice = o.control.reason();
                return accepted;
            }
            o.control.cancelMovement();
            if (intent.action == ActorControlIntent::Action::Pickup) {
                if (!intent.item) return false;
                OnlineItemCommand request; request.action = OnlineItemAction::Pickup;
                request.item = guid(intent.item->id); request.itemRevision = intent.item->revision;
                request.toCursor = intent.toCursor; request.context = o.context;
                const bool accepted = o.items.submit(o.session, request);
                if (!accepted) o.notice = o.items.reason();
                return accepted;
            }
            OnlineCombatCommand request; request.action = OnlineCombatCommand::Action::Cast;
            request.hand = intent.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
            request.stationary = intent.stationary; request.repeat = intent.repeat;
            request.context = o.context;
            if (intent.target) {
                const auto raw = intent.target.value - 1;
                request.target = OnlineUnitKey{uint8_t(raw >> 32), uint32_t(raw)};
            } else if (std::isfinite(intent.point.x) && std::isfinite(intent.point.y) &&
                intent.point.x >= 0 && intent.point.y >= 0 && intent.point.x <= UINT16_MAX && intent.point.y <= UINT16_MAX)
                request.point = OnlinePoint{uint16_t(intent.point.x), uint16_t(intent.point.y)};
            else return false;
            const bool accepted = o.combat.submit(request);
            if (!accepted) o.notice = o.combat.reason();
            return accepted;
        }
        bool move(MoveIntent intent) override {
            if (!std::isfinite(intent.destination.x) || !std::isfinite(intent.destination.y) ||
                intent.destination.x < 0 || intent.destination.y < 0 ||
                intent.destination.x > UINT16_MAX || intent.destination.y > UINT16_MAX) return false;
            const bool accepted = o.control.move({uint16_t(intent.destination.x), uint16_t(intent.destination.y)},
                o.run || intent.forceRun, o.context, intent.displayOrigin);
            if (!accepted) o.notice = o.control.reason();
            return accepted;
        }
        void stopMoving() override { o.control.cancelMovement(); }
        void stopActions() override {
            o.control.cancelMovement();
            OnlineCombatCommand c; c.action = OnlineCombatCommand::Action::Stop;
            c.context = o.context;
            o.combat.submit(c);
        }
        void toggleRun() override { o.run = !o.run; }
        void respawn() override {
            o.control.cancelMovement();
            if (!o.session.resurrect(o.context)) o.notice = o.session.read().error
                ? o.session.read().error->message : "Waiting for the server resurrection response.";
        }
    } actor{*this};
    struct Inventory final : IInventoryClient {
        Impl &o; explicit Inventory(Impl &owner) : o(owner) {}
        const InventoryView &read() const override { return o.inventoryView; }
        InventoryError preview(const InventoryIntent &intent) const override {
            if (o.inventoryView.dead) return InventoryError::AccessDenied;
            return std::visit([&](const auto &c) {
                using T = std::decay_t<decltype(c)>;
                if (o.inventoryView.ownTrade) {
                    const auto &trade = o.session.read().world.playerTrade;
                    if (trade.ownAgreed || trade.response != OnlinePlayerTrade::Response::None)
                        return InventoryError::AccessDenied;
                    if constexpr (!std::is_same_v<T, MoveItem> && !std::is_same_v<T, TransferItem> && !std::is_same_v<T, SwapItems>)
                        return InventoryError::AccessDenied;
                    if constexpr (requires { c.item; }) {
                        const auto *item = o.inventoryView.item(c.item.id);
                        const auto *where = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                        if (!where || (where->container != o.inventoryView.containers.backpack &&
                            where->container != o.inventoryView.ownTrade && where->container != o.inventoryView.containers.cursor))
                            return InventoryError::AccessDenied;
                    }
                    auto writable = [&](EntityId container) {
                        return container == o.inventoryView.ownTrade || container == o.inventoryView.containers.backpack ||
                            container == o.inventoryView.containers.cursor;
                    };
                    if constexpr (std::is_same_v<T, MoveItem>) {
                        const bool allowed = std::visit([&](const auto &destination) {
                            if constexpr (requires { destination.container; }) return writable(destination.container);
                            else return false;
                        }, c.destination);
                        if (!allowed) return InventoryError::AccessDenied;
                    } else if constexpr (std::is_same_v<T, TransferItem>) {
                        if (!writable(c.destination)) return InventoryError::AccessDenied;
                    } else if constexpr (std::is_same_v<T, SwapItems>) {
                        for (const auto handle : {c.first,c.second}) {
                            const auto *item = o.inventoryView.item(handle.id);
                            const auto *loc = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                            if (!loc || !writable(loc->container)) return InventoryError::AccessDenied;
                            if (item->revision != handle.revision) return InventoryError::SourceChanged;
                        }
                    }
                }
                if constexpr (std::is_same_v<T, SplitStack> || std::is_same_v<T, EquipHirelingItem>)
                    return InventoryError::InvalidRequest;
                else if constexpr (requires { c.item; }) {
                    const auto *item = o.inventoryView.item(c.item.id);
                    return !item ? InventoryError::UnknownItem : item->revision != c.item.revision
                        ? InventoryError::SourceChanged : InventoryError::None;
                } else return InventoryError::None;
            }, intent);
        }
        void submit(InventoryIntent c) override { o.inventoryCommand(std::move(c)); }
        std::optional<Cell> beltSpace(std::string_view code) const override {
            const auto belt = o.inventoryView.containers.belt;
            const auto *grid = o.inventoryView.container(belt);
            if (!grid) return {};
            for (int x = 0; x < grid->columns; ++x) {
                bool compatible = true;
                for (int y = 0; y < grid->rows; ++y)
                    if (const auto *i = o.inventoryView.item(o.inventoryView.itemAt(belt, {x,y})))
                        compatible &= i->definition == code;
                if (!compatible) continue;
                for (int y = 0; y < grid->rows; ++y)
                    if (!o.inventoryView.itemAt(belt, {x,y})) return Cell{x,y};
            }
            return {};
        }
    } inventory{*this};
    struct Character final : ICharacterClient {
        Impl &o; explicit Character(Impl &owner) : o(owner) {}
        const CharacterView &read() const override { return o.characterView; }
        void submit(CharacterIntent c) override {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                OnlineCombatCommand request;
                if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                    if (v.index >= o.hotkeys.size() || v.skill < -1) return;
                    request.action = OnlineCombatCommand::Action::BindHotkey;
                    request.hotkeySlot = uint8_t(v.index); request.skill = uint16_t(std::max(0,v.skill));
                    request.hand = v.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
                    request.context = o.context;
                    if (o.combat.submit(request)) o.hotkeys[v.index] = SkillHotkey{v.skill,v.right};
                    else o.notice = o.combat.reason();
                    return;
                } else if constexpr (std::is_same_v<T, AllocateAttribute>) {
                    request.action = OnlineCombatCommand::Action::SpendAttribute;
                    // Attribute intent order STR/DEX/VIT/ENE; native stat order STR/ENE/DEX/VIT.
                    constexpr uint8_t ids[]{0,2,3,1}; request.attribute = ids[size_t(v.attribute)];
                } else if constexpr (std::is_same_v<T, AllocateSkill>) {
                    request.action = OnlineCombatCommand::Action::LearnSkill; request.skill = uint16_t(v.id);
                } else {
                    request.action = OnlineCombatCommand::Action::SelectSkill;
                    request.skill = uint16_t(std::max(0,v.skill));
                    request.hand = v.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
                }
                request.context = o.context;
                if (!o.combat.submit(request)) o.notice = o.combat.reason();
            }, c);
        }
    } character{*this};
    struct Quests final : IQuestClient {
        Impl &o; explicit Quests(Impl &owner) : o(owner) {}
        const QuestView &read() const override { return o.questView; }
    } quests{*this};
    struct Npc final : INpcClient {
        Impl &o; explicit Npc(Impl &owner) : o(owner) {}
        const NpcConversationView &read(EntityId) const override { return o.npcView; }
        const NpcSceneView &scene() const override { return o.npcScene; }
        const ShopView &shop(EntityId, bool) const override { return o.shopView; }
        const ShopOfferView *inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const override {
            if (npc != o.shopView.npc || gamble != o.shopView.gamble || !onlineInteractionMatches(o.context, o.session.read())) return nullptr;
            const auto it = std::find_if(o.shopView.offers.begin(), o.shopView.offers.end(),
                [&](const auto &v) { return v.slot == slot; });
            return it == o.shopView.offers.end() ? nullptr : &*it;
        }
        std::optional<unsigned> quote(EntityId npc, ItemHandle item, bool repair) const override {
            if (!npc || o.context.npc != npcGuid(npc) || !onlineInteractionMatches(o.context, o.session.read())) return {};
            if (repair && !item.id) return o.repairAllQuote();
            const auto *entry = o.inventoryView.item(item.id);
            if (!entry || entry->revision != item.revision || o.context.npc != npcGuid(npc)) return {};
            return o.itemQuote(guid(item.id), repair ? OnlineItemAction::Repair : OnlineItemAction::Sell);
        }
        bool canRequestSale(EntityId npc, ItemHandle item) const override {
            const auto *entry = o.inventoryView.item(item.id);
            const auto &online = o.session.read();
            if (!npc || !entry || entry->revision != item.revision || o.context.npc != npcGuid(npc) ||
                !onlineInteractionMatches(o.context, online)) return false;
            const auto wire = online.world.items.find(guid(item.id));
            const auto *definition = o.data.items.find(entry->definition);
            return wire != online.world.items.end() && definition && !definition->questTag &&
                !(wire->second.flags & 0x1000u) && wire->second.ownerType == 0 && wire->second.owner == online.load.playerUnitId &&
                ((wire->second.mode == 0 && wire->second.page == 1) || wire->second.mode == 1 || wire->second.mode == 4);
        }
        const HirelingView &hireling() const override { return o.hirelingView; }
        const HirelingListView &hirelings(EntityId) const override { return o.hirelingList; }
        void submit(NpcIntent c) override {
            if (!onlineInteractionMatches(o.context, o.session.read())) { o.notice = "NPC interaction changed; select it again."; return; }
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                OnlineItemCommand request;
                request.npc = o.context.npc.value_or(0);
                if constexpr (std::is_same_v<T, EndNpcConversation>) { o.session.close_npc(o.context); return; }
                else if constexpr (std::is_same_v<T, TalkToNpc>) {
                    if (v.action == TalkToNpc::Action::Acknowledge && v.message && *v.message <= UINT16_MAX) {
                        const auto &conversation = o.session.read().world.npcConversation;
                        if (conversation && !conversation->acknowledged.contains(uint16_t(*v.message)))
                            o.session.acknowledge_npc_message(uint16_t(*v.message), o.context);
                    } else if (v.action == TalkToNpc::Action::Trade) {
                        request.action = OnlineItemAction::TradeOpen; o.enqueue(request);
                    }
                    return;
                }
                else if constexpr (std::is_same_v<T, CompleteActOne> || std::is_same_v<T, CompleteActTwo>) { o.session.npc_travel(o.context); return; }
                else if constexpr (std::is_same_v<T, IdentifyWithCain>) request.action = OnlineItemAction::IdentifyAll;
                else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    request.action = OnlineItemAction::Buy; request.item = v.slot;
                    request.gamble = v.gamble;request.multibuy=v.multibuy;
                } else if constexpr (std::is_same_v<T, OpenGamble>) {
                    request.action = OnlineItemAction::TradeOpen; request.gamble = true;
                } else if constexpr (std::is_same_v<T, SellVendorItem>) {
                    request.action = OnlineItemAction::Sell; request.item = guid(v.item.id); request.itemRevision = v.item.revision;
                } else if constexpr (std::is_same_v<T, RepairVendorItem>) {
                    request.action = v.item.id ? OnlineItemAction::Repair : OnlineItemAction::RepairAll;
                    if (v.item.id) { request.item = guid(v.item.id); request.itemRevision = v.item.revision; }
                } else { o.notice = "This native NPC service is unavailable."; return; }
                o.enqueue(request);
            }, c);
        }
    } npc{*this};
    struct MapClient final : IMapClient {
        Impl &o; explicit MapClient(Impl &owner) : o(owner) {}
        const MapSceneView &read() const override { return o.mapView; }
        const TravelMenuView &travel(EntityId source, int act) const override {
            auto &v = o.travelView; v = {}; v.revision = o.revision; v.source = source; v.act = act;
            if (o.scene) for (const auto &d : o.scene->waypoints) if (d.act == act)
                v.entries.push_back({d.level,o.data.itemStrings.contains(d.name) ? o.data.itemStrings.at(d.name) : d.name,d.unlocked ? "" : "Not activated",
                    d.unlocked ? std::optional<RegionId>{RegionId(d.level)} : std::nullopt});
            return v;
        }
        bool waypointSource(EntityId source) const override {
            return o.session.read().world.waypointSource == npcGuid(source);
        }
        void closeTravel() override { if (onlineInteractionMatches(o.context, o.session.read())) o.session.use_waypoint(0, 0, o.context); }
        void submit(MapIntent intent) override {
            if (!onlineInteractionMatches(o.context, o.session.read())) { o.notice = "Travel interaction changed; select it again."; return; }
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, WaypointTravel>)
                    if (o.scene) for (const auto &d : o.scene->waypoints)
                        if (d.level == uint16_t(v.destination) && d.unlocked) { o.session.use_waypoint(d.level,d.number,o.context); return; }
            }, intent);
        }
    } map{*this};

    Impl(Archives &a, net::RealmSession &s, RemoteInventory &i, RemoteCombat &c, RemoteControl &r)
        : session(s), items(i), combat(c), control(r), data(loadClassicData(a)) {}
    std::optional<double> stat(std::string_view name) const {
        const auto &costs = data.tables.at("itemstatcost");
        for (size_t row = 0; row < costs.rows().size(); ++row) if (costs.value(row,"Stat") == name) {
            const auto id = costs.number(row,"ID");
            if (!id || *id < 0 || *id > 255) return {};
            const auto it = session.read().world.playerAttributes.find(uint8_t(*id));
            if (it == session.read().world.playerAttributes.end()) return {};
            const int shift = costs.number(row,"ValShift").value_or(0);
            const double value = costs.number(row,"Signed").value_or(0) ? double(int32_t(it->second)) : double(it->second);
            return value / std::ldexp(1.,shift);
        }
        return {};
    }
    void queue(OnlineItemCommand c) {
        if (!c.context) c.context = context;
        transactions.push_back(std::move(c));
    }
    void enqueue(OnlineItemCommand c) {
        if (waiting || !transactions.empty()) { notice = "Waiting for the previous server item response."; return; }
        queue(std::move(c)); pump();
    }
    void pump() {
        const auto &online = session.read();
        const auto &w = online.world;
        if ((waitingContext && !onlineInteractionMatches(*waitingContext, online)) ||
            (!transactions.empty() && (!transactions.front().context ||
                !onlineInteractionMatches(*transactions.front().context, online)))) {
            notice = "Queued item operation cancelled after the game, area or interaction changed.";
            transactions.clear(); waiting.reset(); waitingContext.reset(); return;
        }
        if (waiting) {
            if (!w.itemRequest || w.itemRequest->sequence != *waiting) { transactions.clear(); waiting.reset(); waitingContext.reset(); return; }
            const auto status = w.itemRequest->state;
            if (status == OnlineItemRequest::State::Pending) return;
            if (status == OnlineItemRequest::State::Rejected || status == OnlineItemRequest::State::TimedOut || status == OnlineItemRequest::State::Interrupted) {
                notice = "Server item request rejected, timed out or interrupted."; transactions.clear();
            }
            // Multi-step operations continue only after the server actually assigns the cursor.
            if (!transactions.empty() && (!items.read().cursor || *items.read().cursor != transactions.front().item)) {
                if (status == OnlineItemRequest::State::Updated && std::chrono::steady_clock::now() < waitingUntil) return;
                notice = "Server did not assign the requested cursor item."; transactions.clear();
            }
            waiting.reset(); waitingContext.reset();
        }
        if (transactions.empty()) return;
        if (!session.item_request_ready()) return;
        auto c = transactions.front(); transactions.pop_front();
        if (c.action == OnlineItemAction::Buy || c.action == OnlineItemAction::Sell) {
            const auto found = session.read().world.items.find(c.item);
            if (found == session.read().world.items.end() || (c.itemRevision && c.itemRevision != found->second.revision)) {
                notice = "The quoted item changed before submission."; transactions.clear(); return;
            }
            c.itemRevision = found->second.revision;
            const auto price = itemQuote(c.item, c.action);
            if (!price) { notice = "The current server item has no complete transaction quote."; transactions.clear(); return; }
            c.amount = *price;
        }
        if (!items.submit(session,c)) { notice = items.reason(); transactions.clear(); return; }
        const auto &sent = session.read();
        if (c.action == OnlineItemAction::TradeOpen) {
            shopView.gamble = sent.world.shopGamble;
            shopView.offers.clear();
            shopView.pricesKnown = false;
            ++shopView.revision;
        }
        if (sent.world.itemRequest) {
            waiting = sent.world.itemRequest->sequence;
            waitingContext = onlineIntentContext(sent);
            waitingUntil = std::chrono::steady_clock::now() + session.request_timeout();
        }
    }
    std::optional<Cell> freeCell(EntityId target, const InventoryItemView &item) const {
        const auto *grid = inventoryView.container(target);
        const auto *def = inventoryView.definition(item.definition);
        if (!grid || !def) return {};
        for (int y=0;y+def->height<=grid->rows;++y) for(int x=0;x+def->width<=grid->columns;++x) {
            bool free = true;
            for(int yy=y;yy<y+def->height;++yy) for(int xx=x;xx<x+def->width;++xx) {
                const auto occupant = inventoryView.itemAt(target,{xx,yy});
                free &= !occupant || occupant == item.id;
            }
            if (free) return Cell{x,y};
        }
        return {};
    }
    void moveItem(ItemHandle handle, ItemDestination destination) {
        const auto *item = inventoryView.item(handle.id);
        if (!item || item->revision != handle.revision) { notice = "Item changed; select it again."; return; }
        OnlineItemCommand next; next.item = guid(handle.id);
        const auto &owned = inventoryView.containers;
        if (std::holds_alternative<GroundLocation>(destination)) next.action = OnlineItemAction::Drop;
        else {
            ContainerLocation target;
            if (const auto *automatic = std::get_if<AutoPlace>(&destination)) {
                auto cell = freeCell(automatic->container,*item);
                if (!cell) { notice = "No room in that container."; return; }
                target = {automatic->container,*cell};
            } else target = std::get<ContainerLocation>(destination);
            if (target.container == owned.cursor) {
                next.action = OnlineItemAction::Take; next.itemRevision = handle.revision; enqueue(next); return;
            }
            next.x = uint8_t(target.cell.x); next.y = uint8_t(target.cell.y);
            if (target.container == owned.belt) {
                next.action = OnlineItemAction::BeltPlace; next.beltSlot = uint8_t(target.cell.y*4+target.cell.x);
            } else if (target.container == owned.equipment || target.container == owned.beltEquipment) {
                next.action = OnlineItemAction::Equip; next.body = target.container == owned.beltEquipment ? 8 : nativeBody(EquipmentSlot(target.cell.x));
            } else {
                next.action = OnlineItemAction::Place;
                if (target.container == inventoryView.peerTrade && inventoryView.peerTrade) {
                    notice = "The other player's offer is read-only."; return;
                }
                next.page = target.container == inventoryView.ownTrade && inventoryView.ownTrade ? 2 : target.container == owned.cube ? 3 : target.container == owned.stash ? 4 : 0;
            }
        }
        if (waiting || !transactions.empty()) { notice = "Waiting for the previous server item response."; return; }
        if (items.read().cursor != next.item) {
            OnlineItemCommand take; take.action = OnlineItemAction::Take; take.item = next.item; take.itemRevision = handle.revision;
            queue(std::move(take));
        } else next.itemRevision = handle.revision;
        queue(std::move(next)); pump();
    }
    void compositeTake(OnlineItemCommand next) {
        if (waiting || !transactions.empty()) { notice="Waiting for the previous server item response."; return; }
        OnlineItemCommand take; take.action=OnlineItemAction::Take; take.item=next.item; take.itemRevision=next.itemRevision;
        next.itemRevision=0;
        queue(std::move(take)); queue(std::move(next)); pump();
    }
    void inventoryCommand(InventoryIntent intent) {
        std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            const auto &owned = inventoryView.containers;
            OnlineItemCommand c;
            if constexpr (std::is_same_v<T, CloseStorage>) {
                if (!onlineWorldMatches(context, session.read())) { notice = "Storage belongs to a previous world."; return; }
                // Explicit close cancels unsent combinations. It has no native ACK
                // and must not leave a coordinator wait in front of a new cube open.
                transactions.clear(); waiting.reset(); waitingContext.reset();
                c.action = OnlineItemAction::StorageClose; c.context = onlineIntentContext(session.read());
                if (!items.submit(session, c)) notice = items.reason();
                context = onlineIntentContext(session.read());
                return;
            } else if constexpr (std::is_same_v<T, MoveItem>) { moveItem(v.item,v.destination); return; }
            else if constexpr (std::is_same_v<T, TransferItem>) { moveItem(v.item,AutoPlace{v.destination}); return; }
            else if constexpr (std::is_same_v<T, EquipItem> || std::is_same_v<T, EquipBelt>) {
                const auto *item = inventoryView.item(v.item.id); if (!item) return;
                const auto *location = std::get_if<ContainerLocation>(&item->location);
                if (location && (location->container == owned.equipment || location->container == owned.beltEquipment)) {
                    moveItem(v.item,v.destination.value_or(ItemDestination{AutoPlace{owned.backpack}})); return;
                }
                EquipmentSlot slot = EquipmentSlot::Belt;
                if constexpr (std::is_same_v<T, EquipItem>) { if (!v.slot) return; slot = *v.slot; }
                moveItem(v.item,ContainerLocation{owned.equipment,{int(slot),0}}); return;
            } else if constexpr (std::is_same_v<T, UseItem> || std::is_same_v<T, UseHirelingPotion>) {
                if constexpr (std::is_same_v<T, UseItem>) {
                    const auto *item = inventoryView.item(v.item.id);
                    if (item && item->revision == v.item.revision) {
                        const auto *definition = data.items.find(item->definition);
                        if (definition && definition->opensCube) {
                            if (!onlineWorldMatches(context, session.read())) { notice = "Cube belongs to a previous world."; return; }
                            c.action = OnlineItemAction::CubeOpen;
                            c.item = guid(v.item.id); c.itemRevision = v.item.revision;
                            c.context = onlineIntentContext(session.read());
                            enqueue(c); return;
                        }
                        const auto &books = data.tables.at("books");
                        for (size_t row=0; row<books.rows().size(); ++row)
                            if (books.value(row,"ScrollSkill") == "Scroll of Townportal" &&
                                (books.value(row,"ScrollSpellCode") == item->definition || books.value(row,"BookSpellCode") == item->definition)) {
                                if (!control.townPortal()) notice = control.reason();
                                return;
                            }
                    }
                }
                c.action = OnlineItemAction::Use; c.item = guid(v.item.id); c.itemRevision = v.item.revision;
                if constexpr (std::is_same_v<T, UseHirelingPotion>) c.mercenary = true;
            } else if constexpr (std::is_same_v<T, UseBeltColumn>) {
                for (int y=0;y<inventoryView.container(owned.belt)->rows;++y)
                    if (const auto *item = inventoryView.item(inventoryView.itemAt(owned.belt,{v.column,y}))) {
                        c.action = OnlineItemAction::Use; c.item = guid(item->id); c.itemRevision = item->revision; c.mercenary = v.hireling; break;
                    }
                if (c.action != OnlineItemAction::Use) return;
            } else if constexpr (std::is_same_v<T, SwapItems>) {
                c.item = guid(v.first.id); c.itemRevision = v.first.revision; c.target = guid(v.second.id); c.targetRevision = v.second.revision;
                const auto *target = inventoryView.item(v.second.id);
                const auto *loc = target ? std::get_if<ContainerLocation>(&target->location) : nullptr;
                c.action = loc && loc->container == owned.belt ? OnlineItemAction::BeltSwap : OnlineItemAction::Swap;
                if (loc) c.beltSlot = uint8_t(loc->cell.y*4+loc->cell.x);
                if (v.destination && (!loc || v.destination->container != loc->container)) {
                    notice = "Swap destination must belong to the covered item's container."; return;
                }
                if (loc) {
                    const auto cell = v.destination ? v.destination->cell : loc->cell;
                    if (cell.x < 0 || cell.y < 0 || cell.x > UINT8_MAX || cell.y > UINT8_MAX) {
                        notice = "Swap destination is outside the native grid coordinate range."; return;
                    }
                    c.x = uint8_t(cell.x); c.y = uint8_t(cell.y);
                }
                if (items.read().cursor != c.item) { compositeTake(c); return; }
            } else if constexpr (std::is_same_v<T, MergeStacks> || std::is_same_v<T, LoadBook> || std::is_same_v<T, SocketItem> || std::is_same_v<T, IdentifyItem>) {
                if constexpr (std::is_same_v<T, MergeStacks>) { c.action=OnlineItemAction::Stack; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
                if constexpr (std::is_same_v<T, LoadBook>) { c.action=OnlineItemAction::Book; c.item=guid(v.scroll.id); c.itemRevision=v.scroll.revision; c.target=guid(v.book.id); c.targetRevision=v.book.revision; }
                if constexpr (std::is_same_v<T, SocketItem>) { c.action=OnlineItemAction::Socket; c.item=guid(v.filler.id); c.itemRevision=v.filler.revision; c.target=guid(v.host.id); c.targetRevision=v.host.revision; }
                if constexpr (std::is_same_v<T, IdentifyItem>) { c.action=OnlineItemAction::Identify; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
            } else if constexpr (std::is_same_v<T, SwitchWeaponSet>) c.action = OnlineItemAction::SwitchWeapons;
            else if constexpr (std::is_same_v<T, TransmuteCube>) c.action = OnlineItemAction::Transmute;
            else if constexpr (std::is_same_v<T, GoldTransaction>) {
                c.action = v.action == GoldAction::Deposit ? OnlineItemAction::GoldDeposit : v.action == GoldAction::Withdraw ? OnlineItemAction::GoldWithdraw : OnlineItemAction::GoldDrop; c.amount = v.amount;
            } else { notice = "This native item operation is unavailable."; return; }
            if (c.action == OnlineItemAction::Socket && items.read().cursor != c.item) { compositeTake(c); return; }
            enqueue(c);
        }, intent);
    }
    // Original private item packets carry the local equipment stat lists. Native NOEQUIP/BROKEN
    // flags and active body slots decide contributions; no server flavor or hidden remote stats.
    std::optional<int> knownEquipmentStat(std::string_view name, int level) const {
        const auto &online=session.read(); const auto &world=online.world;
        if (!online.load.playerUnitId) return {};
        const auto actor=world.units.find({0,*online.load.playerUnitId});
        if (actor==world.units.end()) return {};
        const EquipmentStatKey key{online.gameGeneration,world.itemRevision,items.read().revision,
            actor->second.appearanceRevision,level,world.weaponSet,actor->second.equipmentObserved};
        if (const auto cached=equipmentStats.find(name);cached!=equipmentStats.end() && cached->second.key==key)
            return cached->second.value;
        const auto value=calculateKnownEquipmentStat(name,level);
        equipmentStats.insert_or_assign(std::string(name),CachedEquipmentStat{key,value});
        return value;
    }
    std::optional<int> calculateKnownEquipmentStat(std::string_view name, int level) const {
        const auto &online=session.read(); const auto &world=online.world;
        int64_t total=0;
        if (!online.load.playerUnitId) return {};
        const auto actor = world.units.find({0, *online.load.playerUnitId});
        if (actor == world.units.end() || !actor->second.equipmentObserved) return {};
        const auto statRow = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &entry) { return entry.name == name; });
        if (statRow == data.itemStats.end() || !statRow->id) return {};
        std::map<std::string, std::set<int32_t>, std::less<>> equippedSets;
        std::vector<ItemInstance> setPieces;
        for (const auto &[equippedId, equipped] : world.items) {
            if (equipped.ownerType != 0 || equipped.owner != online.load.playerUnitId) continue;
            if (equipped.flags & 0x4100u) continue;
            const auto *base = data.items.find(equipped.code);
            const bool active = (equipped.mode == 1 && equipped.body <= 10) || (equipped.mode == 0 && equipped.page == 1 && base && base->equipment.isType("char"));
            if (!active) continue;
            const auto equipment = items.read().items.find(equippedId);
            if (equipment == items.read().items.end() || !equipment->second.decoded || equipment->second.revision != equipped.revision) return {};
            const auto instance = completeItem(equipped, equipment->second, SocketLocation{{}, 0});
            if (!instance) return {};
            for (const auto &bonus : resolveItemStats(data, *instance, level))
                if (bonus.name == name) total += bonus.value;
            if (equipped.mode == 1 && instance->identified && instance->quality == ItemQuality::Set) {
                const auto record = std::find_if(data.setItems.begin(), data.setItems.end(),
                    [&](const auto &entry) { return int32_t(entry.row) == instance->specialRow; });
                if (record == data.setItems.end()) return {};
                equippedSets[record->set].insert(instance->specialRow);
                setPieces.push_back(*instance);
            }
        }
        auto hasStat = [&](const PropertyRange &property) {
            const auto definition = std::find_if(data.properties.begin(), data.properties.end(),
                [&](const auto &entry) { return entry.code == property.code; });
            return definition != data.properties.end() && std::any_of(definition->operations.begin(), definition->operations.end(),
                [&](const auto &operation) { return operation.stat == name; });
        };
        std::set<std::string, std::less<>> countedSets;
        for (const auto &piece : setPieces) {
            const auto record = std::find_if(data.setItems.begin(), data.setItems.end(),
                [&](const auto &entry) { return int32_t(entry.row) == piece.specialRow; });
            const auto &worn = equippedSets.at(record->set);
            const unsigned count = unsigned(worn.size());
            std::array<bool, 5> activeLayers{};
            if (record->setAddFunction == 2) {
                for (unsigned layer = 0; layer < activeLayers.size(); ++layer) activeLayers[layer] = count > layer + 1;
            } else if (record->setAddFunction == 1) {
                unsigned layer = 0;
                for (const auto &other : data.setItems) {
                    if (other.set != record->set || other.row == record->row) continue;
                    if (layer >= activeLayers.size()) return {};
                    activeLayers[layer++] = worn.contains(int32_t(other.row));
                }
            } else if (record->setAddFunction) return {};
            for (unsigned layer = 0; layer < activeLayers.size(); ++layer) {
                if (!activeLayers[layer]) continue;
                bool assigned = false;
                for (const auto &bonus : piece.savedSetStats[layer]) {
                    if (bonus.id != *statRow->id || bonus.parameter) continue;
                    total += bonus.value;
                    assigned = true;
                }
                if (!assigned && std::any_of(record->setBonuses.begin(), record->setBonuses.end(), [&](const auto &bonus) {
                    return bonus.perItem && bonus.pieces == int(layer + 2) && hasStat(bonus.property);
                })) return {};
            }
            if (!countedSets.insert(record->set).second) continue;
            const unsigned fullCount = unsigned(std::count_if(data.setItems.begin(), data.setItems.end(),
                [&](const auto &entry) { return entry.set == record->set; }));
            for (const auto &bonus : record->setBonuses) {
                if (bonus.perItem || (bonus.pieces ? count < unsigned(bonus.pieces) : count != fullCount) || !hasStat(bonus.property)) continue;
                const auto &property = bonus.property;
                if (property.directRoll && property.minimum != property.maximum) return {};
                for (const auto &value : resolvePropertyStats(data, property, property.minimum.value_or(0), level, int(piece.level)))
                    if (value.name == name) total += value.value;
            }
        }
        if (total<INT32_MIN || total>INT32_MAX) return {};
        return int(total);
    }
    std::optional<int> knownSelfEquipmentStat(std::string_view name,int level) const {
        if(name=="item_fastercastrate") if (const auto native=stat(name)) return int(*native);
        auto total=knownEquipmentStat(name,level);
        const auto &online=session.read();
        if (!total || !online.load.playerUnitId) return {};
        const OnlineUnitKey key{0,*online.load.playerUnitId};
        const auto unit=online.world.units.find(key);
        if (unit==online.world.units.end() || !unit->second.stateSequence) return {};
        const auto states=combat.states().find(key);
        if (states==combat.states().end() || !states->second.decoded || states->second.sequence!=unit->second.stateSequence) return {};
        const auto &costs=data.tables.at("itemstatcost");
        for (size_t row=0;row<costs.rows().size();++row) {
            if (costs.value(row,"Stat")!=name) continue;
            const auto id=costs.number(row,"ID"); if (!id) return {};
            int64_t value=*total;
            for (const auto &[state,list]:states->second.states) {
                (void)state;
                for (const auto &entry:list.stats) if (entry.id==*id && !entry.parameter) value+=entry.value;
            }
            if (value<INT32_MIN || value>INT32_MAX) return {};
            return int(value);
        }
        return {};
    }
    void projectCharacter() {
        const auto &online = session.read(); const auto &w = online.world;
        CharacterProjectionInput input;
        input.revision = revision; input.name = online.selectedCharacter;
        input.actor = online.load.playerUnitId ? itemId(*online.load.playerUnitId) : EntityId{};
        if (online.load.playerUnitId)
            if (const auto it = w.units.find({0, *online.load.playerUnitId}); it != w.units.end() && it->second.classId)
                input.characterClass = *it->second.classId;
        const auto &costs = data.tables.at("itemstatcost");
        for (size_t row = 0; row < costs.rows().size(); ++row) {
            const auto name = costs.value(row, "Stat");
            if (auto value = stat(name)) input.stats.emplace(std::string(name), *value);
        }
        input.life = w.life; input.mana = w.mana; input.stamina = w.stamina;
        input.dead = onlinePlayerDead(w); input.running = run; input.town = scene && scene->town;
        input.aliveAfterDeathSave = w.life && !*w.life && w.deathPhase == OnlineDeathPhase::Alive;
        input.weaponSet = w.weaponSet;
        const unsigned weaponSet = std::min(w.weaponSet, 1u);
        input.selectedSkills[weaponSet * 2] = w.leftSkill ? int(w.leftSkill->skill) : -1;
        input.selectedSkills[weaponSet * 2 + 1] = w.rightSkill ? int(w.rightSkill->skill) : -1;
        for (size_t slot = 0; slot < input.hotkeys.size(); ++slot) {
            if (hotkeys[slot]) input.hotkeys[slot] = *hotkeys[slot];
            else if (const auto &native = w.skillHotkeys[slot]; native && native->selection && native->selection->owner == UINT32_MAX)
                input.hotkeys[slot] = {native->selection->skill ? int(native->selection->skill) : -1,
                    native->hand == OnlineSkillHand::Right};
        }
        input.baseRanks.insert(w.playerBaseSkills.begin(), w.playerBaseSkills.end());
        input.effectiveRanks.insert(w.playerSkills.begin(), w.playerSkills.end());
        input.baseRanksAssigned = w.playerBaseSkillsAssigned;
        input.difficulty = online.load.difficulty;
        if(const auto level=stat("level")) {
            if(const auto itemPierce=knownSelfEquipmentStat("item_pierce",int(*level));itemPierce && w.playerBaseSkillsAssigned) {
                int chance=*itemPierce;
                for(const auto &[skillId,record]:data.skills.skills) if(record.passiveContribution.amazon && record.passiveContribution.amazon->stat==AmazonPassiveStat::Pierce) {
                    const auto rank=w.playerSkills.find(uint16_t(skillId));if(rank!=w.playerSkills.end()) chance+=amazonPassiveValue(*record.passiveContribution.amazon,rank->second);
                }
                input.missilePierceChance=chance;
            }
            const auto ias=knownSelfEquipmentStat("item_fasterattackrate",int(*level));
            const auto rate=knownSelfEquipmentStat("attackrate",int(*level));
            if(ias && rate && *ias>-120) for(const auto &[id,item]:w.items) {
                (void)id;
                if(item.ownerType!=0 || item.owner!=online.load.playerUnitId || item.mode!=1 || item.body!=4 || (item.flags&0x4100u)) continue;
                const auto *base=data.items.find(item.code);
                if(base && base->base.speed) input.attackTiming=KnownAttackTiming{*ias,*base->base.speed,*rate};
                break;
            }
        }
        if (const auto value = stat("passive_fire_mastery")) input.fireMastery = int(*value);
        if (const auto value = stat("passive_ltng_mastery")) input.lightningMastery = int(*value);
        if (const auto value = stat("passive_cold_mastery")) input.coldDamagePercent = int(*value);
        for (size_t hand = 0; hand < input.throwReady.size(); ++hand) {
            const bool leftHand = hand != 0;
            for (uint8_t body : {uint8_t(leftHand ? 5 : 4), uint8_t(5)}) {
                bool found = false;
                for (const auto &[id, item] : w.items) {
                    if (item.mode != 1 || item.body != body || item.ownerType != 0 || item.owner != online.load.playerUnitId) continue;
                    found = true;
                    const auto *definition = data.items.find(item.code);
                    const auto decoded = items.read().items.find(id);
                    input.throwReady[hand] = definition && definition->family == ItemFamily::Weapon &&
                        definition->equipment.throwable && decoded != items.read().items.end() && decoded->second.decoded &&
                        decoded->second.quantity.value_or(0) > 0;
                    break;
                }
                if (found || leftHand) break;
            }
        }
        if (const auto level=stat("level"))
            if (const auto faster=knownSelfEquipmentStat("item_fastercastrate",int(*level))) input.stats.insert_or_assign("item_fastercastrate",*faster);
        characterView = projectCharacterDisplay(data, input);
    }
    ItemInstance itemInstance(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
        const auto *definition=data.items.find(native.code);
            ItemInstance item; item.id=itemId(native.id); item.revision=native.revision; item.definition=native.code; item.location=location;
            item.quantity=di.gold.value_or(di.quantity.value_or(1)); item.charges=definition->bookCapacity?item.quantity:0; item.durability=di.durability.value_or(0);
            item.quality=itemQualityFromNative(di.quality, true).value(); item.identified=di.identified; item.level=di.level; item.defense=int(di.defense.value_or(0));
            item.nativeProperties=true; item.nativeFlags=native.flags; item.nativeMaxDurability=di.maxDurability.value_or(0);
            item.nativeFormat=di.format; item.nativeGraphic=di.graphic; item.nativeHasGraphic=di.hasGraphic; item.personalizedName=di.personalizedName;
            item.nativeAutoAffix=di.autoAffix;
            if(di.autoAffix) for(const auto &record:data.autoMagic) if(record.row==unsigned(di.autoAffix-1))
                item.requiredLevel=std::max(item.requiredLevel,record.requiredLevel);
            item.sockets=di.sockets; item.runewordRow=-1;
            for (const auto &word:data.runewords) if (word.stringId==di.runeword) item.runewordRow=word.row;
            // Unidentified native special items omit their file index. Zero is
            // not evidence for the first UniqueItems/SetItems row.
            if(di.identified && (item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique)) item.specialRow=di.fileIndex;
            if(item.quality==ItemQuality::Superior || item.quality==ItemQuality::Inferior) item.gradeRow=di.fileIndex;
            if (item.specialRow>=0) {
                const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
                for (const auto &record:records) if(int32_t(record.row)==item.specialRow) item.requiredLevel=record.requiredLevel;
            }
            for (bool prefix:{true,false}) for (auto index:prefix?di.prefixes:di.suffixes) {
                if (!index) continue;
                const auto &records=prefix?data.magicPrefixes:data.magicSuffixes;
                for (const auto &record:records) if(record.row==unsigned(index-1)) {
                    item.affixes.push_back({prefix,int32_t(index-1),{}});
                    item.requiredLevel=std::max(item.requiredLevel,record.requiredLevel);
                }
            }
            if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Tempered) {
                item.rarePrefixRow=int(di.rarePrefix)-int(data.tables.at("raresuffix").rows().size())-1;
                item.rareSuffixRow=int(di.rareSuffix)-1;
            }
            for(const auto &s:di.stats) item.savedStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(const auto &s:di.runewordStats) item.runewordStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(size_t index=0;index<di.setStats.size();++index) for(const auto &s:di.setStats[index]) item.savedSetStats[index].push_back({s.id,int(s.parameter),int(s.value)});
            return item;
    }
    std::optional<ItemInstance> completeItem(const OnlineItem &native, const OnlineDecodedItem &detail, ItemLocation location) const {
        auto item = itemInstance(native, detail, location);
        const auto &world = session.read().world;
        std::vector<const OnlineItem *> children;
        for (const auto &[id, child] : world.items) {
            if (child.ownerType != 4 || child.owner != native.id) continue;
            children.push_back(&child);
        }
        std::sort(children.begin(), children.end(), [](const auto *first, const auto *second) { return first->socketAssignmentRevision < second->socketAssignmentRevision; });
        for (const auto *entry : children) {
            const auto &child = *entry;
            const auto id = child.id;
            const auto decoded = items.read().items.find(id);
            if (!child.socketAssignmentRevision || child.mode != 6 || decoded == items.read().items.end() || !decoded->second.decoded ||
                decoded->second.revision != child.revision || !data.items.find(child.code)) return {};
            item.socketedItems.push_back(itemInstance(child, decoded->second, SocketLocation{item.id, unsigned(item.socketedItems.size())}));
        }
        if (item.socketedItems.size() != detail.filledSockets) return {};
        const auto *host = data.items.find(native.code);
        for (const auto &child : item.socketedItems) {
            const auto gem = data.socketGems.find(child.definition);
            if (gem == data.socketGems.end() || !child.savedStats.empty()) continue;
            if (!host || host->gemApplyType < 0 || host->gemApplyType > 2) return {};
            for (const auto &property : gem->second.properties[size_t(host->gemApplyType)])
                if (property.directRoll && property.minimum != property.maximum) return {};
        }
        return item;
    }
    std::optional<unsigned> itemQuote(uint32_t id, OnlineItemAction action) const {
        const auto &online = session.read();
        const auto &world = online.world;
        const bool repair = action == OnlineItemAction::Repair;
        const bool sale = action == OnlineItemAction::Sell;
        if ((!repair && !sale && action != OnlineItemAction::Buy) ||
            !onlineInteractionMatches(context, online) || !world.npcConversation ||
            world.npcRequested != world.npcConversation->source || !online.load.difficulty) return {};
        const auto vendor = data.vendors.find(npcIdentity());
        const auto wire = world.items.find(id);
        const auto decoded = items.read().items.find(id);
        if (wire == world.items.end() || decoded == items.read().items.end() ||
            !decoded->second.decoded || decoded->second.revision != wire->second.revision) return {};
        const auto &native = wire->second;
        const auto &detail = decoded->second;
        if (!detail.gamble && vendor == data.vendors.end()) return {};
        const auto *definition = data.items.find(native.code);
        if (!definition || (detail.gamble && (sale || repair || !world.shopGamble))) return {};
        if (sale || repair) {
            if (native.ownerType != 0 || native.owner != online.load.playerUnitId ||
                !((native.mode == 0 && native.page == 1) || native.mode == 1 || (sale && native.mode == 4))) return {};
            if (sale && (definition->questTag || (native.flags & 0x1000u))) return {};
            if (repair && !shopView.repairAvailable) return {};
        } else if (world.shopSource != world.npcRequested || native.ownerType != 1 ||
            native.owner != world.shopSource || native.action != 11 || native.mode != 0) return {};
        if (!repair && (native.flags & 0x20000u)) return 1;
        if (detail.gamble && !detail.format) return itemGamblePrice(data, native.code, 0, detail.format, 0);
        std::optional<int64_t> bodyCost;
        if (native.flags & 0x10000u) {
            if (!definition->base.cost) return {};
            bodyCost = int64_t(*definition->base.cost) * detail.earLevel;
        } else if (definition->equipment.isType("body")) {
            const auto &monsters = data.tables.at("monstats");
            if (!definition->base.cost || detail.fileIndex >= monsters.rows().size()) return {};
            const auto column = *online.load.difficulty == 0 ? "Level" : *online.load.difficulty == 1 ? "Level(N)" : "Level(H)";
            const auto level = monsters.number(detail.fileIndex, column);
            if (!level) return {};
            bodyCost = *definition->base.cost + int64_t(8) * *level;
        }
        std::vector<int> factors;
        std::vector<int> repairFactors;
        if (!detail.gamble) for (const auto &price : vendor->second.questPrices) {
            if (price.flag < 0 || !world.quests.playerFlags ||
                size_t(price.flag) >= world.quests.playerFlags->size()) return {};
            if ((*world.quests.playerFlags)[size_t(price.flag)] & 3u) {
                factors.push_back(repair ? price.repair : sale ? price.buy : price.sell);
                repairFactors.push_back(price.repair);
            }
        }
        int reduce = 0;
        if (!sale) {
            if (const auto value = stat("item_reducedprices")) reduce = int(*value);
            else {
                const auto known=knownEquipmentStat("item_reducedprices",characterView.level);
                if (!known) return {};
                reduce=*known;
            }
        }
        if (detail.gamble) {
            const auto level = stat("level");
            return itemGamblePrice(data, native.code, int(level.value_or(0)), detail.format, reduce);
        }
        if ((definition->maxStack > 1 || definition->bookCapacity) && !detail.quantity) return {};
        if (definition->family == ItemFamily::Armor && !detail.defense) return {};
        if (definition->family != ItemFamily::Misc && (!detail.maxDurability || (*detail.maxDurability && !detail.durability))) return {};
        const auto item = completeItem(native, detail, SocketLocation{{}, 0});
        if (!item) return {};
        return itemTradePrice(data, *item, vendor->second, repair, factors, reduce, sale, *online.load.difficulty, detail.autoAffix, bodyCost, repairFactors,
            definition->bookCapacity ? std::optional<unsigned>(detail.book) : std::nullopt);
    }
    InventoryItemView projectItem(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
            const auto *definition=data.items.find(native.code);
            const auto complete = completeItem(native, di, location);
            auto item = complete.value_or(itemInstance(native, di, location));
            const auto stats = resolveItemStats(data, item, characterView.level);
            const auto maximum = itemMaximumDurability(data, item, stats);
            auto display=describeInventoryItem(data,data.items,item,{characterView.level,characterView.attributes[0],characterView.attributes[1],maximum,{}});
            if (!complete) display.tooltip.push_back({"Socket properties are not fully assigned by the server",ItemTextTone::Error});
            std::string groundArt=definition->groundAnimation, inventoryArt=di.artKey;
            if(item.specialRow>=0) {
                const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
                for(const auto &record:records) if(int32_t(record.row)==item.specialRow) {
                    if(!record.groundAnimation.empty()) groundArt=record.groundAnimation;
                    if(item.identified && !record.icon.empty()) inventoryArt=record.icon;
                }
            }
            InventoryItemView projection{item.id,item.revision,item.definition,std::move(inventoryArt),std::move(display.name),location,item.quality,item.identified,item.quantity,item.durability,item.charges,std::move(display.tooltip),native.flags,item.sockets,item.runewordRow>=0,std::move(groundArt)};
            projection.specialRow = item.specialRow;
            return projection;
    }
    std::optional<unsigned> repairAllQuote() const {
        const auto &online = session.read();
        if (!shopView.repairAvailable || !online.load.playerUnitId || !onlineInteractionMatches(context, online)) return {};
        const auto actor = online.world.units.find({0, *online.load.playerUnitId});
        if (actor == online.world.units.end() || !actor->second.equipmentObserved) return {};
        uint64_t total = 0;
        for (const auto &[id, item] : online.world.items) {
            if (item.ownerType != 0 || item.owner != online.load.playerUnitId || item.mode != 1 || !item.body || item.body > 12) continue;
            if (!item.flags || !(item.flags & 0x10u) || (item.flags & 0x400000u)) continue;
            const auto decoded = items.read().items.find(id);
            const auto *definition = data.items.find(item.code);
            if (decoded == items.read().items.end() || !decoded->second.decoded || decoded->second.revision != item.revision || !definition) return {};
            const auto instance = completeItem(item, decoded->second, SocketLocation{{}, 0});
            if (!instance) return {};
            const auto stats = resolveItemStats(data, *instance, characterView.level);
            auto value = [&](std::string_view name) {
                int64_t totalValue = 0;
                for (const auto &stat : stats) if (stat.name == name) totalValue += stat.rawValue;
                return totalValue;
            };
            const auto maximumDurability = itemMaximumDurability(data, *instance, stats);
            const auto &source = data.tables.at(definition->base.sourceTable);
            bool needed = definition->equipment.repairable && definition->maxDurability && maximumDurability &&
                !source.number(definition->base.sourceRow, "nodurability").value_or(0) &&
                !value("item_indesctructible") && decoded->second.durability.value_or(0) != maximumDurability;
            if (definition->equipment.repairable && definition->equipment.throwable && definition->maxStack > 1) {
                const auto maximum = std::clamp(int64_t(definition->maxStack) + value("item_extra_stack"), int64_t(1), int64_t(511));
                if (!decoded->second.quantity) return {};
                needed |= *decoded->second.quantity < maximum;
            }
            for (const auto &stat : stats) {
                if (stat.name != "item_charged_skill") continue;
                if (stat.rawValue < 0 || stat.rawValue > 65535 || (unsigned(stat.rawValue) & 0xff) > (unsigned(stat.rawValue) >> 8)) return {};
                needed |= (unsigned(stat.rawValue) & 0xff) < (unsigned(stat.rawValue) >> 8);
            }
            if (!needed) continue;
            const auto price = itemQuote(id, OnlineItemAction::Repair);
            if (!price || total + *price > UINT32_MAX) return {};
            total += *price;
        }
        return unsigned(total);
    }
    void itemNotice(const InventoryItemView &item, unsigned quantity, std::string prefix, bool gold = false) {
        if (!quantity) return;
        std::string name = item.name;
        const auto *definition = data.items.find(item.definition);
        if (gold) name = std::to_string(quantity) + " " + name;
        else if (quantity > 1)
            name += definition && definition->bookCapacity ? " (" + std::to_string(quantity) + " pages)" :
                " x" + std::to_string(quantity);
        itemNotices.push_back(std::move(prefix) + name);
        while (itemNotices.size() > 32) itemNotices.pop_front();
    }
    void projectItemFeedback(const InventoryView &current) {
        const auto &w = session.read().world;
        if (inventoryView.gameGeneration != current.gameGeneration || inventoryView.areaGeneration != current.areaGeneration) {
            transferFeedback.reset(); feedbackSequence = 0; itemNotices.clear();
            return; // Initial snapshots are not transfers.
        }
        const auto now = std::chrono::steady_clock::now();
        const auto &request = w.itemRequest;
        if (request && request->sequence != feedbackSequence) {
            feedbackSequence = request->sequence; transferFeedback.reset();
            const auto action = request->command.action;
            const auto *source = inventoryView.item(itemId(request->command.item));
            const bool pickup = action == OnlineItemAction::Pickup;
            const auto *container = source ? std::get_if<ContainerLocation>(&source->location) : nullptr;
            if (source && source->revision == request->command.itemRevision && source->quantity &&
                ((pickup && std::holds_alternative<GroundLocation>(source->location)) ||
                 ((action == OnlineItemAction::Stack || action == OnlineItemAction::Book) &&
                  container && (container->container == inventoryView.containers.cursor ||
                                container->container == inventoryView.containers.backpack)))) {
                TransferFeedback feedback;
                feedback.action = action; feedback.sequence = request->sequence; feedback.source = *source;
                feedback.gold = inventoryView.gold; feedback.goldKnown = inventoryView.goldKnown;
                const auto *definition = data.items.find(source->definition);
                feedback.isGold = definition && definition->equipment.isType("gold");
                for (const auto &[id, item] : inventoryView.items) {
                    if (id == source->id || !std::holds_alternative<ContainerLocation>(item.location)) continue;
                    if (!pickup && id != itemId(request->command.target)) continue;
                    if (!pickup && item.revision != request->command.targetRevision) continue;
                    const auto *destination = data.items.find(item.definition);
                    if (destination && (item.definition == source->definition || destination->bookScroll == source->definition))
                        feedback.quantities.emplace(id,item.quantity);
                }
                transferFeedback = std::move(feedback);
            }
        }
        if (transferFeedback && request && request->state == OnlineItemRequest::State::Updated &&
            transferFeedback->expires == std::chrono::steady_clock::time_point::max())
            transferFeedback->expires = now + std::chrono::seconds(5); // Allow ordered follow-up packets after a related update.
        if (transferFeedback && (!request || request->sequence != transferFeedback->sequence ||
            request->state == OnlineItemRequest::State::Rejected || request->state == OnlineItemRequest::State::Interrupted ||
            request->state == OnlineItemRequest::State::TimedOut || current.dead || now >= transferFeedback->expires))
            transferFeedback.reset();
        EntityId tracked;
        if (transferFeedback) {
            auto &feedback = *transferFeedback;
            tracked = feedback.source.id;
            const auto *source = current.item(tracked);
            const bool removed = !w.items.contains(guid(tracked)); // A decode failure is not removal.
            const bool assigned = source && std::holds_alternative<ContainerLocation>(source->location);
            uint64_t received = 0;
            bool book = false;
            for (const auto &[id, before] : feedback.quantities) {
                const auto *destination = current.item(id);
                if (!destination || !std::holds_alternative<ContainerLocation>(destination->location) || destination->quantity <= before) continue;
                received += destination->quantity - before;
                if (const auto *definition = data.items.find(destination->definition)) book |= definition->bookCapacity != 0;
            }
            bool complete = false, confirmed = false;
            if (feedback.isGold) {
                // Gold pickup removes the pile (partial pickup creates a new pile).
                // Neither an unrelated gold stat nor disappearance alone is success.
                if (removed && feedback.goldKnown && current.goldKnown && current.gold > feedback.gold) {
                    received = current.gold - feedback.gold;
                    complete = received <= feedback.source.quantity;
                    confirmed = complete;
                } else received = 0;
            } else if (feedback.action == OnlineItemAction::Pickup && assigned) {
                received += source->quantity; // Include the remainder of an automatic stack pickup.
                complete = received == feedback.source.quantity;
                confirmed = complete;
            } else if (removed) {
                complete = received == feedback.source.quantity;
                confirmed = complete;
            } else if (source && source->quantity <= feedback.source.quantity &&
                       received == feedback.source.quantity - source->quantity) {
                // Partial manual/ground stacking requires both sides of the transfer.
                complete = source->quantity == 0;
                confirmed = true;
            } else received = 0;
            if (received > feedback.reported && received <= feedback.source.quantity &&
                confirmed) {
                const auto &display = source && assigned ? *source : feedback.source;
                const auto prefix = feedback.action == OnlineItemAction::Pickup ? "Picked up: " :
                    book ? "Added to tome: " : "Stacked: ";
                itemNotice(display,unsigned(received - feedback.reported),prefix,feedback.isGold);
                feedback.reported = unsigned(received);
            }
            if (complete) transferFeedback.reset();
        }
        // Preserve unsolicited, explicit ground-to-owned assignment feedback.
        for (const auto &[id, item] : current.items) {
            if (id == tracked || !std::holds_alternative<ContainerLocation>(item.location)) continue;
            const auto *previous = inventoryView.item(id);
            if (previous && std::holds_alternative<GroundLocation>(previous->location))
                itemNotice(item,item.quantity,"Picked up: ");
        }
    }
    void projectInventory() {
        const auto &online=session.read(); const auto &w=online.world; const auto &decoded=items.read();
        InventoryView v; v.revision=revision; v.gameGeneration=online.gameGeneration; v.areaGeneration=w.areaGeneration;
        v.containers={containerId(1),containerId(2),containerId(3),containerId(4),containerId(5),containerId(6),containerId(7),containerId(8)};
        const auto &owned=v.containers;
        for (const auto &[id,kind,columns,rows]:std::vector<std::tuple<EntityId,ContainerKind,int,int>>{
            {owned.backpack,ContainerKind::Backpack,decoded.columns,decoded.rows},{owned.belt,ContainerKind::Belt,4,std::max(1,decoded.beltSlots/4)},
            {owned.stash,ContainerKind::Stash,decoded.stashColumns,decoded.stashRows},{owned.cube,ContainerKind::Cube,decoded.cubeColumns,decoded.cubeRows},
            {owned.equipment,ContainerKind::Equipment,int(EquipmentSlot::Count),1},{owned.beltEquipment,ContainerKind::BeltEquipment,1,1},
            {owned.cursor,ContainerKind::Cursor,1,1},{owned.hirelingEquipment,ContainerKind::Equipment,int(EquipmentSlot::Count),1}})
            v.containerViews.emplace(id,InventoryContainerView{id,kind,columns,rows});
        auto layout=[](const auto &x){return InventoryLayoutView{x.columns,x.rows,x.left,x.top,x.cellSize,x.expansion};};
        v.stashLayout=layout(data.stashLayout); v.cubeLayout=layout(data.cubeLayout); v.hirelingSlots=data.hirelingLayout.slots;
        if (w.playerTrade.phase == OnlinePlayerTrade::Phase::Open) {
            const auto &table = data.tables.at("inventory");
            size_t peerRow = table.rows().size(), ownRow = peerRow;
            for (size_t row = 0; row < table.rows().size(); ++row) {
                if (table.value(row,"class") == "Trade Page 1-2") peerRow = row;
                if (table.value(row,"class") == "Trade Page 2-2") ownRow = row;
            }
            auto tradeLayout = [&](size_t row) {
                InventoryLayoutView l;
                if (row >= table.rows().size() || peerRow >= table.rows().size()) return l;
                l.columns = table.number(row,"gridX").value_or(0); l.rows = table.number(row,"gridY").value_or(0);
                l.left = table.number(row,"gridLeft").value_or(0) - table.number(peerRow,"invLeft").value_or(0);
                l.top = table.number(row,"gridTop").value_or(0) - table.number(peerRow,"invTop").value_or(0);
                l.cellSize = table.number(row,"gridBoxWidth").value_or(0);
                if (l.columns < 1 || l.rows < 1 || l.cellSize < 1 || l.left < 0 || l.top < 0 ||
                    l.cellSize != table.number(row,"gridBoxHeight").value_or(0) ||
                    l.left + l.columns*l.cellSize > 320 || l.top + l.rows*l.cellSize > 432) return InventoryLayoutView{};
                return l;
            };
            v.ownTradeLayout = tradeLayout(ownRow); v.peerTradeLayout = tradeLayout(peerRow);
            if (v.ownTradeLayout.cellSize && v.peerTradeLayout.cellSize) {
                v.ownTrade = containerId(9); v.peerTrade = containerId(10);
                v.containerViews.emplace(v.ownTrade, InventoryContainerView{v.ownTrade,ContainerKind::Backpack,v.ownTradeLayout.columns,v.ownTradeLayout.rows});
                v.containerViews.emplace(v.peerTrade, InventoryContainerView{v.peerTrade,ContainerKind::Backpack,v.peerTradeLayout.columns,v.peerTradeLayout.rows,true});
            }
        }
        v.cubeCode=data.cubeCode; v.staffRecipeOutput=data.staffRecipe.output;
        v.weaponSet=w.weaponSet; v.gold=unsigned(stat("gold").value_or(0)); v.bankGold=unsigned(stat("goldbank").value_or(0));
        v.goldKnown = stat("gold").has_value();
        v.walletLimit=unsigned(characterView.level)*10000; v.groundGoldLimit=v.walletLimit;
        v.bankGoldLimit=stashGoldLimit(unsigned(characterView.level));
        for (const auto &[code,def]:data.items.entries()) if(def.equipment.isType("gold")) { v.groundGoldLimit=def.maxStack; break; }
        v.dead=characterView.dead;
        if(w.storage.kind==OnlineStorageKind::Stash) v.storage=owned.stash;
        if(w.playerPosition) v.dropLocation=GroundLocation{mapView.region,mapView.observer};
        for(const auto &[id,native]:w.items) {
            const bool ground=native.mode==3 || native.mode==5;
            if(!ground && (native.ownerType!=0 || native.owner!=online.load.playerUnitId)) continue;
            const auto d=decoded.items.find(id); const auto *definition=data.items.find(native.code);
            if(d==decoded.items.end() || !d->second.decoded || !definition) continue;
            const auto &di=d->second; ItemLocation location;
            if(ground && scene && scene->origin) location=GroundLocation{mapView.region,
                {float(int(native.groundX)-scene->origin->x),float(int(native.groundY)-scene->origin->y)}};
            else if(native.mode==4) location=ContainerLocation{owned.cursor,{}};
            else if(native.mode==2) location=ContainerLocation{owned.belt,{native.x%4,native.x/4}};
            else if(native.mode==1 && native.body>=1 && native.body<=12) {
                int slot=native.body-1;
                if(native.body==4 || native.body==5) slot=int(weaponHandSlot(native.body==5,w.weaponSet));
                else if(native.body==11 || native.body==12) slot=int(weaponHandSlot(native.body==12,1-w.weaponSet));
                location = native.body==8 ? ItemLocation{ContainerLocation{owned.beltEquipment,{}}} : ItemLocation{ContainerLocation{owned.equipment,{slot,0}}};
            } else if(native.mode==0 && (native.page==1 || native.page==4 || native.page==5))
                location=ContainerLocation{native.page==1?owned.backpack:native.page==4?owned.cube:owned.stash,{native.x,native.y}};
            else if(native.mode==0 && ((native.page==3 && v.ownTrade) || (native.page==2 && v.peerTrade)))
                location=ContainerLocation{native.page==3?v.ownTrade:v.peerTrade,{native.x,native.y}};
            else continue;
            InventoryDefinitionView def; def.targetCursor=definition->targetCursor; def.code=native.code; def.name=definition->name; def.bookScroll=definition->bookScroll;
            def.width=di.width; def.height=di.height; def.beltRows=definition->beltRows; def.maxStack=definition->maxStack; def.bookCapacity=definition->bookCapacity;
            def.beltAllowed=definition->beltAllowed; def.opensCube=definition->opensCube; def.twoHanded=definition->equipment.twoHanded;
            def.socketFiller=definition->equipment.isType("sock"); def.identifySource=data.isIdentifyScroll(native.code)||data.isIdentifyScroll(def.bookScroll);
            for(size_t slot=0;slot<def.slots.size();++slot) def.slots[slot]=definition->equipment.fits(EquipmentSlot(slot));
            v.definitions.emplace(native.code,std::move(def));
            auto projection=projectItem(native,di,location);
            if (ground) {
                projection.groundPickupAllowed = native.mode == 3;
                projection.groundAnimationRevision = native.groundAnimationRevision;
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if (native.groundAnimationRevision && now >= native.groundAnimationReceivedMilliseconds)
                    projection.groundAnimationAge = std::min(4.f, float(now-native.groundAnimationReceivedMilliseconds)/1000.f);
            }
            v.items.emplace(itemId(id), std::move(projection));
        }
        if(w.itemRequest && w.itemRequest->command.action==OnlineItemAction::Pickup && w.itemRequest->state==OnlineItemRequest::State::Pending)
            v.pickupTarget=itemId(w.itemRequest->command.item);
        projectItemFeedback(v);
        v.targetingRevision=w.itemTargetingRevision;
        v.targetingReady=!w.itemTargetingSource;
        if(w.itemTargetingSource) {
            const auto source=v.items.find(itemId(*w.itemTargetingSource));
            if(source!=v.items.end()) {
                v.targetingReady=true;
                const auto definition=v.definitions.find(source->second.definition);
                if(definition!=v.definitions.end() && definition->second.identifySource) v.targetingSource=source->second.handle();
            }
        }
        inventoryView=std::move(v);
    }
    std::string npcIdentity() const {
        const auto &w=session.read().world; if(!w.npcConversation) return {};
        auto unit=w.units.find({1,w.npcConversation->source});
        if(unit==w.units.end() || !unit->second.classId) return {};
        const auto &mon=data.tables.at("monstats");
        for(size_t row=0;row<mon.rows().size();++row) if(mon.number(row,"hcIdx")==*unit->second.classId) return std::string(mon.value(row,"Id"));
        return {};
    }
    void projectInteractions() {
        const auto &online=session.read(); const auto &w=online.world;
        mapView={}; mapView.revision=revision; mapView.actor=characterView.actor;
        mapView.travelRequested = w.waypointRequested.has_value();
        mapView.act=online.load.act.value_or(0); mapView.region=RegionId(scene&&scene->area?*scene->area:1);
        mapView.palette=scene&&scene->palette?*scene->palette:mapView.act;
        if(scene && scene->origin && w.playerPosition) mapView.observer={float(int(w.playerPosition->x)-scene->origin->x),float(int(w.playerPosition->y)-scene->origin->y)};
        if(scene) for(const auto &d:scene->waypoints) if(d.unlocked) mapView.waypointActs[d.act]=true;
        npcView={}; npcView.revision=revision; npcView.actor=characterView.actor;
        npcScene={}; npcScene.revision=revision;
        const auto &mon=data.tables.at("monstats"), &extra=data.tables.at("monstats2");
        for (const auto &key:w.questAlerts) {
            const auto unit=w.units.find(key);
            if (key.type!=1 || unit==w.units.end() || !unit->second.classId) continue;
            for (size_t row=0; row<mon.rows().size(); ++row) {
                if (mon.number(row,"hcIdx")!=*unit->second.classId || !mon.number(row,"interact").value_or(0)) continue;
                for (size_t sub=0; sub<extra.rows().size(); ++sub)
                    if (extra.value(sub,"Id")==mon.value(row,"MonStatsEx")) {
                        npcScene.npcs[npcId(key.id)]={true,extra.number(sub,"OverlayHeight").value_or(0)-1};
                        break;
                    }
                break;
            }
        }
        if(scene && scene->npcConversation) {
            const auto &d=*scene->npcConversation; npcView.npc=npcId(d.source); npcView.valid=true; npcView.speaker=d.speaker;
            if(scene->origin) npcView.position={float(int(d.position.x)-scene->origin->x),float(int(d.position.y)-scene->origin->y)};
            for(const auto &m:d.messages) if(!m.text.empty()) {
                if(m.menu==0 && !m.acknowledged && !npcView.introduction) npcView.introduction=m.text;
                else if(m.menu==1 || m.menu==2) {
                    npcView.textTopics.emplace(m.stringId,m.text);
                    auto title=m.text.substr(0,m.text.find('\n'));
                    if(title.size()>40) title=title.substr(0,40)+"...";
                    npcView.talkEntries.push_back({title,{NpcMenuAction::TextTopic,{},m.stringId}});
                }
            }
            auto add=[&](std::string label,NpcMenuAction action) { npcView.services.push_back({std::move(label),{action,{}}}); };
            if(!npcView.textTopics.empty()) {
                add("Talk",NpcMenuAction::Talk);
                npcView.talkEntries.push_back({"Back",{NpcMenuAction::Back,{}}});
            }
            const auto identity=npcIdentity();
            if(data.vendors.contains(identity) && identity!="nihlathak") add("Trade",NpcMenuAction::Trade);
            if(identity=="gheed" || identity=="elzix" || identity=="alkor" || identity=="jamella" || identity=="drehya" || identity=="nihlathak") add("Gamble",NpcMenuAction::Gamble);
            if(identity.starts_with("cain")) add("Identify Items",NpcMenuAction::Identify);
            if(!d.travelLabel.empty()) add(d.travelLabel,NpcMenuAction::GoEast);
            add("Cancel",NpcMenuAction::Cancel);
        }
        QuestProjectionInput questInput;
        questInput.revision=revision; questInput.actor=characterView.actor; questInput.currentAct=mapView.act;
        questInput.denRemaining=w.quests.denRemaining;
        for (const auto &def:questDefinitions) {
            auto &entry=questInput.entries[questIndex(def.id)];
            entry.status=w.quests.statuses[def.nativeSlot];
            if (w.quests.playerFlags) entry.playerFlags=(*w.quests.playerFlags)[def.nativeSlot];
        }
        questView=projectQuestDisplay(data,questInput);
        shopView={}; shopView.revision=revision; shopView.actor=characterView.actor; shopView.npc=npcView.npc;
        shopView.bankGold=inventoryView.bankGold; shopView.pricesKnown=false;
        shopView.tabLabels={"Weapons","Armor","Armor","Misc"};
        const auto identity=npcIdentity();
        shopView.gamble=w.shopGamble;
        shopView.available=npcView.valid && (identity=="nihlathak" || data.vendors.contains(identity));
        shopView.repairAvailable=!w.shopGamble && (identity=="charsi" || identity=="fara" || identity=="hratli" || identity=="halbu" || identity=="larzuk");
        if(w.shopSource) for(const auto &[id,native]:w.items) {
            if(native.ownerType!=1 || native.owner!=w.shopSource || native.action!=11) continue;
            auto d=items.read().items.find(id); const auto *def=data.items.find(native.code);
            if(d==items.read().items.end() || !d->second.decoded || !def) continue;
            const auto &item=d->second;
            ShopOfferView offer; offer.slot=id; offer.width=item.width; offer.height=item.height; offer.definition=native.code;
            auto projection=projectItem(native,item,SocketLocation{{},0});
            offer.name=projection.name; offer.artKey=projection.artKey; offer.quality=projection.quality;
            const auto price = itemQuote(id, OnlineItemAction::Buy);
            offer.priceKnown = price.has_value(); offer.price = price.value_or(0);
            offer.storePage=def->family==ItemFamily::Weapon?0:def->family==ItemFamily::Armor?1:3;
            offer.tooltip=projection.tooltip;
            if (item.gamble) offer.tooltip={{projection.name,ItemTextTone::Name}};
            shopView.offers.push_back(std::move(offer));
            // Include read-only shelf art in the shared inventory art cache, not owned inventory occupancy.
            inventoryView.items.emplace(projection.id,std::move(projection));
        }
        shopView.pricesKnown = !shopView.offers.empty() && std::all_of(shopView.offers.begin(), shopView.offers.end(), [](const auto &offer) { return offer.priceKnown; });
        shopView.repairAllPrice = repairAllQuote();
    }
    void update(const OnlineSceneView &binding) {
        scene=&binding;
        context = onlineIntentContext(session.read());
        if(generation!=session.read().gameGeneration) {
            generation=session.read().gameGeneration; transactions.clear(); waiting.reset(); waitingContext.reset(); hotkeys={};
        }
        if (areaGeneration!=session.read().world.areaGeneration) {
            areaGeneration=session.read().world.areaGeneration; transactions.clear(); waiting.reset(); waitingContext.reset();
        }
        ++revision; projectInteractions(); projectCharacter(); projectInventory(); projectInteractions();
        if (characterView.dead) { transactions.clear(); waiting.reset(); waitingContext.reset(); }
        else pump();
    }
};
RemoteUiClients::RemoteUiClients(Archives &a,net::RealmSession &s,RemoteInventory &i,RemoteCombat &c,RemoteControl &r,bool running):impl_(std::make_unique<Impl>(a,s,i,c,r)){impl_->run=running;}
RemoteUiClients::~RemoteUiClients()=default;
size_t RemoteUiClients::queuedItemCommands() const { return impl_->transactions.size(); }
std::optional<uint64_t> RemoteUiClients::waitingItemRequest() const { return impl_->waiting; }
std::optional<unsigned> RemoteUiClients::itemQuote(uint32_t item, OnlineItemAction action) const {
    return action == OnlineItemAction::RepairAll ? impl_->repairAllQuote() : impl_->itemQuote(item, action);
}
void RemoteUiClients::update(const OnlineSceneView &s){impl_->update(s);}
const ClassicData &RemoteUiClients::content()const{return impl_->data;}
IActorClient &RemoteUiClients::actor(){return impl_->actor;}
IInventoryClient &RemoteUiClients::inventory(){return impl_->inventory;}
ICharacterClient &RemoteUiClients::character(){return impl_->character;}
INpcClient &RemoteUiClients::npc(){return impl_->npc;}
IQuestClient &RemoteUiClients::quests(){return impl_->quests;}
IMapClient &RemoteUiClients::map(){return impl_->map;}
bool RemoteUiClients::running()const{return impl_->run;}
bool RemoteUiClients::busy()const{
    const auto &request=impl_->session.read().world.itemRequest;
    return impl_->waiting.has_value()||!impl_->transactions.empty()||(request&&request->state==OnlineItemRequest::State::Pending);
}
RemoteUiNotice RemoteUiClients::takeNotice(){
    if (!impl_->notice.empty()) return {std::exchange(impl_->notice,{}),true};
    if (impl_->itemNotices.empty()) return {};
    auto notice = std::move(impl_->itemNotices.front()); impl_->itemNotices.pop_front();
    return {std::move(notice),false};
}
}
