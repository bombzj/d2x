#include "ai_impl.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include "world/preset_identity.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::prepareVaughtClues() {
    auto &clues=vaughtClues;if(clues.prepared) return;
    clues.prepared=true;
    try {
        clues.level=definitions->area("Act 5 - Temple Boss");
        clues.monster=definitions->selector(Json{{"monster","nihlathakboss"}}).identity;
        const DataTable presets(archives.read("data/global/excel/lvlprest.txt"));
        std::optional<size_t> preset;
        for(size_t row=0;row<presets.rows().size();++row) if(presets.number(row,"LevelId")==clues.level) {
            if(preset) throw std::runtime_error("Multiple Vaught level presets");
            preset=row;
        }
        if(!preset || presets.number(*preset,"Files")!=4)
            throw std::runtime_error("Vaught requires the four authored branch templates");
        std::vector<MapData> maps;
        for(int file=1;file<=4;++file) {
            const auto name=presets.value(*preset,"File"+std::to_string(file));
            if(name.empty() || name=="0") throw std::runtime_error("Missing Vaught template");
            const auto path="data/global/tiles/"+std::string(name);
            maps.push_back(decodeDs1(archives.read(path),path));
            const auto &map=maps.back();
            if(map.act!=4 || map.width<9 || map.height<9 || map.dependencies.size()!=1 ||
                !map.dependencies.front().ends_with("\\expansion\\wildtemple\\interior.dt1"))
                throw std::runtime_error("Unrecognized Vaught mural library or extent");
            if(file==1) {clues.width=map.width;clues.height=map.height;}
            else if(map.width!=clues.width || map.height!=clues.height)
                throw std::runtime_error("Vaught templates have different extents");
        }
        const Vec center{float(clues.width-1)*.5f,float(clues.height-1)*.5f};
        std::set<std::pair<int,int>> directions;
        for(size_t variant=0;variant<maps.size();++variant) {
            const auto &map=maps[variant];
            std::vector<VaughtClues::Branch> candidates;
            for(const auto &layer:map.walls) for(int y=0;y<map.height;++y) for(int x=0;x<map.width;++x) {
                const auto &first=layer[size_t(y)*map.width+x];
                // Original interior.dt1's two coloured mural panels. Their
                // placement, rather than the reusable art identity alone, is
                // the clue. Only a unique pair near the central junction counts.
                if(!first.present() || ((first.value>>20)&63)!=0 || ((first.value>>8)&255)!=1 ||
                    (first.orientation!=1 && first.orientation!=2) ||
                    std::abs(x-center.x)>map.width*.25f || std::abs(y-center.y)>map.height*.25f) continue;
                const int nx=x+(first.orientation==2),ny=y+(first.orientation==1);
                if(nx>=map.width || ny>=map.height) continue;
                const auto &second=layer[size_t(ny)*map.width+nx];
                if(!second.present() || second.key()!=uint32_t((2<<8)|first.orientation)) continue;
                bool unique=true;
                for(size_t other=0;other<maps.size();++other) if(other!=variant)
                    for(const auto &wall:maps[other].walls) {
                        const auto &a=wall[size_t(y)*map.width+x],&b=wall[size_t(ny)*map.width+nx];
                        if(a.present() && b.present() && a.key()==first.key() && b.key()==second.key()) unique=false;
                    }
                if(!unique) continue;
                VaughtClues::Branch branch;
                branch.walls={{x,y,first.key()},{nx,ny,second.key()}};
                const Vec delta{(x+nx)*.5f-center.x,(y+ny)*.5f-center.y};
                branch.direction=std::abs(delta.x)>std::abs(delta.y)?Vec{delta.x>0?1.f:-1.f,0}:
                    Vec{0,delta.y>0?1.f:-1.f};
                branch.observe=center+Vec{.5f,.5f}+branch.direction*
                    std::max(0.f,std::abs(delta.x*branch.direction.x+delta.y*branch.direction.y)-3.f);
                candidates.push_back(std::move(branch));
            }
            if(candidates.size()!=1) throw std::runtime_error("Vaught central mural pair is missing or ambiguous");
            auto branch=std::move(candidates.front());
            if(!directions.emplace(int(branch.direction.x),int(branch.direction.y)).second)
                throw std::runtime_error("Vaught mural templates do not cover four distinct arms");
            // Validate the authored association, never inspect this game's
            // selected preset or hidden units. InitFn 69 is D2MOO's
            // OBJECTS_InitFunction69_NihlathakStartOutsideTown.
            unsigned markers=0;
            for(const auto &object:map.objects) if(object.type==2) {
                const int id=originalObjectClass(object,map.version,map.act);
                for(size_t row=0;row<definitions->objects.rows().size();++row)
                    if(definitions->objects.number(row,"Id")==id && definitions->objects.number(row,"InitFn")==69) {
                        const Vec delta{object.x/5.f-center.x,object.y/5.f-center.y};
                        const float along=delta.x*branch.direction.x+delta.y*branch.direction.y;
                        const float across=std::abs(delta.x*branch.direction.y-delta.y*branch.direction.x);
                        if(along<=std::max(map.width,map.height)*.25f || across>=along)
                            throw std::runtime_error("Vaught mural and authored Nihlathak arm disagree");
                        ++markers;
                    }
            }
            if(markers!=1) throw std::runtime_error("Vaught Nihlathak marker is missing or ambiguous");
            float farthest=0;
            for(const auto &floor:map.floors) for(int y=0;y<map.height;++y) for(int x=0;x<map.width;++x) {
                if(!floor[size_t(y)*map.width+x].present()) continue;
                const Vec delta{float(x)-center.x,float(y)-center.y};
                if(std::abs(delta.x*branch.direction.y-delta.y*branch.direction.x)>1) continue;
                farthest=std::max(farthest,delta.x*branch.direction.x+delta.y*branch.direction.y);
            }
            if(farthest<=std::max(map.width,map.height)*.25f)
                throw std::runtime_error("Vaught arm has no authored floor extent");
            // Aim inside the far chamber, two tiles off its end wall. This is
            // a terrain approach; actual unit assignment still locates the boss.
            branch.end=center+Vec{.5f,.5f}+branch.direction*(farthest-2);
            clues.branches.push_back(std::move(branch));
        }
    } catch(const std::exception &error) {
        clues.branches.clear();clues.unavailable=error.what();
        note("terrain-clue-unavailable",clues.unavailable);
    }
}

