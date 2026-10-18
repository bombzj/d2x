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
    bool denHasKills{};
    unsigned denRemaining{};
    std::map<PlayerId,EntityId> contributors;
    std::map<PlayerId,unsigned> observed;
    std::array<int,5> stones{};
    unsigned activatedStones{};
    EntityId cainInvisibleObject;
    EntityId cainStartObject;
    EntityId cainTownActor;
    struct CainStart {RegionId area;Vec position;};
    std::optional<CainStart> cainStart;
    std::array<EntityId,5> monoliths{};
    bool stonesOrdered{}, cainRescued{}, restoreCairnStones{}, cainPortalOpened{}, cainOffer{}, burialOffer{}, toolsOffer{};
    bool andarielPortal{};
    uint64_t andarielPortalAt{};
    struct CairnControl {EntityId owner;RegionId area;Vec position;};
    std::optional<CairnControl> cairnPending;
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
    std::set<EntityId> radamentFxStarted;
};
struct Goal {
    uint32_t stage{};
    std::set<PlayerId> eligible;
    std::map<PlayerId,EntityId> actors;
    std::map<PlayerId,uint64_t> experience;
    bool attributeReward{}, consumeForgeItems{}, consumeKhalimItems{}, primaryGoal=true;
};
struct ActThreeState {
    EntityId figurineSource{};
    RegionId figurineArea{};
    Vec figurinePosition;
    bool figurineDropped{};
    bool figurineEnabled{true};
    EntityId gidbinnAltar{};
    uint64_t gidbinnAt{};
    bool gidbinnSpawned{},gidbinnDropped{};
    bool flailDropped{},cubeDropped{},orbSmashed{},mephistoSlain{},soulstoneDropped{};
    unsigned orbHits{};
    uint64_t sewerAt{};
    bool orbSequenceStarted{},orbFxPending{};
};
struct ActFourState {
    bool izualActivated{};
    bool izualSlain{},ghostSpawned{},hammerDropped{},forgePlaced{},forgeSmashed{},diabloSlain{},diabloSpawned{},portalOpened{};
    bool sanctumCleared{};
    Vec ghostPosition;
    unsigned forgeHits{};
    std::set<int> seals;
    std::map<EntityId,std::string> sealAfterglows;
    uint64_t diabloAt{};
};
struct ActFiveState {
    bool siegeBossActivated{};
    bool anyaThawed{},anyaSpawned{},anyaPortal{},ancientsActive{},ancientsDefeated{},throneDeparted{},baalSlain{},tyraelSpawned{},exitPortal{};
    unsigned rescued{},wave{};
    uint64_t ancientAt{},waveAt{};
    bool wavePrepared{};
    Vec baalPosition;
    EntityId baalFxSource;
    bool baalFxStarted{};
    std::optional<uint16_t> zooMonster;
};
}
