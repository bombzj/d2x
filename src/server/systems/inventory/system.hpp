#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/intents.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "server/systems/transactions/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server { class MovementSystem; }
namespace d2x::server::inventory {
// Inventory access and placement plans; no second occupancy or item copy.
struct GroundTransfer { ItemHandle item; bool drop{}, cursor{}; };
using Intent = std::variant<GroundTransfer, MoveItem, TransferItem, SwapItems, SplitStack, MergeStacks,
    LoadBook, IdentifyItem, EquipBelt, EquipItem, EquipHirelingItem, UseItem,
    UseBeltColumn, UseHirelingPotion, SwitchWeaponSet, CloseStorage, GoldTransaction>;
enum class Source { Stored, Cursor, Belt };
enum class EquipmentMode { Insert, Indirect, Swap, TwoHanded };
struct Request {
    Intent intent;
    Source source = Source::Cursor;
    EquipmentMode equipmentMode = EquipmentMode::Insert;
    std::optional<unsigned> weaponSet{};
    std::vector<ItemHandle> equipmentGuards{};
};
bool supports(const Request &);
// Host-only decoder input; includes no property data or mutable domain references.
struct InputItem { ItemHandle handle; ContainerLocation location; };
struct InputState {
    PlayerContainers containers;
    unsigned weaponSet{};
    std::map<EntityId, InputItem> items;
};
struct Access { EntityId source; ContainerKind kind; uint64_t revision{}; RegionId area{}; std::optional<int> remoteRange{}; };
struct Pickup { ActorContext actor; GroundTransfer request; uint64_t locomotion{}; };
struct State { std::map<PlayerId, Access> storage; std::map<PlayerId, Pickup> pickups; };
struct Ports { const PlayerStore &players; items::System &items; transactions::System &transactions; const ItemCatalog *definitions; MovementSystem &movement; effects::System &effects; const AreaStore &areas; EventOutbox &events; travel::System &travel; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    std::optional<InputState> input(PlayerId) const;
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> close(PlayerId);
    DomainResult<> openStash(const ActorContext &, EntityId, std::optional<int> remoteRange = {});
    bool storageAccess(PlayerId) const;
    DomainResult<> storage(const ActorContext &, const Request &);
    DomainResult<> consume(const ActorContext &, const UseItem &, Source);
    DomainResult<transactions::Plan> weaponCost(const ActorContext &, const WeaponDamage &,
        const SkillCastSpec &, bool payMana, bool payAmmo, unsigned wear = 0) const;
    DomainResult<> ground(const ActorContext &, const GroundTransfer &, std::optional<SkillCastSpec> telekinesis = {});
    DomainResult<> telekinesis(const ActorContext &, EntityId, const SkillCastSpec &);
    std::optional<Vec> groundPosition(EntityId, RegionId) const;
    DomainResult<> identify(const ActorContext &, const IdentifyItem &);
    DomainResult<> dropGold(const ActorContext &, unsigned amount);
};
}
