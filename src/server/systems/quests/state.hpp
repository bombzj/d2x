#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/quest/id.hpp"
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace d2x::server::quests {
// World state is per act and per game; personal progression stays in PlayerStore.
struct ActOneState {
    bool denCleared{};
    unsigned denRemaining{};
    std::set<PlayerId> eligible;
    std::map<PlayerId,unsigned> observed;
    std::array<int,5> stones{};
    unsigned activatedStones{};
    bool stonesOrdered{}, cainRescued{}, restoreCairnStones{}, cainPortalOpened{};
    bool andarielPortal{};
    uint64_t andarielPortalAt{};
};
struct ActTwoState {
    bool sunScheduled{}, dark{}, altarDestroyed{}, journalRead{}, tombOpen{}, durielSlain{}, tyraelPortal{};
    uint64_t sunAt{}, tombAt{};
    RegionId tomb{};
    EntityId radament{};
    Vec bookPosition;
    std::optional<uint16_t> tombOffset;
    std::map<PlayerId,std::tuple<RegionId,bool,uint64_t>> observed;
    std::map<PlayerId,EntityId> orifices;
    std::map<PlayerId,EntityId> journals;
    std::map<PlayerId,ActorContext> journalReads;
    std::set<PlayerId> radamentBooks;
};
struct Goal {
    uint32_t stage{};
    std::set<PlayerId> eligible;
};
struct ActThreeState {
    EntityId figurineSource{};
    RegionId figurineArea{};
    Vec figurinePosition;
    bool figurineDropped{};
    EntityId gidbinnAltar{};
    uint64_t gidbinnAt{};
    bool gidbinnSpawned{},gidbinnDropped{};
    bool flailDropped{},cubeDropped{},orbSmashed{},mephistoSlain{},soulstoneDropped{};
    unsigned orbHits{};
    uint64_t sewerAt{};
};
struct ActFourState {
    bool izualSlain{},ghostSpawned{},hammerDropped{},forgePlaced{},forgeSmashed{},diabloSlain{},diabloSpawned{},portalOpened{};
    Vec ghostPosition;
    unsigned forgeHits{};
    std::set<int> seals;
    uint64_t diabloAt{};
};
struct ActFiveState {
    bool anyaThawed{},anyaSpawned{},anyaPortal{},ancientsActive{},ancientsDefeated{},throneDeparted{},baalSlain{},tyraelSpawned{},exitPortal{};
    unsigned rescued{},wave{};
    uint64_t ancientAt{},waveAt{};
    bool wavePrepared{};
    std::set<PlayerId> ancientEligible;
    Vec baalPosition;
};
}
