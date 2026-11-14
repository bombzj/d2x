#pragma once
#include "world/navigation.hpp"
#include "world/navigation_planner.hpp"
#include "contracts/online_world.hpp"
#include "contracts/automap_discovery.hpp"
#include <map>
#include <array>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace d2x {
class RemoteTown;
struct OnlineView;
// Player knowledge, independent of room loading and of the movement executor.
// Coordinates are collision cells, never exploration objectives in their own right.
class DebugExplorationMemory {
  public:
    using Cell=std::pair<int,int>;
    // World DynamicPath position -> native integer feet (not grid-local centres).
    static Cell playerCell(Vec worldPosition);
    struct Passage {
        uint64_t id{},evidence{};
        bool units{}; // Known ground beyond the unit observation boundary.
        bool road{};
        Cell direction;
        std::set<Cell> boundary;
        std::vector<Cell> approaches;
        std::optional<uint64_t> deferred;
        Cell approachFor(Cell edge) const;
        Cell edgeFor(Cell approach) const;
    };
    struct Transport {
        uint16_t identity{};Cell source,from,to;
        bool enabled=true;
        bool operator==(const Transport &) const = default;
    };
    struct Area {
        int x{},y{},width{},height{}; // Storage bounds of observed data, not level bounds.
        using MaskBlock=std::array<uint16_t,32*32>;
        std::map<Cell,std::shared_ptr<const MaskBlock>> maskBlocks;
        uint16_t level{};
        std::map<Cell,uint16_t> terrainLevels; // Known page ownership, for area-local objectives only.
        std::set<Cell> unitObserved; // Entered, subscribed room footprints, in native tiles; not assignment proof.
        std::set<Cell> paths,pathChecked;
        bool followPaths{};
        uint64_t revision{},nextPassage{1},sample{},geometry{};
        std::vector<Passage> passages;
        std::map<Cell,std::set<Cell>> walked;
        std::vector<Transport> transports; // Directed links established by an actual own-unit relocation.
        std::optional<Cell> previous;
        uint64_t discontinuity{};
        std::optional<Cell> heading;
        std::optional<uint64_t> commitment;
        std::optional<std::array<int,4>> expectedBounds; // Configured experience, never collision or storage bounds.
        std::shared_ptr<const AutomapSketch> sketch;
        uint16_t mask(int globalX,int globalY) const;
        std::vector<uint16_t> flatMasks() const;
        bool clear(int globalX,int globalY) const;
        bool stand(int globalX,int globalY) const;
        Passage *passage(uint64_t);
        const Passage *passage(uint64_t) const;
    };
    struct PlanProfile {
        uint64_t calls{},structuralCalls{},doorBypassCalls{},storageCells{},expanded{},landingCandidates{},castCandidates{};
        double prepareMs{},landingsMs{},searchMs{},finishMs{};
    };
    struct Plan {
        // Partial advances toward a known objective through resident cast
        // pages; its endpoint does not complete that objective or passage.
        enum class Outcome {Unreachable, StartUnavailable, ExecutionBlocked, Goal, Passage, Partial} outcome=Outcome::Unreachable;
        std::vector<OnlinePoint> points;
        std::optional<OnlinePoint> objective; // Final known endpoint behind a Partial route.
        uint64_t passage{},evidence{};
        size_t expanded{};
        float cost{};
        bool backtracking{},doorFallback{};
        std::optional<Transport> transport; // First operation on this route; never a walk/cast edge.
        PlanProfile profile;
    };
    // Resident terrain/object constraints and cast adjacency. Unit occupancy
    // is checked only by the local executor, never by a long-route query.
    struct Execution {
        std::shared_ptr<const NavigationSnapshot> collision;
        std::shared_ptr<const Grid> collisionGrid;
        std::shared_ptr<const NavigationSnapshot> landingCollision;
        std::shared_ptr<const Grid> landingGrid; // Real collision, including closed doors.
        std::vector<NavigationRoom> rooms; // Acquired pages, used only for legal cast edges.
        bool requiresDoor(Cell) const;
        bool landingClear(Cell) const;
        bool landingAllowed(Cell) const;
        bool castRayAllowed(Cell from,Cell to,bool restricted) const;
        bool castAllowed(Cell from,Cell to,bool restricted) const;
    };
    Area *observe(const RemoteTown &,const OnlineView &,const Grid &planning,Vec position);
    Area *area(uint16_t);
    struct Metrics {
        uint64_t sampledCells{},reusedTiles{},boundaryCells{},copiedMaskBytes{},observations{};
        double sampleMs{},boundaryMs{},observationMs{},maxObservationMs{};
    } metrics;
    const std::map<uint16_t,Area> &areas() const {return areas_;}
    // Both modes select the same passages. Teleport adds observed landing edges,
    // with no walking requirement between them; execution revalidates each cast.
    static Plan plan(const Area &,Vec from,int teleportRange=0,bool restricted=false,
        std::optional<Vec> goal={},float vicinity=2,uint64_t committed=0,bool exploreGoal=false,
        const std::optional<std::vector<Cell>> &approaches={},const Execution *execution=nullptr,bool preferDoorBypass=true,
        std::optional<Cell> preferredDirection={},PlanProfile *profile=nullptr);
    static void defer(Area &,uint64_t passage);
    static std::vector<Cell> teleportReturnAnchors(const Area &,Cell from,int range);
    struct RouteRejection {
        const char *reason="none";
        size_t point{};
        Cell from{},to{};
    };
    static bool routeValid(const Area &,const Plan &,int range,bool restricted,const Execution *,RouteRejection *rejection=nullptr);
  private:
    uint64_t game_{};
    std::optional<uint16_t> current_;
    std::map<uint16_t,Area> areas_;
    struct Observation {
        std::shared_ptr<const NavigationSnapshot> source;
        std::array<std::set<Cell>,8> edges;
        std::set<std::tuple<uint16_t,int,int,int,int>> unitRooms;
    };
    std::map<uint16_t,Observation> observations_;
    static void rebuildPassages(Area &,Observation &,const std::set<Cell> &dirty);
    static void rebuildPathPassages(Area &);
};
}
