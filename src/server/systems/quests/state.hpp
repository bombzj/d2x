#pragma once
#include "server/runtime/contracts.hpp"
#include "server/area_store.hpp"
#include "gameplay/quest/id.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/forge_rewards.hpp"
#include <memory>
#include <optional>
#include <string>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace d2x::server::quests {
struct QuestCompletion {
    std::map<PlayerId,EntityId> recipients;
    std::set<PlayerId> soundRecipients;
};
// World state is per act and per game; personal progression stays in PlayerStore.
struct ActOneState {
    bool denCleared{};
    using DenCompletion=QuestCompletion;
    std::optional<DenCompletion> denCompletion;
    bool denLeftTown{}, denEntered{};
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
    bool radamentActivated{},summonerActivated{};
    std::array<ActTwoQuestRuntime,6> runtime{};
};
struct Goal {
    uint32_t stage{};
    uint32_t flags{};
    std::set<PlayerId> eligible;
    std::map<PlayerId,EntityId> actors;
    std::map<PlayerId,uint64_t> experience;
    bool attributeReward{}, consumeForgeItems{}, consumeKhalimItems{}, primaryGoal=true, notifyQuest=true;
    uint16_t nativeSet{},nativeClear{};
};
struct StatusNotice {
    uint8_t status{};std::optional<EntityId> deactivateNpc{};
    struct Recipient {PlayerId player;EntityId actor;RegionId area;uint8_t status{};};
    std::vector<Recipient> recipients;
    uint16_t counter{};
    bool prepared{};
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
    std::optional<QuestCompletion> sanctumCompletion;
    struct SanctumSweep {
        std::vector<RoomView> rooms;
        size_t room{},unit{};
        std::vector<EntityId> units;
        bool prepared{};
        EntityId pendingDeath;
    };
    std::optional<SanctumSweep> sanctumSweep;
    Vec ghostPosition;
    unsigned forgeHits{};
    EntityId forgeObject;
    std::shared_ptr<const ForgeRewardRules> forgeRewards;
    uint64_t forgeOpenAt{},forgeDue{};
    unsigned forgePlayers{},forgeTier{},forgeDropIndex{},forgeSuccessful{};
    bool forgeAwaiting{},forgeFxPending{},forgeStarted{};
    std::string forgeCode;
    std::set<int> seals;
    std::map<EntityId,std::string> sealAfterglows;
    uint64_t diabloAt{};
    uint32_t diabloTimerCalls{};
    bool diabloBirthPending{};
};
struct ActFiveState {
    bool siegeBossActivated{};
    bool nihlathakActivated{};
    bool nihlathakSlain{};
    std::optional<QuestCompletion> nihlathakCompletion;
    std::optional<QuestCompletion> anyaCompletion;
    bool anyaThawed{},anyaSpawned{},anyaPortal{},ancientsActive{},ancientsDefeated{},throneDeparted{},baalSlain{},tyraelSpawned{},exitPortal{};
    unsigned rescued{};
    unsigned rescueSpawned{},rescueKilled{};
    unsigned rescueRemaining{15};
    std::set<EntityId> rescueDeaths;
    std::set<EntityId> rescueFreed,rescueDoors;
    bool rescueGroupsComplete{};
    uint8_t rescueLastStatus{};
    std::optional<PlayerId> rescuePlayer;
    struct RescuePortal {
        Vec group;
        EntityId object;
        std::set<EntityId> prisoners;
        uint64_t due{};
        unsigned closingUpdates{};
    };
    std::vector<RescuePortal> rescuePortals;
    uint64_t ancientAt{};
    struct Ancient {EntityId statue,monster;uint64_t birthAt{},animationEnd{};bool spawned{},dead{},restorePending{},deathFx{},deathFxMissile{};};
    std::array<Ancient,3> ancients;
    EntityId ancientAltar;
    struct AncientReader {EntityId actor,altar;uint64_t generation{};};
    std::map<PlayerId,AncientReader> ancientAltarReaders;
    uint64_t ancientRestoreAt{},ancientAltarEnd{};
    Vec baalPosition;
    EntityId baalFxSource;
    bool baalFxStarted{};
    std::optional<uint16_t> zooMonster;
};
}
