#pragma once
#include "server/runtime/combat_rules.hpp"
#include "server/runtime/object_rules.hpp"
#include "server/runtime/npc_rules.hpp"
#include "server/runtime/contracts.hpp"
#include "world/navigation.hpp"
#include "world/native_field.hpp"
#include "world/identity.hpp"
#include "gameplay/monsters/components.hpp"
#include "gameplay/monsters/native_spawn.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include "gameplay/environment/time.hpp"
#include <map>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <array>
#include <compare>
#include <memory>
#include <optional>
#include <vector>
#include <functional>

namespace d2x {class PreparedRoomTerrain;}
namespace d2x::server {
namespace world { class System; }
struct RoomView {
    RegionId area;
    int x{}, y{}; // Global game-tile anchor, the original 07/08 identity.
    auto operator<=>(const RoomView &) const = default;
};
struct RoomPoint {RoomView room;Vec position;};
struct RoomRandomLoan {RoomView room;uint64_t areaGeneration{},revision{},expected{},next{};};
// Prepared collision values only. No Map, DT1, Archives or renderer handles.
struct AreaExit { EntityId id; RegionId destination; int warp{}, slot{}; Vec position, arrival; bool requiresQuest{}; Vec exitWalk; };
struct AreaBoundary { RegionId destination; int side{}, plane{}, start{}, end{}; };
struct AreaObject { EntityId id; int type{}; Vec position; ObjectRule rule; bool questSpawn{}; int preset{}; std::optional<size_t> presetOrder; };
struct PortalRule {
    int definition{},range{},openingTicks{};
    int width{},height{};uint16_t collisionMask{};std::array<bool,8> collision{};
};
struct TerrainCell { size_t index{}; uint8_t blocked{},light{},terrain{}; uint16_t full{}; };
struct AreaMetadata {
    int waypointIndex = -1;
    std::optional<Vec> waypointAnchor; // Native spawn marker, resolved against current collision at travel commit.
    std::optional<Vec> portalArrival;
    std::optional<Vec> questArrival; // Original quest object warp spawn (tile 0/11).
    std::optional<PortalRule> portalRule, specialPortalRule;
    std::map<int,PortalRule> questPortalRules;
    std::map<int,uint16_t> questObjectMessages;
    RegionId id = RegionId::Encampment;
    Vec spawn, origin;
    RegionId townRegion{};
    int act{};
    std::optional<int> staffTomb;
    std::vector<TerrainCell> openedTombWall;
    unsigned tombOpeningTicks{};
    bool town{}, teleportAllowed{};
    int teleportMode{};
    std::vector<AreaExit> exits;
    std::vector<AreaBoundary> boundaries;
    std::vector<AreaObject> objects;
    std::vector<AreaNpc> npcs;
    std::vector<int> objectDeferred; // Native preset callbacks awaiting object/quest authority.
    int monsterLevel{}; // Current MPQ area level for this difficulty/expansion.
};
struct AreaDefinition : AreaMetadata {
    std::shared_ptr<const PreparedRoomTerrain> roomTerrain;
    uint64_t initialLevelRandom{};
    bool fixedPosition{};
    bool saveMonsters{};
    int tileX{},tileY{},tileWidth{},tileHeight{};
    std::vector<NativeTileInfo> tileInfo;
    std::vector<uint8_t> zooCandidates;
    struct NativePresetMonster {
        size_t order{};
        int definition{-1},collisionObject{};
        Vec position;
        bool retry{};
        int retryAttempts{4},finalRetryAttempts{};
        MonsterRank rank{MonsterRank::Normal};
        std::string superUnique;
        int uniqueIndex{-1},minimum{},maximum{};
        std::array<int,3> modifiers{};
        bool automatic{},stacks{};
        bool randomElite{};
        int place{-1};
        bool group{},dead{};
        std::string deferred;
        std::vector<Vec> skillPositions;
        struct Extra {int definition{-1},minimum{},maximum{},attempts{};bool owned{},party{};};
        std::vector<Extra> extras;
        int questVariant{-1};
        bool prisonDoor{};
        std::optional<Vec> sourcePosition;
    };
    std::vector<NativePresetMonster> nativePresetMonsters;
    std::map<std::string,NativePresetMonster,std::less<>> nativeQuestMonsters;
    struct PopulationClass {
        int minimum{},maximum{},sparse{},partyMinimum{},partyMaximum{};
        std::array<int,2> minions{-1,-1};
        bool ownsParty{},singleGroup{},waterParty{},objectHut{},hostile{},figurineEligible{};
    };
    struct PopulationRules {
        int density{},uniqueMinimum{},uniqueMaximum{},warpDistance{};
        int wander{},wanderClass{-1},urnClass{-1},afterglowClass{-1};
        int chestBaseClass{-1},chestClass{-1},chestBirthTicks{};
        std::vector<MonsterPopulationChoice> roster;
        std::vector<int> uniques;
        std::map<int,PopulationClass> classes;
        std::vector<Vec> warpArrivals;
    } populationRules;
    std::shared_ptr<const CollisionField> collisionField;
    std::map<std::string,PreparedMonster,std::less<>> environmentMonsters;
    std::vector<std::string> populationRoster;
    std::map<int,ShrineRule> shrineRules;
    std::vector<ShrineGenerationEntry> shrineGeneration;
    ObjectPopulationRules objectPopulation;
    std::map<int,ObjectRule> objectRules;
    Grid collision;
    RoomLayout activation;
    std::vector<std::vector<RoomView>> roomNear; // Authored DRLG order, including cross-level links.
    std::map<std::string,NpcRule,std::less<>> questNpcs;
    std::map<int, MonsterComponentPalette> componentPalettes;
    std::vector<std::string> populationDeferred;
    std::vector<RoomPopulationLayout> populationRooms;
    std::vector<NativeLevelSpawnEntry> normalSpawnRoster;
    std::map<int,PreparedMonster> nativeMonsterClasses;
};
struct AreaView { AreaMetadata definition; uint64_t generation{}; std::vector<std::string> populationDeferred; size_t populationMissing{},populationPendingRooms{}; };
struct AreaState {
    AreaDefinition definition;
    uint64_t generation = 1;
    std::optional<uint64_t> levelRandom;
    struct RoomState {
        std::array<uint16_t, 4> references{};
        uint8_t status{4};
        bool loaded{};
        bool portalRetained{}; // Native HASPORTAL: ToggleHasPortalFlag(0) sets, (1) clears.
        std::optional<uint64_t> random;
        uint64_t randomRevision{};
        uint64_t creationOrder{};
    };
    std::vector<RoomState> rooms;
    std::vector<EntityId> unitHeads; // Native pUnitFirst, separate from room residency.
};
struct RoomUnitEntry {
    uint8_t kind{};
    std::optional<std::pair<RegionId,size_t>> room;
    EntityId previous,next;
};
class AreaStore;
struct RoomUnitRegistration {
  private:
    friend class AreaStore;
    std::map<EntityId,RoomUnitEntry>::node_type node;
    const AreaStore *publishedBy_{};
    EntityId publishedId_;
  public:
    RoomUnitRegistration()=default;
};
class AreaStore {
    friend class world::System;
    std::map<RegionId, AreaState> areas_;
    struct ActTime {NativeActEnvironment value;uint64_t revision{1};};
    std::array<std::optional<ActTime>,5> actTimes_;
    std::map<EntityId,RoomUnitEntry> roomUnits_;
    uint64_t roomCreationOrder_{};
    std::optional<std::pair<RegionId,size_t>> unitRoom(RegionId,Vec) const noexcept;
    void unlinkRoomUnit(EntityId,RoomUnitEntry &) noexcept;
    void linkRoomUnit(EntityId,RoomUnitEntry &) noexcept;
  public:
    std::optional<StatTimeContext> statTime(RegionId) const;
    std::optional<NativeActEnvironment> environment(RegionId) const;
    uint64_t environmentRevision(int act) const;
    static void validate(const AreaDefinition &initial);
    explicit AreaStore(AreaDefinition initial) {
        validate(initial);
        const auto id = initial.id;
        areas_.emplace(id, AreaState{std::move(initial), 1, {}});
        initializeUnitRooms(id);
    }
    const AreaState *find(RegionId id) const {
        const auto found = areas_.find(id); return found == areas_.end() ? nullptr : &found->second;
    }
    const auto &all() const { return areas_; }
    const AreaState &at(RegionId id) const { return areas_.at(id); }
    std::vector<std::pair<RegionId,size_t>> loadedRoomOrder() const;
    void initializeUnitRooms(RegionId);
    std::shared_ptr<RoomUnitRegistration> prepareRoomUnit(EntityId,uint8_t kind,RegionId,Vec,std::optional<RoomView> room={}) const;
    void publishRoomUnit(RoomUnitRegistration &) noexcept;
    bool validateRoomUnit(const RoomUnitRegistration &,EntityId) const;
    bool hasRoomUnit(EntityId id) const noexcept {return roomUnits_.contains(id);}
    void moveRoomUnit(EntityId,RegionId,Vec) noexcept;
    void removeRoomUnit(EntityId) noexcept;
    void setMissileMask(RegionId,Vec,int size,bool occupied) noexcept;
    std::vector<EntityId> roomUnitOrder(RoomView) const;
    std::optional<RoomView> roomAtPosition(RegionId source,Vec observer,int32_t worldX,int32_t worldY) const;
    using CollisionSample=std::function<uint16_t(RoomView,int32_t,int32_t)>;
    DomainResult<RoomRandomLoan> roomRandom(RoomView) const;
    bool validateRoomRandom(const RoomRandomLoan &) const noexcept;
    void publishRoomRandom(const RoomRandomLoan &) noexcept;
    uint16_t nativeCollisionMask(RoomView,int32_t worldX,int32_t worldY,int size,uint16_t mask,const CollisionSample &) const;
    std::optional<RoomPoint> nativeFreeCoordinates(RoomView,int32_t worldX,int32_t worldY,int size,uint16_t mask,
        const CollisionSample &,std::optional<std::pair<int32_t,int32_t>> field={},uint16_t fieldMask=0,bool fallback=false,int maxDistance=50,int increment=1) const;
    std::optional<RoomPoint> nativeFreeSpace(RoomView,int32_t worldX,int32_t worldY,int size,const CollisionSample &) const;
    std::optional<RoomPoint> nativeRestoreCoordinates(RoomView,int32_t worldX,int32_t worldY,int size,uint16_t mask,const CollisionSample &) const;
    std::optional<RoomPoint> nativeQuestFreePosition(RoomView,int32_t worldX,int32_t worldY,int size,uint16_t mask,int radius,const CollisionSample &) const;
    bool loaded(RegionId, Vec) const;
    bool active(RegionId, Vec) const;
    bool nearby(RegionId observerArea, Vec observer, RegionId targetArea, Vec target) const;
};
} // namespace d2x::server
