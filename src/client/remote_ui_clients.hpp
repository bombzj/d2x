#pragma once
#include "client/actor_client.hpp"
#include "client/character_client.hpp"
#include "client/inventory_client.hpp"
#include "client/npc_client.hpp"
#include "client/quest_client.hpp"
#include "client/map_client.hpp"
#include "contracts/online_items.hpp"
#include "core/math.hpp"
#include "gameplay/actions/movement_timing.hpp"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <map>

namespace d2x {
class Archives;
struct ClassicData;
struct OnlineSceneView;
struct OnlineUnitKey;
struct UnitLightAttributes { int32_t radiusModifier{}; uint32_t color{}; };
struct Pd2PlayerRates { int velocityPercent{},attackRate{},itemIAS{}; };
class RemoteInventory;
class RemoteCombat;
class RemoteControl;
namespace net { class RealmSession; }
struct RemoteUiNotice { std::string text; bool error = false; };
// Shared presentation ports project server snapshots and submit native requests.
// Acceptance means queued by the client, not confirmed by the server.
class RemoteUiClients {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    RemoteUiClients(Archives &, net::RealmSession &, RemoteInventory &, RemoteCombat &, RemoteControl &, bool running = true);
    ~RemoteUiClients();
    void update(const OnlineSceneView &, std::optional<Vec> displayPosition = {});
    const ClassicData &content() const;
    const std::map<std::string,double,std::less<>> &skillFormulaStats() const;
    IActorClient &actor();
    IInventoryClient &inventory();
    ICharacterClient &character();
    const ICharacterClient &character() const;
    INpcClient &npc();
    IQuestClient &quests();
    IMapClient &map();
    bool running() const;
    bool busy() const;
    size_t queuedItemCommands() const;
    std::optional<uint64_t> waitingItemRequest() const;
    RemoteUiNotice takeNotice();
    std::optional<unsigned> itemQuote(uint32_t item, OnlineItemAction action) const;
    std::optional<UnitLightAttributes> light(OnlineUnitKey) const;
    std::optional<MovementTiming> movement(OnlineUnitKey,bool running) const;
    std::optional<KnownAttackTiming> attackTiming() const;
    std::optional<Pd2PlayerRates> pd2PlayerRates(OnlineUnitKey) const;
    std::optional<int32_t> pd2ObservedStat(OnlineUnitKey,std::string_view) const;
};
}
