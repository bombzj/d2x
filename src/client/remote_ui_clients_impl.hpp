#pragma once
// Private implementation of RemoteUiClients; included only by remote_ui_*.cpp.
#include "remote_ui_clients.hpp"
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
#include "gameplay/items/belt_rules.hpp"
#include "network/realm_session.hpp"
#include <algorithm>
#include <deque>
#include <cmath>
#include <chrono>
#include <set>
#include <utility>
#include <tuple>

namespace d2x {
namespace remote_ui_impl_detail {
// Containers and wire entities occupy disjoint namespaces. Native GUID zero is valid.
inline EntityId itemId(uint32_t id) { return {uint64_t(id) + 1}; }
inline uint32_t guid(EntityId id) { return uint32_t(id.value - 1); }
inline EntityId npcId(uint32_t id) { return {(uint64_t{1} << 32) + id + 1}; }
inline uint32_t npcGuid(EntityId id) { return uint32_t(id.value - (uint64_t{1} << 32) - 1); }
inline EntityId containerId(unsigned index) { return {(uint64_t{2} << 32) + index}; }
inline uint8_t nativeBody(EquipmentSlot slot) {
    if (slot == EquipmentSlot::AlternateRightHand) return 4;
    if (slot == EquipmentSlot::AlternateLeftHand) return 5;
    return uint8_t(slot) + 1;
}
}
using namespace remote_ui_impl_detail;
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
        ActorView controlledActor() const override;
        bool control(ActorControlIntent intent) override;
        bool move(MoveIntent intent) override;
        void stopMoving() override;
        void stopActions() override;
        void toggleRun() override;
        void respawn() override;
    } actor{*this};

    struct Inventory final : IInventoryClient {
        Impl &o; explicit Inventory(Impl &owner) : o(owner) {}
        const InventoryView &read() const override;
        bool submitStaff(EntityId source,std::optional<ItemHandle> item) override;
        InventoryError preview(const InventoryIntent &intent) const override;
        void submit(InventoryIntent c) override;
        std::optional<Cell> beltSpace(std::string_view code) const override;
    } inventory{*this};

    struct Character final : ICharacterClient {
        Impl &o; explicit Character(Impl &owner) : o(owner) {}
        const CharacterView &read() const override;
        void submit(CharacterIntent c) override;
    } character{*this};

    struct Quests final : IQuestClient {
        Impl &o; explicit Quests(Impl &owner) : o(owner) {}
        const QuestView &read() const override;
        bool acknowledgeCompletion(QuestId) override;
    } quests{*this};

    struct Npc final : INpcClient {
        Impl &o; explicit Npc(Impl &owner) : o(owner) {}
        const NpcConversationView &read(EntityId) const override;
        const NpcSceneView &scene() const override;
        const ShopView &shop(EntityId, bool) const override;
        const ShopOfferView *inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const override;
        std::optional<unsigned> quote(EntityId npc, ItemHandle item, bool repair) const override;
        bool canRequestSale(EntityId npc, ItemHandle item) const override;
        const HirelingView &hireling() const override;
        const HirelingListView &hirelings(EntityId) const override;
        void submit(NpcIntent c) override;
    } npc{*this};

    struct MapClient final : IMapClient {
        Impl &o; explicit MapClient(Impl &owner) : o(owner) {}
        const MapSceneView &read() const override;
        const TravelMenuView &travel(EntityId source, int act) const override;
        bool waypointSource(EntityId source) const override;
        void closeTravel() override;
        void submit(MapIntent intent) override;
    } map{*this};

    Impl(Archives &a, net::RealmSession &s, RemoteInventory &i, RemoteCombat &c, RemoteControl &r);
    std::optional<double> stat(std::string_view name) const;
    void queue(OnlineItemCommand c);
    void enqueue(OnlineItemCommand c);
    void pump();
    std::optional<Cell> freeCell(EntityId target, const InventoryItemView &item) const;
    void moveItem(ItemHandle handle, ItemDestination destination);
    void compositeTake(OnlineItemCommand next);
    void inventoryCommand(InventoryIntent intent);
    std::optional<int> knownEquipmentStat(std::string_view name, int level, std::optional<OnlineUnitKey> owner = {}) const;
    std::optional<int> calculateKnownEquipmentStat(std::string_view name, int level, OnlineUnitKey owner) const;
    std::optional<UnitLightAttributes> light(OnlineUnitKey key) const;
    std::optional<MovementTiming> movement(OnlineUnitKey key,bool running) const;
    std::optional<int> knownSelfEquipmentStat(std::string_view name,int level) const;
    void projectSkillUseFacts(CharacterProjectionInput &input) const;
    void projectCharacter();
    ItemInstance itemInstance(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const;
    std::optional<ItemInstance> completeItem(const OnlineItem &native, const OnlineDecodedItem &detail, ItemLocation location) const;
    std::optional<CombatDisplayEquipment> observedCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense=0,int rating=0,int minimum=0,int maximum=0) const;
    std::optional<CombatDisplayEquipment> calculateCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense,int rating,int minimum,int maximum) const;
    std::optional<unsigned> itemQuote(uint32_t id, OnlineItemAction action) const;
    InventoryItemView projectItem(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const;
    std::optional<unsigned> repairAllQuote() const;
    void itemNotice(const InventoryItemView &item, unsigned quantity, std::string prefix, bool gold = false);
    void projectItemFeedback(const InventoryView &current);
    void projectInventory();
    std::string npcIdentity() const;
    void projectInteractions();
    void projectHireling();
    void projectHirelingList();
    void update(const OnlineSceneView &binding, std::optional<Vec> position);
};
}
