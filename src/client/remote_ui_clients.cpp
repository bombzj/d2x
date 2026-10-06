#include "remote_ui_clients.hpp"
#include "client/remote_inventory.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "content/classic_data.hpp"
#include "content/character/character_progression.hpp"
#include "content/items/item_display.hpp"
#include "gameplay/character/attributes.hpp"
#include "contracts/online_scene.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/items/gold_limits.hpp"
#include "network/realm_session.hpp"
#include <algorithm>
#include <deque>
#include <cmath>
#include <utility>

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

    struct Actor final : IActorClient {
        Impl &o; explicit Actor(Impl &owner) : o(owner) {}
        ActorView controlledActor() const override {
            ActorView v; v.id = o.characterView.actor; v.region = o.mapView.region;
            v.dead = o.characterView.dead; v.position = o.mapView.observer;
            v.lightRadius = int(std::clamp(int64_t(CharacterAttributes{}.lightRadius) +
                int64_t(o.stat("item_lightradius").value_or(0)), int64_t(1), int64_t(18)));
            return v;
        }
        void control(ActorControlIntent) override {}
        void move(MoveIntent) override {} // World input remains bound to RemoteControl.
        void stopMoving() override { stopActions(); }
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
        const ShopOfferView *inspectShopOffer(EntityId, uint32_t slot, bool) const override {
            const auto it = std::find_if(o.shopView.offers.begin(), o.shopView.offers.end(),
                [&](const auto &v) { return v.slot == slot; });
            return it == o.shopView.offers.end() ? nullptr : &*it;
        }
        std::optional<unsigned> quote(EntityId, ItemHandle, bool) const override {
            return std::nullopt; // Native price calculation has not been implemented.
        }
        bool canRequestSale(EntityId npc, ItemHandle item) const override {
            const auto *entry = o.inventoryView.item(item.id);
            return entry && entry->revision == item.revision && o.context.npc == npcGuid(npc) &&
                onlineInteractionMatches(o.context, o.session.read());
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
                else if constexpr (std::is_same_v<T, TalkToNpc>) return;
                else if constexpr (std::is_same_v<T, CompleteActOne> || std::is_same_v<T, CompleteActTwo>) { o.session.npc_travel(o.context); return; }
                else if constexpr (std::is_same_v<T, IdentifyWithCain>) request.action = OnlineItemAction::IdentifyAll;
                else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    request.action = OnlineItemAction::Buy; request.item = v.slot;
                    if (v.gamble) { o.notice = "Native gambling service is unavailable."; return; }
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
        if (!items.submit(session,c)) { notice = items.reason(); transactions.clear(); return; }
        const auto &sent = session.read();
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
                next.page = target.container == owned.cube ? 3 : target.container == owned.stash ? 4 : 0;
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
            if constexpr (std::is_same_v<T, MoveItem>) { moveItem(v.item,v.destination); return; }
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
                if (loc) { c.x=uint8_t(loc->cell.x); c.y=uint8_t(loc->cell.y); }
                if (items.read().cursor != c.item) { compositeTake(c); return; }
            } else if constexpr (std::is_same_v<T, MergeStacks> || std::is_same_v<T, LoadBook> || std::is_same_v<T, SocketItem> || std::is_same_v<T, IdentifyItem>) {
                if constexpr (std::is_same_v<T, MergeStacks>) { c.action=OnlineItemAction::Stack; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
                if constexpr (std::is_same_v<T, LoadBook>) { c.action=OnlineItemAction::Book; c.item=guid(v.scroll.id); c.itemRevision=v.scroll.revision; c.target=guid(v.book.id); c.targetRevision=v.book.revision; }
                if constexpr (std::is_same_v<T, SocketItem>) { c.action=OnlineItemAction::Socket; c.item=guid(v.filler.id); c.itemRevision=v.filler.revision; c.target=guid(v.host.id); c.targetRevision=v.host.revision; }
                if constexpr (std::is_same_v<T, IdentifyItem>) { c.action=OnlineItemAction::Identify; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
            } else if constexpr (std::is_same_v<T, SwitchWeaponSet>) c.action = OnlineItemAction::SwitchWeapons;
            else if constexpr (std::is_same_v<T, TransmuteCube>) c.action = OnlineItemAction::Transmute;
            else if constexpr (std::is_same_v<T, CloseStorage>) c.action = OnlineItemAction::StorageClose;
            else if constexpr (std::is_same_v<T, GoldTransaction>) {
                c.action = v.action == GoldAction::Deposit ? OnlineItemAction::GoldDeposit : v.action == GoldAction::Withdraw ? OnlineItemAction::GoldWithdraw : OnlineItemAction::GoldDrop; c.amount = v.amount;
            } else { notice = "This native item operation is unavailable."; return; }
            if (c.action == OnlineItemAction::Socket && items.read().cursor != c.item) { compositeTake(c); return; }
            enqueue(c);
        }, intent);
    }
    void projectCharacter() {
        const auto &online = session.read(); const auto &w = online.world;
        CharacterView v; v.revision = revision; v.name = online.selectedCharacter;
        v.actor = online.load.playerUnitId ? itemId(*online.load.playerUnitId) : EntityId{};
        std::optional<uint16_t> cls;
        if (online.load.playerUnitId) if (auto it=w.units.find({0,*online.load.playerUnitId});it!=w.units.end()) cls=it->second.classId;
        const auto &characters = data.tables.at("charstats");
        if (cls && *cls < data.characters.size()) {
            const auto &definition = data.characters[*cls];
            v.className = definition.name; v.classCode = definition.code;
            auto xp = experienceThresholds(data.tables.at("experience"),characters.value(definition.sourceRow,"class"));
            v.level = int(stat("level").value_or(1)); v.experience = uint64_t(stat("experience").value_or(0));
            if (size_t(v.level)<xp.size()) v.currentLevelExperience=xp[size_t(v.level)];
            if (size_t(v.level+1)<xp.size()) v.nextLevelExperience=xp[size_t(v.level+1)];
            if (!xp.empty()) v.maximumExperience=xp.back();
        }
        v.attributes={int(stat("strength").value_or(0)),int(stat("dexterity").value_or(0)),int(stat("vitality").value_or(0)),int(stat("energy").value_or(0))};
        v.resistances={int(stat("fireresist").value_or(0)),int(stat("coldresist").value_or(0)),int(stat("lightresist").value_or(0)),int(stat("poisonresist").value_or(0))};
        v.unspentAttributes=int(stat("statpts").value_or(0)); v.unspentSkills=int(stat("newskills").value_or(0));
        v.hp=w.life.value_or(uint16_t(stat("hitpoints").value_or(0))); v.mana=w.mana.value_or(uint16_t(stat("mana").value_or(0))); v.stamina=w.stamina.value_or(uint16_t(stat("stamina").value_or(0)));
        v.maxLife=int(stat("maxhp").value_or(0)); v.maxMana=int(stat("maxmana").value_or(0)); v.maxStamina=int(stat("maxstamina").value_or(0));
        v.dead=onlinePlayerDead(w); v.running=run; v.weaponSet=w.weaponSet;
        if (v.dead) v.hp = 0;
        else if (w.life && !*w.life && w.deathPhase == OnlineDeathPhase::Alive && v.maxLife > 0)
            // SCmd sends fixed-point HP >> 8. A living death-save reentry can
            // therefore report zero whole HP; the original UI displays 1 HP.
            // Keep the replica/sample untouched and only project the living HUD.
            v.hp = 1;
        v.attackUsable=!v.dead && scene && !scene->town;
        v.attack.damage="?"; v.attack.attackRating="?";
        v.defense=int(stat("armorclass").value_or(0));
        if (const auto *tree=data.skills.tree(v.classCode)) { v.hasSkillTree=true; v.pageNames=tree->pageNames; }
        v.selectedSkills[v.weaponSet*2] = w.leftSkill ? int(w.leftSkill->skill) : -1;
        v.selectedSkills[v.weaponSet*2+1] = w.rightSkill ? int(w.rightSkill->skill) : -1;
        for (size_t slot=0; slot<v.skillHotkeys.size(); ++slot) {
            if (hotkeys[slot]) v.skillHotkeys[slot]=*hotkeys[slot];
            else if (const auto &native=w.skillHotkeys[slot]; native && native->selection && native->selection->owner==UINT32_MAX)
                v.skillHotkeys[slot]={native->selection->skill ? int(native->selection->skill) : -1,
                    native->hand==OnlineSkillHand::Right};
        }
        v.choices[0].push_back(std::nullopt); v.choices[1].push_back(std::nullopt);
        const auto *tree=data.skills.tree(v.classCode);
        auto throwReady = [&](bool leftHand) {
            for (uint8_t body : {uint8_t(leftHand ? 5 : 4), uint8_t(5)}) {
                for (const auto &[itemId,item] : w.items) {
                    if (item.mode!=1 || item.body!=body || item.ownerType!=0 || item.owner!=online.load.playerUnitId) continue;
                    const auto *definition=data.items.find(item.code);
                    if (!definition || definition->family!=ItemFamily::Weapon) continue;
                    const auto decoded=items.read().items.find(itemId);
                    return definition->equipment.throwable && decoded!=items.read().items.end() &&
                        decoded->second.decoded && decoded->second.quantity.value_or(0)>0;
                }
                if (leftHand) break;
            }
            return false;
        };
        for (const auto &[id,entry]:data.skills.skills) {
            auto base=w.playerBaseSkills.find(uint16_t(id)), bonus=w.playerBonusSkills.find(uint16_t(id));
            const int rank=base==w.playerBaseSkills.end()?0:base->second;
            const int effective=rank+(bonus==w.playerBonusSkills.end()?0:bonus->second);
            if (entry.classCode != v.classCode && !entry.classCode.empty() && !effective) continue;
            CharacterSkillView skill; skill.id=id; skill.page=entry.page; skill.row=entry.row; skill.column=entry.column;
            skill.listRow=entry.listRow; skill.listPool=entry.listPool; skill.iconCell=entry.iconCell;
            skill.classCode=entry.classCode; skill.name=entry.name; skill.baseRank=rank; skill.effectiveRank=effective;
            skill.maximumRank=entry.maximumRank; skill.nextRequiredLevel=entry.requiredLevel+rank;
            skill.passive=entry.passive; skill.leftAllowed=entry.leftAllowed;
            auto wire=w.playerSkills.find(uint16_t(id));
            if (wire!=w.playerSkills.end()) skill.effectiveRank=std::max(skill.effectiveRank,int(wire->second));
            const bool innate=tree && (entry.basicAction==BasicSkillAction::Attack ||
                std::find(tree->commonSkills.begin(),tree->commonSkills.end(),id)!=tree->commonSkills.end());
            skill.available=skill.effectiveRank>0 || innate;
            skill.canAllocate=entry.classCode==v.classCode && rank<entry.maximumRank && v.unspentSkills>0 && v.level>=skill.nextRequiredLevel;
            for(int required:entry.prerequisites) {
                auto r=w.playerBaseSkills.find(uint16_t(required)); skill.canAllocate &= r!=w.playerBaseSkills.end() && r->second>0;
            }
            skill.usableNow=skill.available && !entry.passive && !v.dead && (!scene || !scene->town || entry.allowedInTown);
            skill.pickerEnabled=skill.available && !entry.passive && !v.dead;
            if (entry.basicAction==BasicSkillAction::Throw || entry.basicAction==BasicSkillAction::LeftHandThrow) {
                skill.pickerEnabled &= throwReady(entry.basicAction==BasicSkillAction::LeftHandThrow);
                skill.usableNow &= skill.pickerEnabled;
            }
            skill.action.damage="?"; skill.action.attackRating="?";
            skill.treeTooltip={entry.name+" "+std::to_string(rank)+"/"+std::to_string(entry.maximumRank)};
            if (!entry.description.empty()) skill.treeTooltip.push_back(entry.description);
            skill.treeTooltip.push_back("Required level "+std::to_string(skill.nextRequiredLevel));
            skill.pickerTooltip=skill.treeTooltip;
            if (skill.available && !skill.passive && id!=0) {
                v.choices[1].push_back(id); if (skill.leftAllowed) v.choices[0].push_back(id);
            }
            v.skills.emplace(id,std::move(skill));
        }
        for (std::string name : {"strength","dexterity","vitality","energy","level","experience",
            "hitpoints","maxhp","mana","maxmana","stamina","maxstamina","armorclass","toblock",
            "fireresist","coldresist","lightresist","poisonresist","damageresist","magicresist",
            "normal_damage_reduction","magic_damage_reduction","poisonlengthresist","fireabsorb"})
            if (!stat(name) && !(name=="hitpoints" && w.life) && !(name=="mana" && w.mana) && !(name=="stamina" && w.stamina))
                v.unknownStats.insert(std::move(name));
        v.physicalResist=int(stat("damageresist").value_or(0)); v.magicResist=int(stat("magicresist").value_or(0));
        v.flatPhysicalReduction=int(stat("normal_damage_reduction").value_or(0)); v.flatMagicReduction=int(stat("magic_damage_reduction").value_or(0));
        v.poisonLengthResist=int(stat("poisonlengthresist").value_or(0)); v.fireAbsorbPercent=int(stat("fireabsorb").value_or(0));
        // Final block chance depends on shield, dexterity and level; a flat native toblock stat is insufficient.
        v.unknownStats.insert("toblock");
        characterView=std::move(v);
    }
    InventoryItemView projectItem(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
        const auto *definition=data.items.find(native.code);
            ItemInstance item; item.id=itemId(native.id); item.revision=native.revision; item.definition=native.code; item.location=location;
            item.quantity=di.gold.value_or(di.quantity.value_or(1)); item.charges=definition->bookCapacity?item.quantity:0; item.durability=di.durability.value_or(0);
            item.quality=ItemQuality(di.quality); item.identified=di.identified; item.level=di.level; item.defense=int(di.defense.value_or(0));
            item.nativeProperties=true; item.nativeFlags=native.flags; item.nativeMaxDurability=di.maxDurability.value_or(0);
            item.nativeFormat=di.format; item.nativeGraphic=di.graphic; item.nativeHasGraphic=di.hasGraphic; item.personalizedName=di.personalizedName;
            item.sockets=di.sockets; item.runewordRow=-1;
            for (const auto &word:data.runewords) if (word.stringId==di.runeword) item.runewordRow=word.row;
            if(di.quality==5 || di.quality==7) item.specialRow=di.fileIndex;
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
            if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted) {
                item.rarePrefixRow=int(di.rarePrefix)-int(data.tables.at("raresuffix").rows().size())-1;
                item.rareSuffixRow=int(di.rareSuffix)-1;
            }
            for(const auto &s:di.stats) item.savedStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(const auto &s:di.runewordStats) item.runewordStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(size_t index=0;index<di.setStats.size();++index) for(const auto &s:di.setStats[index]) item.savedSetStats[index].push_back({s.id,int(s.parameter),int(s.value)});
            auto display=describeInventoryItem(data,data.items,item,{characterView.level,characterView.attributes[0],characterView.attributes[1],item.nativeMaxDurability,{}});
            std::string groundArt=definition->groundAnimation;
            if(item.specialRow>=0) {
                const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
                for(const auto &record:records) if(int32_t(record.row)==item.specialRow && !record.groundAnimation.empty()) groundArt=record.groundAnimation;
            }
            return InventoryItemView{item.id,item.revision,item.definition,di.artKey,std::move(display.name),location,item.quality,item.identified,item.quantity,item.durability,item.charges,std::move(display.tooltip),native.flags,item.sockets,item.runewordRow>=0,std::move(groundArt)};
    }
    void projectInventory() {
        const auto &online=session.read(); const auto &w=online.world; const auto &decoded=items.read();
        InventoryView v; v.revision=revision;
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
        v.cubeCode=data.cubeCode; v.staffRecipeOutput=data.staffRecipe.output;
        v.weaponSet=w.weaponSet; v.gold=unsigned(stat("gold").value_or(0)); v.bankGold=unsigned(stat("goldbank").value_or(0));
        v.walletLimit=unsigned(characterView.level)*10000; v.groundGoldLimit=v.walletLimit;
        v.bankGoldLimit=stashGoldLimit(unsigned(characterView.level));
        for (const auto &[code,def]:data.items.entries()) if(def.equipment.isType("gold")) { v.groundGoldLimit=def.maxStack; break; }
        v.dead=characterView.dead;
        if(w.storage.kind==OnlineStorageKind::Stash) v.storage=owned.stash;
        if(w.playerPosition) v.dropLocation=GroundLocation{mapView.region,mapView.observer};
        for(const auto &[id,native]:w.items) {
            const bool ground=native.mode==3;
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
            else continue;
            InventoryDefinitionView def; def.targetCursor=definition->targetCursor; def.code=native.code; def.name=definition->name; def.bookScroll=definition->bookScroll;
            def.width=di.width; def.height=di.height; def.beltRows=definition->beltRows; def.maxStack=definition->maxStack; def.bookCapacity=definition->bookCapacity;
            def.beltAllowed=definition->beltAllowed; def.opensCube=definition->opensCube; def.twoHanded=definition->equipment.twoHanded;
            def.socketFiller=definition->equipment.isType("sock"); def.identifySource=data.isIdentifyScroll(native.code)||data.isIdentifyScroll(def.bookScroll);
            for(size_t slot=0;slot<def.slots.size();++slot) def.slots[slot]=definition->equipment.fits(EquipmentSlot(slot));
            v.definitions.emplace(native.code,std::move(def));
            v.items.emplace(itemId(id), projectItem(native,di,location));
        }
        if(w.itemRequest && w.itemRequest->command.action==OnlineItemAction::Pickup && w.itemRequest->state==OnlineItemRequest::State::Pending)
            v.pickupTarget=itemId(w.itemRequest->command.item);
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
            if(identity.starts_with("cain")) add("Identify Items",NpcMenuAction::Identify);
            if(!d.travelLabel.empty()) add(d.travelLabel,NpcMenuAction::GoEast);
            add("Cancel",NpcMenuAction::Cancel);
        }
        questView={}; questView.revision=revision; questView.actor=characterView.actor; questView.currentAct=mapView.act; questView.tabCount=5;
        for(const auto &def:questDefinitions) {
            questView.acts[size_t(def.act)].push_back(def.id);
            auto &entry=questView.entries[questIndex(def.id)]; entry.act=def.act; entry.displaySlot=def.displaySlot; entry.icon=def.icon;
            entry.title=data.questContent[questIndex(def.id)].title;
            const auto &state=w.quests;
            const auto status=state.statuses[def.nativeSlot];
            const auto flags=state.playerFlags ? std::optional<uint16_t>{(*state.playerFlags)[def.nativeSlot]} : std::nullopt;
            entry.known=flags.has_value() || status.has_value();
            entry.completed=(flags && (*flags & 1)) || status==13; // Native completed log status; never shared flags.
            entry.active=!entry.completed && ((status && *status) || (flags && (*flags & 0x001E)));
            std::string key;
            if (entry.completed) key="qstsComplete";
            else if (status && *status==12) key="qstsThankYouComeAgain";
            else if (status && *status>0 && *status<12) {
                key=(def.id==QuestId::SiegeOnHarrogath ? "qsta" : "qstsa") +
                    std::to_string(def.act+1)+"q"+std::to_string(def.nativeQuest)+std::to_string(*status);
                if (def.id==QuestId::DenOfEvil && *status==4 && state.denRemaining==1) key+="0";
            }
            if (const auto text=data.questStrings.find(key); text!=data.questStrings.end()) {
                entry.description=text->second;
                if (def.id==QuestId::DenOfEvil && state.denRemaining)
                    if (const auto at=entry.description->find("%d"); at!=std::string::npos)
                        entry.description->replace(at,2,std::to_string(*state.denRemaining));
            } else if (!entry.known || entry.active)
                entry.description="Native quest description is not available yet.";
        }
        const auto den=w.quests.statuses[questDefinition(QuestId::DenOfEvil).nativeSlot];
        questView.showDenRemaining=den==4 && !questView.entry(QuestId::DenOfEvil).completed;
        if (questView.showDenRemaining) questView.denRemaining=w.quests.denRemaining;
        shopView={}; shopView.revision=revision; shopView.actor=characterView.actor; shopView.npc=npcView.npc;
        shopView.bankGold=inventoryView.bankGold; shopView.pricesKnown=false;
        shopView.tabLabels={"Weapons","Armor","Armor","Misc"};
        const auto identity=npcIdentity();
        shopView.available=npcView.valid && data.vendors.contains(identity) && identity!="nihlathak";
        shopView.repairAvailable=identity=="charsi" || identity=="fara" || identity=="hratli" || identity=="halbu" || identity=="larzuk";
        if(w.shopSource) for(const auto &[id,native]:w.items) {
            if(native.ownerType!=1 || native.owner!=w.shopSource || native.action!=11) continue;
            auto d=items.read().items.find(id); const auto *def=data.items.find(native.code);
            if(d==items.read().items.end() || !d->second.decoded || !def) continue;
            const auto &item=d->second;
            ShopOfferView offer; offer.slot=id; offer.width=item.width; offer.height=item.height; offer.definition=native.code;
            auto projection=projectItem(native,item,SocketLocation{{},0});
            offer.name=projection.name; offer.artKey=item.artKey; offer.quality=ItemQuality(item.quality);
            offer.storePage=def->family==ItemFamily::Weapon?0:def->family==ItemFamily::Armor?1:3;
            offer.tooltip=projection.tooltip;
            offer.tooltip.push_back({"Price is determined by the server",ItemTextTone::Normal});
            shopView.offers.push_back(std::move(offer));
            // Include read-only shelf art in the shared inventory art cache, not owned inventory occupancy.
            inventoryView.items.emplace(projection.id,std::move(projection));
        }
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
std::string RemoteUiClients::takeNotice(){return std::exchange(impl_->notice,{});}
void RemoteUiClients::openShop(){
    OnlineItemCommand c; c.action=OnlineItemAction::TradeOpen;
    if(impl_->session.read().world.npcConversation) {c.npc=impl_->session.read().world.npcConversation->source;impl_->enqueue(c);}
}
}
