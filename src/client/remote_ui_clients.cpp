#include "gameplay/skills/amazon_passive_spec.hpp"
#include "remote_ui_clients.hpp"
#include "gameplay/items/belt_rules.hpp"
#include "item_skill_sources.hpp"
#include "client/character_projection.hpp"
#include "client/quest_projection.hpp"
#include "client/pet_projection.hpp"
#include "client/remote_inventory.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "content/classic_data.hpp"
#include "content/string_table.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "content/skills/description_values.hpp"
#include "content/character/character_progression.hpp"
#include "content/items/item_display.hpp"
#include "content/items/item_appearance.hpp"
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
    std::shared_ptr<const ClassicData> sharedData;
    const ClassicData &data;
    const OnlineSceneView *scene{};
    std::optional<Vec> displayPosition;
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
    mutable std::map<std::pair<OnlineUnitKey,std::string>,CachedEquipmentStat> equipmentStats;
    using CombatEquipmentKey=std::tuple<uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,std::string,int,int,int,int,unsigned,bool,int,int,int,int,std::map<int,int>>;
    struct CachedCombatEquipment {CombatEquipmentKey key;std::optional<CombatDisplayEquipment> value;};
    mutable std::map<OnlineUnitKey,CachedCombatEquipment> combatEquipment;

    struct Actor final : IActorClient {
        Impl &o; explicit Actor(Impl &owner) : o(owner) {}
        mutable EntityId lightActor;
        mutable uint64_t lightGeneration{~uint64_t{}};
        mutable int lightRadius{13};
        ActorView controlledActor() const override {
            ActorView v; v.id = o.characterView.actor; v.region = o.mapView.region;
            v.dead = o.characterView.dead; v.position = o.mapView.observer;
            if (lightActor != v.id || lightGeneration != o.generation) {
                lightActor = v.id; lightGeneration = o.generation; lightRadius = 13;
            }
            // Client 18BC0 -> A8B60: a nonpositive request leaves the
            // existing light unchanged; positive requests update immediately.
            if (const auto id=o.session.read().load.playerUnitId) if (const auto light=o.light({0,*id})) {
                const int64_t requested = 13 + int64_t(light->radiusModifier);
                if (requested > 0) lightRadius = int(std::min<int64_t>(18, requested));
            }
            v.lightRadius = lightRadius;
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
        bool submitStaff(EntityId source,std::optional<ItemHandle> item) override {
            if(source!=o.inventoryView.staffSource) return false;
            if(item) {const auto *value=o.inventoryView.item(item->id);if(!value || value->revision!=item->revision || value->definition!=o.data.staffRecipe.output) return false;}
            return o.session.submit_staff(uint32_t(source.value-1),item?std::optional{guid(item->id)}:std::nullopt,o.context);
        }
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
                if constexpr (std::is_same_v<T, EquipItem>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if(!item) return InventoryError::UnknownItem;
                    if(item->revision!=c.item.revision) return InventoryError::SourceChanged;
                    if(!c.slot) return InventoryError::None;
                    const auto *definition=o.data.items.find(item->definition);
                    if(!definition) return InventoryError::UnknownDefinition;
                    if(!item->identified) return InventoryError::Unidentified;
                    if(!definition->equipment.known || !definition->equipment.fits(*c.slot))
                        return InventoryError::UnsupportedEquipment;
                    const auto &required=definition->equipment.requiredClass;
                    if(!required.empty() && required!=o.characterView.classCode) return InventoryError::WrongClass;
                    return InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if(!o.hirelingView.active || !item || item->revision!=c.item.revision) return InventoryError::AccessDenied;
                    const auto *location=std::get_if<ContainerLocation>(&item->location);
                    const auto &containers=o.inventoryView.containers;
                    if(!location || (location->container!=containers.cursor && location->container!=containers.backpack &&
                        location->container!=containers.hirelingEquipment && location->container!=containers.equipment && location->container!=o.inventoryView.storage)) return InventoryError::AccessDenied;
                    if(c.slot) {
                        const auto *definition=o.data.items.find(item->definition);
                        const auto row=std::find_if(o.data.hirelings.begin(),o.data.hirelings.end(),[&](const auto &r){return r.classId==o.hirelingView.classId;});
                        if(!definition || row==o.data.hirelings.end()) return InventoryError::UnknownDefinition;
                        if(!item->identified) return InventoryError::Unidentified;
                        const auto &equipment=definition->equipment;
                        if((definition->maxDurability && !item->durability) || equipment.isType("ques")) return InventoryError::RestrictedItem;
                        const bool shield=row->act==3 && *c.slot==EquipmentSlot::LeftHand && equipment.isType("shld");
                        if(!equipment.fits(*c.slot) || (!shield && *c.slot!=EquipmentSlot::Head && *c.slot!=EquipmentSlot::Torso && *c.slot!=EquipmentSlot::RightHand)) return InventoryError::UnsupportedEquipment;
                        if(*c.slot==EquipmentSlot::RightHand && ((!equipment.isType(row->weaponType1) && (row->weaponType2.empty() || !equipment.isType(row->weaponType2))) || (row->act==3 && equipment.twoHanded))) return InventoryError::UnsupportedEquipment;
                    }
                    return InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, MergeStacks>) {
                    const auto *target=o.inventoryView.item(c.target.id);
                    if (!target) return InventoryError::UnknownItem;
                    const auto *base=o.data.items.find(target->definition);
                    const auto native=o.session.read().world.items.find(guid(target->id));
                    const auto decoded=o.items.read().items.find(guid(target->id));
                    if (!base || native==o.session.read().world.items.end() || decoded==o.items.read().items.end())
                        return InventoryError::UnknownItem;
                    const auto instance=o.completeItem(native->second,decoded->second,target->location);
                    if (!instance) return InventoryError::InvalidRequest;
                    int64_t maximum=base->maxStack;
                    for (const auto &stat : resolveItemStats(o.data,*instance,o.characterView.level))
                        if (!stat.layer && (stat.effect.empty()?stat.name:stat.effect)=="item_extra_stack") maximum+=stat.value;
                    // A full target has no native mutation/ACK. Keep the cursor
                    // usable instead of waiting for a response that cannot arrive.
                    return maximum>0 && target->quantity>=uint64_t(maximum)
                        ? InventoryError::StackFull : InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, LoadBook>) {
                    const auto *source=o.inventoryView.item(c.scroll.id), *target=o.inventoryView.item(c.book.id);
                    if (!source || !target) return InventoryError::UnknownItem;
                    if (source->revision!=c.scroll.revision || target->revision!=c.book.revision) return InventoryError::SourceChanged;
                    const auto *definition=o.inventoryView.definition(target->definition);
                    // Original 0x29 transfers a scroll into a tome, never tome-to-tome.
                    if (source->id==target->id || !definition || !definition->bookCapacity ||
                        definition->bookScroll!=source->definition || source->quality!=ItemQuality::Normal || target->quality!=ItemQuality::Normal)
                        return InventoryError::IncompatibleStack;
                    return target->charges>=definition->bookCapacity ? InventoryError::StackFull : InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, SplitStack>)
                    return InventoryError::InvalidRequest;
                else if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if (!item) return InventoryError::UnknownItem;
                    if (item->revision!=c.item.revision) return InventoryError::SourceChanged;
                    const auto *potion=o.data.potion(item->definition);
                    if (!potion || (potion->kind!=PotionKind::Healing && potion->kind!=PotionKind::Rejuvenation && potion->kind!=PotionKind::Remedy))
                        return InventoryError::UnsupportedUse;
                    const auto *where=std::get_if<ContainerLocation>(&item->location);
                    if (!o.hirelingView.active && (!where || where->container!=o.inventoryView.containers.belt))
                        return InventoryError::AccessDenied;
                    if (!where || (where->container!=o.inventoryView.containers.backpack &&
                        where->container!=o.inventoryView.containers.cursor && where->container!=o.inventoryView.containers.belt))
                        return InventoryError::AccessDenied;
                    return InventoryError::None;
                }
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
            const auto *base=o.inventoryView.definition(code);
            if (!grid || !base || !base->beltAllowed) return {};
            for (int x = 0; x < grid->columns; ++x) {
                const auto *bottom=o.inventoryView.item(o.inventoryView.itemAt(belt,{x,0}));
                const auto *other=bottom?o.inventoryView.definition(bottom->definition):nullptr;
                const bool compatible=other && (other->code==code || (!base->beltPotionType.empty() && other->beltPotionType==base->beltPotionType));
                if (!compatible) continue;
                for (int y = 0; y < grid->rows; ++y)
                    if (!o.inventoryView.itemAt(belt, {x,y})) return Cell{x,y};
            }
            if(base->autoBelt) for(int x=0;x<grid->columns;++x)
                if(!o.inventoryView.itemAt(belt,{x,0})) return Cell{x,0};
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
                    request.owner=v.owner;request.hotkeySlot = uint8_t(v.index); request.skill = uint16_t(std::max(0,v.skill));
                    request.hand = v.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
                    request.context = o.context;
                    if (o.combat.submit(request)) o.hotkeys[v.index] = SkillHotkey{v.skill,v.right,v.owner};
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
                    request.owner=v.owner;request.skill = uint16_t(std::max(0,v.skill));
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
        bool acknowledgeCompletion(QuestId quest) override {
            return o.session.acknowledge_quest_log(uint16_t(questDefinition(quest).nativeSlot),o.context.gameGeneration);
        }
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
                else if constexpr (std::is_same_v<T, OpenHirelingList>) {
                    if(!o.session.hireling_service(OnlineHirelingAction::List,{},o.context)) o.notice="Hireling list request is unavailable or pending.";
                    return;
                }
                else if constexpr (std::is_same_v<T, HireMercenary>) {
                    if(v.slot>UINT16_MAX || !o.session.hireling_service(OnlineHirelingAction::Hire,uint16_t(v.slot),o.context)) o.notice="Hireling offer changed or a request is pending.";
                    return;
                }
                else if constexpr (std::is_same_v<T, ResurrectHireling>) {
                    if(!o.session.hireling_service(OnlineHirelingAction::Resurrect,{},o.context)) o.notice="Hireling resurrection is unavailable or pending.";
                    return;
                }
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
                else if constexpr (std::is_same_v<T, CompleteActOne> || std::is_same_v<T, CompleteActTwo>) {
                    if(o.scene && o.scene->npcConversation && o.scene->npcConversation->travelDestination)
                        o.session.npc_travel(*o.scene->npcConversation->travelDestination,o.context);
                    return;
                }
                else if constexpr (std::is_same_v<T, ClaimAkaraRespec>) {o.session.npc_travel(0,o.context);return;}
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
                } else if constexpr (std::is_same_v<T, ImbueItem> || std::is_same_v<T, SocketQuestItem> || std::is_same_v<T, PersonalizeQuestItem>) {
                    request.action=OnlineItemAction::QuestService;request.item=guid(v.item.id);request.itemRevision=v.item.revision;
                    if(o.items.read().cursor!=request.item) {o.compositeTake(request);return;}
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
        void closeTravel() override {
            if (onlineInteractionMatches(o.context, o.session.read()) && o.session.use_waypoint(0, 0, o.context)) {
                // Only our successful close advances this frame's interaction.
                // The following click/shortcut must use the resulting context.
                o.context = onlineIntentContext(o.session.read());
                o.mapView.travelRequested = false;
            }
        }
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
        : session(s), items(i), combat(c), control(r), sharedData(sharedClassicData(a)), data(*sharedData) {}
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
                if (automatic->container == owned.cube) {
                    for (const auto &[id, cube] : inventoryView.items) {
                        const auto *base = inventoryView.definition(cube.definition);
                        const auto *at = std::get_if<ContainerLocation>(&cube.location);
                        if (!base || !base->opensCube || !at ||
                            (at->container != owned.backpack && at->container != inventoryView.storage)) continue;
                        next.action = OnlineItemAction::CubeStore; next.target = guid(id);
                        next.itemRevision = handle.revision; next.targetRevision = cube.revision;
                        if (items.read().cursor != next.item) compositeTake(next);
                        else enqueue(next);
                        return;
                    }
                    notice = "An accessible stored cube is required."; return;
                }
                auto cell = freeCell(automatic->container,*item);
                if (!cell) { notice = "No room in that container."; return; }
                target = {automatic->container,*cell};
            } else target = std::get<ContainerLocation>(destination);
            if (target.container == owned.cursor) {
                const auto *origin=std::get_if<ContainerLocation>(&item->location);
                next.action = origin && origin->container==owned.hirelingEquipment?OnlineItemAction::HirelingEquipment:OnlineItemAction::Take;
                if(next.action==OnlineItemAction::HirelingEquipment) next.body=nativeBody(EquipmentSlot(origin->cell.x));
                next.itemRevision = handle.revision; enqueue(next); return;
            }
            next.x = uint8_t(target.cell.x); next.y = uint8_t(target.cell.y);
            if (target.container == owned.belt) {
                const auto *origin=std::get_if<ContainerLocation>(&item->location);
                if(std::holds_alternative<AutoPlace>(destination) && origin && origin->container==owned.backpack && !items.read().cursor) {
                    next.action=OnlineItemAction::BeltAuto;next.itemRevision=handle.revision;enqueue(next);return;
                }
                next.action = OnlineItemAction::BeltPlace; next.beltSlot = uint8_t(target.cell.y*4+target.cell.x);
            } else if(target.container==owned.hirelingEquipment) {
                next.action=OnlineItemAction::HirelingEquipment;next.body=nativeBody(EquipmentSlot(target.cell.x));
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
            if(const auto *origin=std::get_if<ContainerLocation>(&item->location);origin && origin->container==owned.hirelingEquipment) {
                take.action=OnlineItemAction::HirelingEquipment;take.body=nativeBody(EquipmentSlot(origin->cell.x));
            }
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
            else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                if(v.slot) moveItem(v.item,ContainerLocation{owned.hirelingEquipment,{int(*v.slot),0}});
                else moveItem(v.item,v.destination.value_or(ItemDestination{ContainerLocation{owned.cursor,{}}}));
                return;
            }
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
                    }
                }
                c.action = OnlineItemAction::Use; c.item = guid(v.item.id); c.itemRevision = v.item.revision;
                if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    if (inventory.preview(v)!=InventoryError::None) {notice="Item cannot be used on this hireling.";return;}
                    c.mercenary=true;
                    const auto native=session.read().world.items.find(c.item);
                    if(native==session.read().world.items.end()) return;
                    if(native->second.mode!=2) {
                        c.action=OnlineItemAction::HirelingEquipment;c.body=0;
                        if(items.read().cursor!=c.item) {compositeTake(c);return;}
                    }
                }
            } else if constexpr (std::is_same_v<T, UseBeltColumn>) {
                if (const auto *item = inventoryView.item(inventoryView.itemAt(owned.belt,{v.column,0}))) {
                    if (v.hireling && inventory.preview(UseHirelingPotion{item->handle()})!=InventoryError::None) return;
                    c.action = OnlineItemAction::Use; c.item = guid(item->id); c.itemRevision = item->revision; c.mercenary = v.hireling;
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
            if ((c.action == OnlineItemAction::Socket || c.action == OnlineItemAction::Book) && items.read().cursor != c.item) { compositeTake(c); return; }
            enqueue(c);
        }, intent);
    }
    // Original private item packets carry the local equipment stat lists. Native NOEQUIP/BROKEN
    // flags and active body slots decide contributions; no server flavor or hidden remote stats.
    std::optional<int> knownEquipmentStat(std::string_view name, int level, std::optional<OnlineUnitKey> owner = {}) const {
        const auto &online=session.read(); const auto &world=online.world;
        if (!owner && online.load.playerUnitId) owner=OnlineUnitKey{0,*online.load.playerUnitId};
        if (!owner) return {};
        const auto actor=world.units.find(*owner);
        if (actor==world.units.end()) return {};
        const EquipmentStatKey key{online.gameGeneration,world.itemRevision,items.read().revision,
            actor->second.appearanceRevision,level,world.weaponSet,actor->second.equipmentObserved};
        const auto identity=std::pair{*owner,std::string(name)};
        if (const auto cached=equipmentStats.find(identity);cached!=equipmentStats.end() && cached->second.key==key)
            return cached->second.value;
        const auto value=calculateKnownEquipmentStat(name,level,*owner);
        equipmentStats.insert_or_assign(identity,CachedEquipmentStat{key,value});
        return value;
    }
    std::optional<int> calculateKnownEquipmentStat(std::string_view name, int level, OnlineUnitKey owner) const {
        const auto &online=session.read(); const auto &world=online.world;
        int64_t total=0;
        const auto actor = world.units.find(owner);
        if (actor == world.units.end() || !actor->second.equipmentObserved) return {};
        const auto statRow = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &entry) { return entry.name == name; });
        if (statRow == data.itemStats.end() || !statRow->id) return {};
        std::map<std::string, std::set<int32_t>, std::less<>> equippedSets;
        std::vector<ItemInstance> setPieces;
        for (const auto &[equippedId, equipped] : world.items) {
            if (equipped.ownerType != owner.type || equipped.owner != owner.id) continue;
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
            if(record->setAddFunction<0 || record->setAddFunction>2) return {};
            std::vector<int32_t> members;
            for(const auto &other:data.setItems) if(other.set==record->set) members.push_back(int32_t(other.row));
            const std::vector<int32_t> wornRows(worn.begin(),worn.end());
            const auto activeLayers=activeSetItemLayers(record->setAddFunction,int32_t(record->row),members,wornRows);
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
    std::optional<UnitLightAttributes> light(OnlineUnitKey key) const {
        const auto &online=session.read();
        const auto unit=online.world.units.find(key);
        if (unit==online.world.units.end() || key.type!=0) return {};
        const bool self=online.load.playerUnitId && key.id==*online.load.playerUnitId;
        const auto base=[&](std::string_view name) -> int32_t {
            if (self) return int32_t(stat(name).value_or(0));
            const auto id=data.nativeStats->names.find(name);
            if (id==data.nativeStats->names.end()) return 0;
            const auto value=unit->second.attributes.find(uint8_t(id->second));
            return value==unit->second.attributes.end()?0:
                value->second >> data.nativeStats->definitions.at(id->second).valueShift;
        };
        const auto states=combat.states().find(key);
        if (!unit->second.stateSequence || states==combat.states().end() || !states->second.decoded ||
            states->second.sequence!=unit->second.stateSequence) return {};
        const auto full=[&](std::string_view name) -> std::optional<int32_t> {
            const auto equipment=knownEquipmentStat(name,base("level"),key);
            if (!equipment) return {};
            const auto id=data.nativeStats->names.at(std::string(name));
            const auto shift=data.nativeStats->definitions.at(id).valueShift;
            int64_t total=int64_t(base(name))+*equipment;
            for (const auto &[state,list]:states->second.states) {
                (void)state;
                for (const auto &entry:list.stats)
                    if (entry.id==id && !entry.parameter) total+=entry.value >> shift;
            }
            if (total<INT32_MIN || total>INT32_MAX) return {};
            return int32_t(total);
        };
        const auto radius=full("item_lightradius"), color=full("item_lightcolor");
        if (!radius || !color) return {};
        return UnitLightAttributes{*radius,uint32_t(*color)};
    }
    void projectSkillUseFacts(CharacterProjectionInput &input) const {
        const auto &online = session.read();
        if (!online.load.playerUnitId) return;
        const OnlineUnitKey key{0, *online.load.playerUnitId};
        const auto actor = online.world.units.find(key);
        if (actor == online.world.units.end()) return;
        if (actor->second.equipmentObserved) {
            constexpr std::array<uint8_t, 2> bodies{4, 5};
            for (size_t hand = 0; hand < bodies.size(); ++hand) {
                const auto equipped = std::find_if(online.world.items.begin(), online.world.items.end(),
                    [&](const auto &entry) {
                        const auto &item = entry.second;
                        return item.ownerType == 0 && item.owner == key.id && item.mode == 1 && item.body == bodies[hand];
                    });
                if (equipped == online.world.items.end()) {
                    input.handEquipment[hand] = SkillHandEquipment{};
                    continue;
                }
                const auto &wire = equipped->second;
                const auto *base = data.items.find(wire.code);
                if (!base || !base->equipment.known) continue;
                SkillHandEquipment facts;
                facts.present = true; facts.unusable = (wire.flags & 0x4100u) != 0;
                facts.types = base->equipment.types;
                facts.ammunitionType = base->equipment.shoots;
                facts.stackable = base->maxStack > 1;
                const auto decoded = items.read().items.find(wire.id);
                if (decoded != items.read().items.end() && decoded->second.decoded && decoded->second.revision == wire.revision)
                    facts.quantity = decoded->second.quantity;
                input.handEquipment[hand] = std::move(facts);
            }
        }
        const auto observed = combat.states().find(key);
        const auto bloodMana = data.states.find("blood_mana"), inferno = data.states.find("inferno");
        if (actor->second.stateSequence && observed != combat.states().end() && observed->second.decoded &&
            observed->second.sequence == actor->second.stateSequence && bloodMana != data.states.end() && inferno != data.states.end())
            input.resourceStates = CharacterProjectionInput::ResourceStates{
                observed->second.states.contains(bloodMana->second.definition.id),
                observed->second.states.contains(inferno->second.definition.id)};
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
            else if (const auto &native = w.skillHotkeys[slot]; native && native->selection)
                input.hotkeys[slot] = {native->selection->skill ? int(native->selection->skill) : -1,
                    native->hand == OnlineSkillHand::Right,native->selection->owner};
        }
        input.baseRanks.insert(w.playerBaseSkills.begin(), w.playerBaseSkills.end());
        input.effectiveRanks.insert(w.playerSkills.begin(), w.playerSkills.end());
        input.itemSkillQuantities.insert(w.itemSkillQuantities.begin(), w.itemSkillQuantities.end());
        input.baseRanksAssigned = w.playerBaseSkillsAssigned;input.chargedSkills=nativeChargedSkills(online,items.read(),data.tables.at("setitems"));
        input.difficulty = online.load.difficulty;
        projectSkillUseFacts(input);
        if(const auto level=stat("level")) {
            if(const auto itemPierce=knownSelfEquipmentStat("item_pierce",int(*level));itemPierce && w.playerBaseSkillsAssigned) {
                int chance=*itemPierce;
                for(const auto &[skillId,record]:data.skills.skills) if(record.passiveContribution.amazon && record.passiveContribution.amazon->stat==AmazonPassiveStat::Pierce) {
                    const auto rank=w.playerSkills.find(uint16_t(skillId));if(rank!=w.playerSkills.end()) chance+=amazonPassiveValue(*record.passiveContribution.amazon,rank->second);
                }
                input.missilePierceChance=chance;
            }
            const auto ias=knownSelfEquipmentStat("item_fasterattackrate",int(*level));
            const auto fhr=knownSelfEquipmentStat("item_fastergethitrate",int(*level));
            const auto fbr=knownSelfEquipmentStat("item_fasterblockrate",int(*level));
            const auto fcr=knownSelfEquipmentStat("item_fastercastrate",int(*level));
            if(fcr && *fcr>-120) input.stats.insert_or_assign("item_fastercastrate",double(*fcr));
            if(fhr && fbr && *fhr>-120 && *fbr>-120 && online.load.playerUnitId) {
                const auto states=combat.states().find({0,*online.load.playerUnitId});
                if(states!=combat.states().end()) input.recoveryTiming=KnownRecoveryTiming{*fhr,*fbr,states->second.states.contains(data.states.at("holyshield").definition.id)};
            }
            const auto rate=knownSelfEquipmentStat("attackrate",int(*level));
            if(ias && rate && *ias>-120) for(const auto &[id,item]:w.items) {
                (void)id;
                if(item.ownerType!=0 || item.owner!=online.load.playerUnitId || item.mode!=1 || item.body!=4 || (item.flags&0x4100u)) continue;
                const auto *base=data.items.find(item.code);
                if(base && base->base.speed) input.attackTiming=KnownAttackTiming{*ias,*base->base.speed,*rate};
                break;
            }
        }
        if (const auto level=stat("level"); level && online.load.playerUnitId && data.skills.descriptions) {
            const auto &catalog=*data.skills.descriptions;
            const auto unit=w.units.find({0,*online.load.playerUnitId});
            const auto states=combat.states().find({0,*online.load.playerUnitId});
            // Native mastery IDs exceed the single-byte attribute packet.
            // Reconstruct the same read-only stat lists as D2Common: observed
            // equipment + observed states + Skills.passivecalc. If a passive
            // already carries that stat in its native state, count it once.
            for (const auto *name:{"passive_fire_mastery","passive_ltng_mastery","passive_cold_mastery","passive_pois_mastery"}) {
                if (const auto native=stat(name)) { input.stats.insert_or_assign(name,*native); continue; }
                const auto equipmentAndStates=knownSelfEquipmentStat(name,int(*level));
                if (!equipmentAndStates || !input.baseRanksAssigned || unit==w.units.end() || states==combat.states().end()) continue;
                const auto statId=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.name==name;});
                if (statId==data.itemStats.end() || !statId->id) continue;
                std::set<int> accounted;
                bool known=true;
                if (const auto passives=catalog.passives.find(name);passives!=catalog.passives.end())
                    for (const auto &passive:passives->second) {
                        const auto passiveState=data.states.find(passive.state);
                        if (passiveState==data.states.end()) { known=false; break; }
                        const auto observed=states->second.states.find(passiveState->second.definition.id);
                        if (observed!=states->second.states.end() && std::any_of(observed->second.stats.begin(),observed->second.stats.end(),
                            [&](const auto &entry){return entry.id==*statId->id && !entry.parameter;})) accounted.insert(passive.skill);
                    }
                if (!known) continue;
                SkillDescriptionInput description{0,{},input.baseRanks,input.effectiveRanks,input.stats};
                description.baseRanksComplete=input.baseRanksAssigned;
                const auto passive=projectDescriptionPassiveStat(catalog,description,name,accounted);
                if (!passive) continue;
                const int64_t total=int64_t(*equipmentAndStates)+*passive;
                if (total>=INT32_MIN && total<=INT32_MAX) input.stats.insert_or_assign(name,double(total));
            }
        }
        const auto knownMastery=[&](std::string_view name)->std::optional<int> {
            const auto found=input.stats.find(name);
            return found==input.stats.end()?std::nullopt:std::optional{int(found->second)};
        };
        input.fireMastery=knownMastery("passive_fire_mastery");
        input.lightningMastery=knownMastery("passive_ltng_mastery");
        input.coldDamagePercent=knownMastery("passive_cold_mastery");
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
        if(input.characterClass && *input.characterClass<data.characters.size() && online.load.playerUnitId &&
            stat("strength") && stat("dexterity") && stat("level") && input.baseRanksAssigned) {
            const auto &definition=data.characters[*input.characterClass];
            input.combatEquipment=observedCombatEquipment({0,*online.load.playerUnitId},
                {definition.code,int(*stat("strength")),int(*stat("dexterity")),int(*stat("level")),definition.blockFactor,weaponSet},
                input.effectiveRanks,0,definition.toHitFactor-35);
            if(input.combatEquipment) {
                const auto &equipment=*input.combatEquipment;
                const auto &weapon=equipment.equipment.weapons[0];
                input.attackTiming=KnownAttackTiming{weapon.fasterAttack,weapon.baseSpeed,equipment.modifiers.combat.attackRate,weapon.itemBaseSpeed,weapon.itemAttackSpeed,bool(weapon.item)};
            }
        }
        if(input.combatEquipment && stat("vitality") && stat("energy") && stat("maxhp") && stat("maxmana") && stat("maxstamina"))
            for(const auto *name:{"statpts","newskills","experience"}) input.stats.try_emplace(name,0);
        characterView = projectCharacterDisplay(data, input);
        characterView.selectedSkillOwners[weaponSet*2]=w.leftSkill?w.leftSkill->owner:UINT32_MAX;
        characterView.selectedSkillOwners[weaponSet*2+1]=w.rightSkill?w.rightSkill->owner:UINT32_MAX;
    }
    ItemInstance itemInstance(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
        const auto *definition=data.items.find(native.code);
            ItemInstance item; item.id=itemId(native.id); item.revision=native.revision; item.definition=native.code; item.location=location;
            item.quantity=di.gold.value_or(di.quantity.value_or(1)); item.charges=definition->bookCapacity?item.quantity:0; item.durability=di.durability.value_or(0);
            item.quality=itemQualityFromNative(di.quality).value(); item.identified=di.identified; item.level=di.level; item.defense=int(di.defense.value_or(0));
            item.nativeProperties=true; item.nativeFlags=native.flags; item.nativeMaxDurability=di.maxDurability.value_or(0);
            item.nativeFormat=di.format; item.nativeGraphic=di.graphic; item.nativeHasGraphic=di.hasGraphic; item.personalizedName=di.personalizedName;
            item.nativeAutoAffix=di.autoAffix;
            if (native.flags & 0x10000u) item.ear=ItemEarIdentity{di.earClass,di.earLevel,di.earName};
            if (item.quality==ItemQuality::Normal && definition->equipment.isType("body") &&
                !definition->equipment.isType("play")) item.nativeBodyPart=di.fileIndex;
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
                item.affixes.push_back({prefix,int32_t(index-1),{}});
                const auto &records=prefix?data.magicPrefixes:data.magicSuffixes;
                for (const auto &record:records) if(record.row==unsigned(index-1)) {
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
    std::optional<CombatDisplayEquipment> observedCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense=0,int rating=0,int minimum=0,int maximum=0) const {
        const auto &online=session.read();
        const auto unit=online.world.units.find(key);
        if(unit==online.world.units.end()) return {};
        const CombatEquipmentKey cacheKey{online.gameGeneration,online.world.itemRevision,items.read().revision,
            unit->second.appearanceRevision,unit->second.stateSequence,actor.characterClass,actor.strength,actor.dexterity,actor.level,
            actor.blockFactor,actor.weaponSet,actor.hireling,defense,rating,minimum,maximum,ranks};
        const auto cached=combatEquipment.find(key);
        if(cached!=combatEquipment.end() && cached->second.key==cacheKey) return cached->second.value;
        auto value=calculateCombatEquipment(key,std::move(actor),ranks,defense,rating,minimum,maximum);
        combatEquipment.insert_or_assign(key,CachedCombatEquipment{cacheKey,value});return value;
    }
    std::optional<CombatDisplayEquipment> calculateCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense,int rating,int minimum,int maximum) const {
        const auto &world=session.read().world;
        const auto unit=world.units.find(key),end=world.units.end();
        const auto observed=combat.states().find(key);
        if(unit==end || !unit->second.equipmentObserved || !unit->second.stateSequence ||
            observed==combat.states().end() || !observed->second.decoded || observed->second.sequence!=unit->second.stateSequence) return {};
        std::vector<CombatDisplayItem> equipped;std::vector<ItemInstance> charms;
        for(const auto &[id,wire]:world.items) {
            if(wire.ownerType!=key.type || wire.owner!=key.id || (wire.flags&0x4100u)) continue;
            const auto *base=data.items.find(wire.code);
            const bool worn=wire.mode==1 && wire.body>=1 && wire.body<=10;
            const bool charm=key.type==0 && wire.mode==0 && wire.page==1 && base && base->equipment.isType("char");
            if(!worn && !charm) continue;
            const auto decoded=items.read().items.find(id);
            if(!base || decoded==items.read().items.end() || !decoded->second.decoded || decoded->second.revision!=wire.revision) return {};
            auto item=completeItem(wire,decoded->second,SocketLocation{{},0});if(!item) return {};
            if(worn) {
                auto slot=EquipmentSlot(wire.body-1);
                if(actor.weaponSet && slot==EquipmentSlot::RightHand) slot=EquipmentSlot::AlternateRightHand;
                if(actor.weaponSet && slot==EquipmentSlot::LeftHand) slot=EquipmentSlot::AlternateLeftHand;
                equipped.push_back({slot,std::move(*item)});
            } else charms.push_back(std::move(*item));
        }
        std::vector<ResolvedItemStat> stateStats;std::set<int> states,accounted;
        for(const auto &[id,list]:observed->second.states) {
            states.insert(id);
            for(const auto &entry:list.stats) {
                const auto cost=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.id==entry.id;});
                if(cost==data.itemStats.end() || entry.value<INT32_MIN || entry.value>INT32_MAX) return {};
                stateStats.push_back({cost->name,cost->name,int(entry.value),int(entry.parameter),int(entry.value)});
            }
            if(data.skills.descriptions) for(const auto &[skill,source]:data.skills.descriptions->skills) {
                if(source.reference("passivestate")!=list.name) continue;
                bool carried=false;
                for(unsigned slot=1;slot<=5;++slot) {
                    const auto name=source.reference("passivestat"+std::to_string(slot));
                    const auto cost=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.name==name;});
                    if(cost!=data.itemStats.end() && std::any_of(list.stats.begin(),list.stats.end(),[&](const auto &entry){return cost->id==entry.id;})) carried=true;
                }
                if(carried) accounted.insert(skill);
            }
        }
        std::map<int,int> baseRanks;
        if(!actor.hireling) for(const auto &[id,rank]:session.read().world.playerBaseSkills) baseRanks.emplace(id,rank);
        int concentration=0;
        int smiteMinimum=0,smiteMaximum=0;
        for(const auto &[id,list]:observed->second.states) {
            (void)id;
            if(list.name=="concentration") for(const auto &entry:list.stats) for(const auto &cost:data.itemStats)
                if(cost.id==entry.id && cost.name=="damagepercent" && !entry.parameter) concentration=int(entry.value);
            if(list.name=="holyshield" && data.skills.descriptions) {
                int skill=-1,rank=0;
                for(const auto &entry:list.stats) for(const auto &cost:data.itemStats) if(cost.id==entry.id) {
                    if(cost.name=="modifierlist_skill") skill=int(entry.value);
                    if(cost.name=="modifierlist_level") rank=int(entry.value);
                }
                const auto source=data.skills.descriptions->skills.find(skill);
                if(rank>0 && source!=data.skills.descriptions->skills.end() && std::any_of(equipped.begin(),equipped.end(),[&](const auto &entry){
                    const auto *definition=data.items.find(entry.item.definition);return definition && definition->equipment.isType("shld");
                })) {
                    const std::map<std::string,double,std::less<>> values;
                    SkillDescriptionInput input{skill,rank,baseRanks,ranks,values};input.baseRanksComplete=true;
                    const auto bonus=SkillDescriptionValues(*data.skills.descriptions,input,source->second).parameter("clc1");
                    if(!bonus) return {};
                    stateStats.push_back({"item_armor_percent","item_armor_percent",*bonus,0,*bonus});
                    const auto smite=SkillDescriptionValues(*data.skills.descriptions,input,source->second).physicalDamage();
                    if(!smite.minimum || !smite.maximum) return {};
                    smiteMinimum=*smite.minimum;smiteMaximum=*smite.maximum;
                }
            }
        }
        auto result=projectCombatEquipment(data,std::move(actor),equipped,charms,stateStats,ranks,baseRanks,states,accounted,defense,rating,minimum,maximum);
        result.concentrationDamagePercent=concentration;
        result.modifiers.combat.smiteMinimum+=smiteMinimum;result.modifiers.combat.smiteMaximum+=smiteMaximum;
        return result;
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
            const auto &online = session.read();
            std::vector<int32_t> equippedSets;
            bool equipmentKnown = false;
            if (item.quality == ItemQuality::Set && online.load.playerUnitId) {
                const auto actor = online.world.units.find({0, *online.load.playerUnitId});
                equipmentKnown = actor != online.world.units.end() && actor->second.equipmentObserved;
                for (const auto &[id, equipped] : online.world.items) {
                    if (equipped.ownerType != 0 || equipped.owner != online.load.playerUnitId || equipped.mode != 1 || equipped.body > 10 || (equipped.flags & 0x4100u)) continue;
                    const auto decoded = items.read().items.find(id);
                    if (decoded == items.read().items.end() || !decoded->second.decoded || decoded->second.revision != equipped.revision) { equipmentKnown = false; continue; }
                    const auto piece = itemInstance(equipped, decoded->second, SocketLocation{{}, 0});
                    if (piece.identified && piece.quality == ItemQuality::Set && piece.specialRow >= 0 &&
                        std::find(equippedSets.begin(), equippedSets.end(), piece.specialRow) == equippedSets.end())
                        equippedSets.push_back(piece.specialRow);
                }
            }
            auto display=describeInventoryItem(data,data.items,item,{characterView.level,characterView.attributes[0],characterView.attributes[1],maximum,online.world.quests.cainStones.value_or(std::array<int,5>{}),characterView.classCode,equippedSets,equipmentKnown});
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
            if (complete && di.revision == native.revision && definition->maxStack > 1 && !definition->bookCapacity) {
                InventoryStackView stack;
                stack.fileIndex = di.fileIndex;
                constexpr std::array<std::string_view,6> names{"mindamage","maxdamage","secondary_mindamage",
                    "secondary_maxdamage","item_throw_mindamage","item_throw_maxdamage"};
                for (const auto &stat : stats) if (!stat.layer)
                    for (size_t index=0;index<names.size();++index)
                        if (stat.name == names[index]) {
                            if (stat.replace) stack.damage[index] = stat.rawValue;
                            else stack.damage[index] += stat.rawValue;
                        }
                projection.stack = stack;
            }
            projection.specialRow = item.specialRow;
            projection.iconCells = {di.width, di.height};
            projection.colorTransform = inventoryColorTransform(data, data.items, item);
            projection.groundColorTransform = groundColorTransform(data, data.items, item);
            if (definition->equipment.isType("gold"))
                projection.artFrame = item.quantity < 100 ? 0 : item.quantity < 500 ? 1 : item.quantity < 5000 ? 2 : 3;
            projection.socketContentsKnown = complete.has_value();
            if (complete)
                for (const auto &child : item.socketedItems) {
                    const auto detail = items.read().items.find(guid(child.id));
                    if (detail == items.read().items.end()) { projection.socketContentsKnown = false; break; }
                    auto art = detail->second.artKey;
                    if (child.identified && child.specialRow >= 0) {
                        const auto &records = child.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
                        for (const auto &record : records)
                            if (int32_t(record.row) == child.specialRow && !record.icon.empty()) art = record.icon;
                    }
                    projection.socketItems.push_back({std::move(art), inventoryColorTransform(data, data.items, child), child.nativeFlags});
                }
            if (complete && di.revision == native.revision)
                projection.cellTone = display.requirementsMet && item.identified && !(native.flags & 0x4104u) &&
                    (!definition->bookCapacity || item.charges) ? InventoryCellTone::Usable : InventoryCellTone::Unusable;
            const auto &source = data.tables.at(definition->base.sourceTable);
            int64_t indestructible = 0;
            for (const auto &stat : stats)
                if (stat.name == "item_indesctructible" && !stat.layer) indestructible += stat.rawValue;
            // D2Client 1.13c RVA 0x28680: HasDurability, non-stackable,
            // then current * 100 / resolved maximum, with integer truncation.
            if (complete && di.revision == native.revision && di.maxDurability && di.durability && maximum &&
                definition->maxDurability && !source.number(definition->base.sourceRow, "nodurability").value_or(0) &&
                !source.number(definition->base.sourceRow, "stackable").value_or(0) && indestructible <= 0)
                projection.durabilityPercentage = unsigned(std::min(uint64_t(*di.durability) * 100 / maximum, uint64_t{100}));
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
        if (const auto playerLayout = data.playerInventoryLayouts.find(characterView.classCode);
            playerLayout != data.playerInventoryLayouts.end()) {
            v.backpackLayout = layout(playerLayout->second.grid);
            v.equipmentSlots = playerLayout->second.slots;
            v.itemCellSize = {v.backpackLayout.cellSize, v.backpackLayout.cellSize};
        }
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
            const bool mercenary=w.hireling && native.ownerType==1 && native.owner==w.hireling->id && native.mode==1;
            if(!ground && !mercenary && (native.ownerType!=0 || native.owner!=online.load.playerUnitId)) continue;
            const auto d=decoded.items.find(id); const auto *definition=data.items.find(native.code);
            if(d==decoded.items.end() || !d->second.decoded || !definition) continue;
            const auto &di=d->second; ItemLocation location;
            if(ground && scene && scene->origin) location=GroundLocation{mapView.region,
                {float(int(native.groundX)-scene->origin->x),float(int(native.groundY)-scene->origin->y)}};
            else if(mercenary && (native.body==1 || native.body==3 || native.body==4 || native.body==5)) location=ContainerLocation{owned.hirelingEquipment,{int(native.body)-1,0}};
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
            def.autoBelt=definition->autoBelt;
            def.beltPotionType=beltPotionFamily(native.code);
            def.shield=definition->equipment.isType("shld"); def.quiver=!definition->equipment.quiver.empty();
            def.socketFiller=definition->equipment.isType("sock"); def.identifySource=data.isIdentifyScroll(native.code)||data.isIdentifyScroll(def.bookScroll);
            def.usable=data.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"useable").value_or(0)!=0;
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
        v.staffRevision=w.staffRevision;v.staffResult=w.staffResult;
        if(w.staffSource) {
            const auto object=w.units.find({2,*w.staffSource});
            if(object!=w.units.end() && object->second.classId) {
                const auto &table=data.tables.at("objects");
                for(size_t row=0;row<table.rows().size();++row) if(table.number(row,"Id")==*object->second.classId && table.number(row,"OperateFn")==25) v.staffSource=EntityId{(uint64_t{2}<<32)+*w.staffSource+1};
            }
        }
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
        if(scene && scene->origin && displayPosition)
            mapView.observer = *displayPosition - Vec{float(scene->origin->x), float(scene->origin->y)};
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
            if(d.type==2) {
                npcView.npc=EntityId{(uint64_t{2}<<32)+d.source+1};
                for(const auto &m:d.messages) if(!m.acknowledged && !m.text.empty()) {npcView.scrollMessage=m.stringId;npcView.introduction=m.text;break;}
            }
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
            auto menu=[&](std::string_view key,std::string fallback) {
                if(const auto found=data.itemStrings.find(key);found!=data.itemStrings.end()) return found->second;
                return fallback;
            };
            // Native servers send quest/intro text in 27; ordinary gossip is
            // local MPQ presentation and must not generate a quest ACK (31).
            if(d.type==1) for(size_t turn=0;;++turn) {
                const auto *speech=gossipSpeech(data.npcDialogues,d.speaker,turn,mapView.act);
                if(!speech || speech->text.empty()) break;
                std::string text=speech->text;
                if(const auto localized=data.itemStrings.find(speech->stringKey);localized!=data.itemStrings.end())
                    text=std::string(npcTblSpeech(localized->second));
                if(std::find(npcView.gossip.begin(),npcView.gossip.end(),text)!=npcView.gossip.end()) break;
                npcView.gossip.push_back(std::move(text));
            }
            if(!npcView.gossip.empty()) npcView.talkEntries.push_back({menu("NPCGossipMenu","Gossip"),{NpcMenuAction::Gossip,{}}});
            if(!npcView.textTopics.empty() || !npcView.gossip.empty()) {
                add(menu("TalkMenu","Talk"),NpcMenuAction::Talk);
                npcView.talkEntries.push_back({menu("Back","Back"),{NpcMenuAction::Back,{}}});
            }
            const auto identity=npcIdentity();
            if(identity=="kashya" || identity=="greiz" || identity=="asheara" || identity=="qual-kehk" || identity=="tyrael") {
                if(identity!="tyrael") add(menu("NPCMenuHire","Hire"),NpcMenuAction::Hire);
                if(w.deadHirelingName && w.hirelingReviveCost) add(menu("hireresurrect","Resurrect")+": " + std::to_string(*w.hirelingReviveCost),NpcMenuAction::Resurrect);
            }
            if(data.vendors.contains(identity) && identity!="nihlathak") add(menu("NPCMenuTrade","Trade"),NpcMenuAction::Trade);
            if(identity=="gheed" || identity=="elzix" || identity=="alkor" || identity=="jamella" || identity=="drehya" || identity=="nihlathak") add(menu("gamble","Gamble"),NpcMenuAction::Gamble);
            if(identity.starts_with("cain")) add(menu("NPCIdentify1","Identify Items"),NpcMenuAction::Identify);
            // Original S2C 0x28 personal quest records, never local reward
            // authority. SUnitNpc accepts the corresponding RewardPending bit.
            const auto rewardPending=[&](QuestId id) {
                if(!w.quests.playerFlags) return false;
                const auto flags=(*w.quests.playerFlags)[questDefinition(id).nativeSlot];
                return (flags&3u)==2u;
            };
            if(identity=="charsi" && characterView.level>=8 && rewardPending(QuestId::ToolsOfTheTrade))
                add(menu("Upgrade","Imbue"),NpcMenuAction::Imbue);
            if(identity=="larzuk" && rewardPending(QuestId::SiegeOnHarrogath)) {
                add(data.itemStrings.at("Addsocketsui"),NpcMenuAction::Socket);
                npcView.serviceHints.emplace(NpcMenuAction::Socket,data.itemStrings.at("Addsocketsui2"));
            }
            if(identity=="drehya" && rewardPending(QuestId::BetrayalOfHarrogath)) {
                add(data.itemStrings.at("Personalizeui"),NpcMenuAction::Personalize);
                npcView.serviceHints.emplace(NpcMenuAction::Personalize,data.itemStrings.at("Rename Instruct"));
            }
            if(!d.travelLabel.empty()) add(d.travelLabel,NpcMenuAction::GoEast);
            if(!d.respecLabel.empty()) add(d.respecLabel,NpcMenuAction::Respec);
            add(menu("strUiMenu1","Cancel"),NpcMenuAction::Cancel);
        }
        QuestProjectionInput questInput;
        questInput.revision=revision; questInput.actor=characterView.actor; questInput.currentAct=mapView.act;
        questInput.denRemaining=w.quests.denRemaining;
        questInput.staffTombOffset=w.quests.staffTombOffset;
        for (const auto &def:questDefinitions) {
            auto &entry=questInput.entries[questIndex(def.id)];
            entry.status=w.quests.statuses[def.nativeSlot];
            if (w.quests.playerFlags) entry.playerFlags=(*w.quests.playerFlags)[def.nativeSlot];
            if(w.quests.availability) {
                const auto found=std::find(nativeQuestInitializationSlots.begin(),nativeQuestInitializationSlots.end(),def.nativeSlot);
                if(found!=nativeQuestInitializationSlots.end()) entry.available=(*w.quests.availability)[size_t(found-nativeQuestInitializationSlots.begin())]!=0;
            }
        }
        questView=projectQuestDisplay(data,questInput);
        shopView={}; shopView.revision=revision; shopView.actor=characterView.actor; shopView.npc=npcView.npc;
        shopView.bankGold=inventoryView.bankGold; shopView.pricesKnown=false;
        shopView.tabLabels={data.itemStrings.at("strBSArmor"),data.itemStrings.at("strBSWeapons"),
            data.itemStrings.at("strBSWeapons"),data.itemStrings.at("strBSMisc")};
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
            if(native.page<1 || native.page>4) continue;
            offer.storePage=int(native.page)-1;offer.cell=std::array{int(native.x),int(native.y)};
            offer.tooltip=projection.tooltip;
            if (item.gamble) offer.tooltip={{projection.name,ItemTextTone::Name}};
            shopView.offers.push_back(std::move(offer));
            // Include read-only shelf art in the shared inventory art cache, not owned inventory occupancy.
            inventoryView.items.emplace(projection.id,std::move(projection));
        }
        shopView.pricesKnown = !shopView.offers.empty() && std::all_of(shopView.offers.begin(), shopView.offers.end(), [](const auto &offer) { return offer.priceKnown; });
        shopView.repairAllPrice = repairAllQuote();
    }
    void projectHireling() {
        hirelingView={};hirelingView.revision=revision;hirelingView.actor=characterView.actor;
        const auto &world=session.read().world;
        if (session.read().load.playerUnitId)
            hirelingView.portraits=projectPetPortraits(data,world,*session.read().load.playerUnitId);
        if(!world.hireling) return;
        const auto &merc=*world.hireling;
        hirelingView.id=npcId(merc.id);hirelingView.classId=merc.monsterClass;
        for(const auto &[key,index]:data.hirelingNameIds) if(index==int(merc.name)) {
            const auto name=data.hirelingStrings.find(key);
            if(name!=data.hirelingStrings.end()) hirelingView.name=name->second;
            break;
        }
        const auto value=[&](std::string_view name)->std::optional<int64_t> {
            const auto &table=data.tables.at("itemstatcost");
            for(size_t row=0;row<table.rows().size();++row) if(table.value(row,"Stat")==name) {
                const auto id=table.number(row,"ID");if(!id || *id<0 || *id>255) return {};
                const auto stat=merc.attributes.find(uint8_t(*id));if(stat==merc.attributes.end()) return {};
                return table.number(row,"Signed").value_or(0)?int64_t(int32_t(stat->second)):int64_t(stat->second);
            }
            return {};
        };
        const auto life=value("hitpoints"),maximum=value("maxhp"),level=value("level");
        hirelingView.known=life && maximum && level && *maximum>0;
        for(const auto *name:{"strength","dexterity","armorclass","mindamage","maxdamage","experience","nextexp","fireresist","coldresist","lightresist","poisonresist"})
            hirelingView.known=hirelingView.known && value(name).has_value();
        if(!hirelingView.known) return;
        const auto projected=observedCombatEquipment({1,merc.id},{"",int(*value("strength")),int(*value("dexterity")),int(*level),0,0,true},
            {},int(*value("armorclass")),0,int(*value("mindamage")),int(*value("maxdamage")));
        hirelingView.known=projected.has_value();if(!projected) return;
        const auto &mods=projected->modifiers;
        const auto &attack=projected->equipment.weapons[0];
        hirelingView.attackTiming=KnownAttackTiming{attack.fasterAttack,attack.baseSpeed,mods.combat.attackRate};
        hirelingView.recoveryTiming=KnownRecoveryTiming{mods.combat.fasterHitRecovery,mods.combat.fasterBlock,false};
        hirelingView.fasterCast=mods.combat.fasterCast;
        hirelingView.level=int(*level);hirelingView.life=float(*life)/256.f;
        hirelingView.maximumLife=std::max(1,int((int64_t(*maximum/256)+mods.maxLife)*(100+mods.combat.lifePercent)/100));
        const auto pet=world.pets.find(merc.id);
        hirelingView.active=*life>0 && !world.deadHirelingName && pet!=world.pets.end();
        hirelingView.strength=projected->actor.strength;hirelingView.dexterity=projected->actor.dexterity;
        hirelingView.defense=projected->equipment.defense;
        for(const auto &[id,item]:world.items) {
            if(item.ownerType!=1 || item.owner!=merc.id || item.mode!=1) continue;
            const auto decoded=items.read().items.find(id);
            if(decoded!=items.read().items.end() && decoded->second.decoded && decoded->second.defense.value_or(0)>0 &&
                (!decoded->second.maxDurability.value_or(0) || decoded->second.durability.value_or(0)>0)) hirelingView.defenseImproved=true;
        }
        if(const auto states=combat.states().find({1,merc.id});states!=combat.states().end() && states->second.decoded) {
            const auto &costs=data.tables.at("itemstatcost");
            for(size_t row=0;row<costs.rows().size();++row) if(costs.value(row,"Stat")=="skill_armor_percent")
                for(const auto &[state,list]:states->second.states) {
                    (void)state;
                    for(const auto &entry:list.stats) if(costs.number(row,"ID")==entry.id && entry.value>0) hirelingView.defenseImproved=true;
                }
        }
        auto weapon=projected->equipment.weapons[0];
        const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.classId==merc.monsterClass;});
        if(!weapon.item && definition!=data.hirelings.end() && definition->act==1) {
            const int percent=std::max(-90,projected->actor.dexterity+mods.combat.damagePercent+std::max(mods.combat.minimumDamagePercent,mods.combat.maximumDamagePercent));
            weapon.minimum=int(int64_t(weapon.meleeBaseMinimum)*(100+percent)/100);
            weapon.maximum=int(int64_t(weapon.meleeBaseMaximum)*(100+percent)/100);
        }
        const auto elements=attackElementRanges(mods.combat,weapon.item);
        WeaponModifiers own;if(const auto found=mods.combat.weapons.find(weapon.item);found!=mods.combat.weapons.end()) own=found->second;
        const int64_t frames=(int64_t(mods.combat.poisonFrames)+own.poisonFrames)/std::max(1,mods.combat.poisonSources+own.poisonSources);
        hirelingView.damageMinimum=int(int64_t(weapon.minimum)/256+elements.fire.minimum+elements.lightning.minimum+elements.cold.minimum+elements.magic.minimum+
            (int64_t(mods.combat.poisonMinimum)+own.poisonMinimum)*frames/256);
        hirelingView.damageMaximum=int(int64_t(weapon.maximum)/256+elements.fire.maximum+elements.lightning.maximum+elements.cold.maximum+elements.magic.maximum+
            (int64_t(mods.combat.poisonMaximum)+own.poisonMaximum)*frames/256);
        hirelingView.experience=uint64_t(std::max<int64_t>(0,*value("experience")));
        hirelingView.nextExperience=uint64_t(std::max<int64_t>(0,*value("nextexp")));
        const int baseResist[]={int(*value("fireresist")),int(*value("coldresist")),int(*value("lightresist")),int(*value("poisonresist"))};
        const int bonus[]={mods.fireResist,mods.coldResist,mods.lightningResist,mods.poisonResist};
        const int caps[]={mods.combat.fireMaxResist,mods.combat.coldMaxResist,mods.combat.lightningMaxResist,mods.combat.poisonMaxResist};
        for(size_t index=0;index<4;++index) hirelingView.resistances[index]=std::clamp(baseResist[index]+bonus[index],-100,std::clamp(75+caps[index],-100,95));
        const auto unit=world.units.find({1,merc.id});
        if(unit!=world.units.end() && unit->second.position && scene && scene->origin)
            hirelingView.position={float(unit->second.position->x)-scene->origin->x,float(unit->second.position->y)-scene->origin->y};
    }
    void projectHirelingList() {
        hirelingList={};hirelingList.revision=revision;hirelingList.actor=characterView.actor;
        hirelingList.gold=inventoryView.gold;
        const auto cancel=data.itemStrings.find("Cancel");
        hirelingList.cancelLabel=cancel==data.itemStrings.end()?"Cancel":cancel->second;
        const auto &online=session.read();const auto &world=online.world;const auto &service=world.hirelingService;
        if(!service.source || !service.listReceived || !world.npcConversation || service.source!=world.npcConversation->source ||
            service.interaction!=world.interactionGeneration || !online.load.difficulty) return;
        hirelingList.npc=npcId(*service.source);
        const auto npc=world.units.find({1,*service.source});
        if(npc==world.units.end() || !npc->second.classId) return;
        for(const auto &[name,seed]:service.offers) {
            const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.seller==*npc->second.classId && entry.difficulty==*online.load.difficulty+1;});
            if(definition==data.hirelings.end()) continue;
            const auto first=data.hirelingNameIds.find(definition->nameFirst),last=data.hirelingNameIds.find(definition->nameLast);
            if(first==data.hirelingNameIds.end() || last==data.hirelingNameIds.end() || name<first->second || name>last->second) continue;
            const auto offer=resolveHirelingOffer(data.hirelings,*npc->second.classId,*online.load.difficulty,characterView.level,seed,uint32_t(name-first->second+1));
            if(!offer) continue;
            const auto text=data.hirelingStrings.find(offer->nameKey);
            const auto row=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.sourceRow==offer->sourceRow;});
            std::string description;
            if(row!=data.hirelings.end()) if(const auto found=data.hirelingDescriptions.find(row->description);found!=data.hirelingDescriptions.end()) description=found->second;
            hirelingList.offers.push_back({name,text==data.hirelingStrings.end()?offer->nameKey:text->second,description,
                offer->level,offer->stats.life,offer->stats.defense,int(offer->stats.price)});
        }
    }
    void update(const OnlineSceneView &binding, std::optional<Vec> position) {
        scene=&binding;
        displayPosition = position;
        context = onlineIntentContext(session.read());
        if(generation!=session.read().gameGeneration) {
            generation=session.read().gameGeneration; transactions.clear(); waiting.reset(); waitingContext.reset(); hotkeys={};
            combatEquipment.clear();equipmentStats.clear();
        }
        std::erase_if(combatEquipment,[&](const auto &entry){return !session.read().world.units.contains(entry.first);});
        std::erase_if(equipmentStats,[&](const auto &entry){return !session.read().world.units.contains(entry.first.first);});
        if (areaGeneration!=session.read().world.areaGeneration) {
            areaGeneration=session.read().world.areaGeneration; transactions.clear(); waiting.reset(); waitingContext.reset();
        }
        ++revision; projectInteractions(); projectCharacter(); projectInventory(); projectInteractions();
        projectHirelingList();
        projectHireling();
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
void RemoteUiClients::update(const OnlineSceneView &s, std::optional<Vec> position){impl_->update(s, position);}
std::optional<UnitLightAttributes> RemoteUiClients::light(OnlineUnitKey key) const { return impl_->light(key); }
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
