#pragma once
#include "client/actor_client.hpp"
#include "client/character_client.hpp"
#include "client/inventory_client.hpp"
#include "client/npc_client.hpp"
#include "client/quest_client.hpp"
#include "client/map_client.hpp"
#include <memory>

namespace d2x {
class Archives;
struct ClassicData;
struct OnlineSceneView;
class RemoteInventory;
class RemoteCombat;
class RemoteControl;
namespace net { class RealmSession; }
// The existing gameplay UI consumes these same ports in both modes. This
// adapter owns snapshots and sends native requests; it never simulates a save.
class RemoteUiClients {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    RemoteUiClients(Archives &, net::RealmSession &, RemoteInventory &, RemoteCombat &, RemoteControl &);
    ~RemoteUiClients();
    void update(const OnlineSceneView &);
    const ClassicData &content() const;
    IActorClient &actor();
    IInventoryClient &inventory();
    ICharacterClient &character();
    INpcClient &npc();
    IQuestClient &quests();
    IMapClient &map();
    bool running() const;
    bool busy() const;
    std::string takeNotice();
    void openShop();
};
}