bool DebugAi::Impl::approachTerrainClue(Context &c,const Selector &selector) {
    if(!area || selector.type!=1 || *area!=definitions->area("Act 5 - Temple Boss")) return false;
    prepareVaughtClues();const auto &clues=vaughtClues;
    if(selector.identity!=clues.monster) return false;
    terrainClueDiagnostics={{"kind","vaught-mural"},{"area",*area}};
    if(clues.branches.empty()) {terrainClueDiagnostics["status"]=clues.unavailable;return false;}
    const auto observation=c.town.navigationObservation();const auto &view=c.session.read();
    if(!observation || !view.load.mapSeed || observation->width!=clues.width || observation->height!=clues.height) {
        terrainClueDiagnostics["status"]="unavailable-observation-or-extent";return false;
    }
    auto &run=vaughtSearch;
    if(run.game!=view.gameGeneration || run.seed!=*view.load.mapSeed || run.objectSeed!=view.load.secondarySeed.value_or(0) ||
        run.x!=observation->tileX || run.y!=observation->tileY) {
        run={};run.game=view.gameGeneration;run.seed=*view.load.mapSeed;run.objectSeed=view.load.secondarySeed.value_or(0);
        run.x=observation->tileX;run.y=observation->tileY;
    }
    for(const auto &seen:c.town.observedNavigationWalls(*area)) for(const auto &branch:clues.branches)
        for(const auto &wall:branch.walls) if(seen.x-run.x==wall.x && seen.y-run.y==wall.y && seen.key==wall.key)
            run.walls.emplace(wall.x,wall.y,wall.key);
    const auto releaseApproach=[&]() {
        cancelWalk(c);frontierRoute.reset();
        approachHint.reset();approachFrontier.reset();run.aim.reset();
    };
    for(size_t i=0;i<clues.branches.size();++i) if(std::all_of(clues.branches[i].walls.begin(),clues.branches[i].walls.end(),
        [&](const auto &wall){return run.walls.contains({wall.x,wall.y,wall.key});})) {
        if(run.branch && *run.branch!=i) {
            if(!run.conflict) {releaseApproach();note("terrain-clue-conflict","Conflicting observed Vaught mural pairs; resuming coverage");}
            run.conflict=true;
        } else if(!run.branch) {
            run.branch=i;run.view.reset();run.retryAt={};releaseApproach();
            const auto direction=clues.branches[i].direction;
            note("terrain-clue-confirmed","vaught direction="+std::to_string(int(direction.x))+","+std::to_string(int(direction.y)));
        }
    }
    terrainClueDiagnostics["observedPanels"]=run.walls.size();
    terrainClueDiagnostics["branch"]=run.branch?Json(*run.branch):Json(nullptr);
    if(run.conflict || run.exhausted) {
        terrainClueDiagnostics["status"]=run.conflict?"conflict":"arm-approached-without-assignment";return false;
    }
    if(Clock::now()<run.retryAt) {terrainClueDiagnostics["status"]="local-route-recovery";return false;}
    const auto worldPoint=[&](Vec tile)->OnlinePoint {
        const int x=int(std::floor((tile.x+run.x)*5)),y=int(std::floor((tile.y+run.y)*5));
        if(x<0 || y<0 || x>UINT16_MAX || y>UINT16_MAX) throw std::runtime_error("Vaught clue outside native coordinates");
        return {uint16_t(x),uint16_t(y)};
    };
    std::optional<size_t> observationIndex;
    OnlinePoint aim=worldPoint({clues.width*.5f,clues.height*.5f});
    std::string purpose="Vaught central mural junction";
    if(run.branch) {
        const auto &branch=clues.branches[*run.branch];aim=worldPoint(branch.end);purpose="observed Vaught mural arm";
        terrainClueDiagnostics["direction"]={branch.direction.x,branch.direction.y};
    } else if(run.centerChecked) {
        observationIndex=run.view;
        if(!observationIndex) {
            float nearest=std::numeric_limits<float>::max();
            for(size_t i=0;i<clues.branches.size();++i) if(!run.checkedViews.contains(i)) {
                const auto at=worldPoint(clues.branches[i].observe);
                const float distance=(Vec{float(at.x),float(at.y)}-c.position).length();
                if(distance<nearest) {nearest=distance;observationIndex=i;}
            }
        }
        if(!observationIndex) {terrainClueDiagnostics["status"]="no-visible-clue-fallback";return false;}
        run.view=observationIndex;aim=worldPoint(clues.branches[*observationIndex].observe);
        purpose="Vaught central wall observation";
    }
    terrainClueDiagnostics["aim"]={aim.x,aim.y};
    if((Vec{float(aim.x),float(aim.y)}-c.position).length()<=6) {
        releaseApproach();
        if(run.branch) {run.exhausted=true;note("terrain-clue-arm-reached","Awaiting real unit assignment; ordinary search remains available");}
        else if(observationIndex) {run.checkedViews.insert(*observationIndex);run.view.reset();}
        else run.centerChecked=true;
        terrainClueDiagnostics["status"]="observe-arrival";
        wait("terrain-clue-observe","Allowing the actual rendered viewport to supply wall observations");return true;
    }
    if(run.aim!=aim) {releaseApproach();run.aim=aim;}
    terrainClueDiagnostics["status"]=run.branch?"approach-confirmed-arm":"approach-central-observation";
    if(approachPoint(c,aim,purpose,6,true)) return true;
    run.aim.reset();run.retryAt=Clock::now()+std::chrono::seconds(5);
    if(!run.branch) {
        if(observationIndex) {run.checkedViews.insert(*observationIndex);run.view.reset();}else run.centerChecked=true;
    }
    terrainClueDiagnostics["status"]="local-route-recovery";
    note("terrain-clue-route-recovery",purpose);return false;
}
bool DebugAi::Impl::observeColdPlainsWaypoint(Context &c) {
    if(!area || *area!=definitions->area("Act 1 - Wilderness 2") || knownWaypointAreas.contains(*area)) return false;
    const auto &view=c.session.read();if(!view.load.mapSeed) return false;
    auto &run=coldPlainsSearch;
    if(run.game!=view.gameGeneration || run.seed!=*view.load.mapSeed) {
        run={};run.game=view.gameGeneration;run.seed=*view.load.mapSeed;
    }
    if(run.done) return false;
    if(!run.prepared) {
        const auto entry=c.town.knownConnectionPosition(*area,definitions->area("Act 1 - Wilderness 1"));
        const auto observation=c.town.navigationObservation();
        // This optional early stop is tied to an actually crossed entrance.
        // A portal arrival elsewhere must not trigger a march back to the gate.
        if(!entry || !observation) return false;
        if((c.position-Vec{float(entry->x),float(entry->y)}).length()>100) {run.done=true;return false;}
        const auto edges=c.town.walkingBoundaries();
        const auto edge=std::find_if(edges.begin(),edges.end(),[&](const auto &e) {
            const int along=e.side%2?entry->y:entry->x,normal=e.side%2?entry->x:entry->y;
            return e.destination==definitions->area("Act 1 - Wilderness 1") &&
                along>=e.start && along<e.end && std::abs(normal-e.plane)<=64;
        });
        if(edge==edges.end()) return false;
        // SpawnAct12Waypoint reserves the entry's eight-tile outdoor block,
        // clamped one block inward. The exact waypoint remains a server unit.
        const int width=(observation->width-1)/8,height=(observation->height-1)/8;
        if(width<3 || height<3) {run.done=true;return false;}
        const int bx=std::clamp((int(entry->x)/5-observation->tileX)/8,1,width-2);
        const int by=std::clamp((int(entry->y)/5-observation->tileY)/8,1,height-2);
        for(const int adjacent:{0,-1,1}) {
            const int x=bx+(edge->side%2?0:adjacent),y=by+(edge->side%2?adjacent:0);
            if(x<1 || y<1 || x>=width-1 || y>=height-1) continue;
            const int px=(observation->tileX+x*8+4)*5,py=(observation->tileY+y*8+4)*5;
            if(px>=0 && py>=0 && px<=UINT16_MAX && py<=UINT16_MAX)
                run.views.push_back({uint16_t(px),uint16_t(py)});
        }
        run.prepared=true;run.deadline=Clock::now()+std::chrono::seconds(20);
        note("cold-plains-entry-waypoint","Observing the entry block before continuing field travel");
    }
    terrainClueDiagnostics={{"kind","cold-plains-entry-waypoint"},{"view",run.next},{"views",run.views.size()}};
    if((run.next>=run.views.size() && Clock::now()>=run.settleUntil) || Clock::now()>=run.deadline) {
        if(approachHint && std::find(run.views.begin(),run.views.end(),*approachHint)!=run.views.end()) {
            cancelWalk(c);approachHint.reset();approachFrontier.reset();
        }
        run.done=true;terrainClueDiagnostics["status"]="ordinary-search-fallback";
        note("cold-plains-waypoint-fallback","Entry observation ended without assignment; ordinary search remains available");return false;
    }
    if(Clock::now()<run.settleUntil) {wait("waypoint-entry-observe","Waiting briefly for normal entry-room unit assignment");return true;}
    const auto aim=run.views[run.next];
    terrainClueDiagnostics["aim"]={aim.x,aim.y};
    if((c.position-Vec{float(aim.x),float(aim.y)}).length()<=6) {
        cancelWalk(c);approachHint.reset();approachFrontier.reset();++run.next;
        run.settleUntil=Clock::now()+std::chrono::milliseconds(250);
        wait("waypoint-entry-observe","Observing the original entrance-side waypoint block");return true;
    }
    if(approachPoint(c,aim,"Cold Plains entrance waypoint observation",6,true)) {
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
        return true;
    }
    ++run.next;return false;
}


} // namespace d2x
