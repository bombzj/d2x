#include "remote_scene_impl.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/pd2_phasing.hpp"
#include "gameplay/skills/cast_timing.hpp"

namespace d2x {

std::optional<std::pair<int,int>> RemoteScene::Impl::findPlayerLeapLanding(
    const OnlineUnit &u,Vec start,Vec target,int range,const Map &map,Vec origin) const {
    const auto collision=navigationCollision(map.grid,origin,u.key);
    const auto landing=nativeLeapLanding(map.grid,collision,start-origin,target-origin,range,unitSize(u));
    if(!landing) return {};
    const auto point=*landing+origin;
    return std::pair{int(point.x),int(point.y)};
}
bool RemoteScene::Impl::displayPlayerLeap(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    const bool controlled=u.key.id==playerId;
    const bool local=controlled && localCast && localCast->started>=0;
    const auto skill=local?std::optional<int>{localCast->command.skill}:
        !controlled && u.actionSkill?std::optional<int>{*u.actionSkill}:
        !controlled && m.playerLeap && m.playerLeap->revision==u.actionRevision?
            std::optional<int>{m.playerLeap->skill}:std::nullopt;
    const auto row=skill?skillRows.find(*skill):skillRows.end();
    if(u.key.type!=0 || !skill ||
       row==skillRows.end() || !(pd2?skills.number(row->second,"cltdofunc")==97:
           skills.number(row->second,"cltdofunc")==43 || skills.number(row->second,"cltdofunc")==44) || playerDead(u)) {
        m.playerLeap.reset();return false;
    }
    const auto revision=local?localCast->revision:!u.actionSkill && m.playerLeap?m.playerLeap->revision:u.actionRevision;
    if(m.invalidatedSkillMovement==revision) return false;
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    const float age=u.actionReceivedMilliseconds && now>=u.actionReceivedMilliseconds?float(now-u.actionReceivedMilliseconds)/1000.f:0.f;
    const float started=local?localCast->started:m.playerLeap && m.playerLeap->revision==revision?m.playerLeap->started:time-age;
    const auto snapshot=combatBinding->states().find(u.key);
    if(snapshot==combatBinding->states().end() || !snapshot->second.decoded || snapshot->second.sequence!=u.stateSequence)
        return false;
    bool flying=false;
    for(const auto &[id,list]:snapshot->second.states) {
        (void)list;const auto state=stateRows.find(id);
        if(state!=stateRows.end() && states.value(state->second,"state")=="skill_move") flying=true;
    }
    // The server's movement-state removal can precede the final position
    // packet. A confirmed flight still owns its native arc/landing.
    if(!flying && !m.playerLeap) return false;
    if(m.leapRevision==revision && m.leapLanding>=0) return false;
    const auto sequence=playerCastSequence(skills.number(row->second,"seqnum").value_or(-1));
    if(!sequence || sequence->fixedSpeed<=0 ||
       time<started+float(playerCastSequenceTiming(*sequence,sequence->fixedSpeed).impact)/25.f)
        return false;
    if(!m.playerLeap || m.playerLeap->revision!=revision) {
        auto destination=local?localCast->command.point:u.destination;
        const auto target=local?localCast->command.target:u.destinationUnit;
        if(target) destination=targetPosition(*target);
        int rank=u.actionSkillLevel.value_or(0);
        if(local) {
            const auto *known=uiClients->character().read().skill(*skill,localCast->command.owner);
            if(!known || !known->effectiveRankKnown) return false;
            rank=known->effectiveRank;
        }
        if(!destination || rank<1) return false;
        const bool leapAttack=!pd2 && skills.number(row->second,"cltdofunc")==44;
        const auto range=leapAttack?std::optional<int>{}:skillCalculation(*skill,rank,"aurarangecalc",u.key);
        std::optional<int> speed;
        if(pd2) speed=skillCalculation(*skill,rank,"calc2",u.key);
        else if(u.classId && *u.classId<characterRows.size()) speed=charstats.number(characterRows[*u.classId],"RunVelocity");
        if((!leapAttack && (!range || *range<1)) || !speed || (!pd2 && *speed<=0)) {
            effectLimitations.insert("Original leap range or velocity unresolved");return false;
        }
        const int x=int(std::floor(m.position.x)),y=int(std::floor(m.position.y));
        const Vec point{float(destination->x),float(destination->y)};
        Vec landing;
        if(leapAttack) {
            const auto collision=navigationCollision(map.grid,origin,u.key);
            const auto localPoint=nativeLeapAttackLanding(map.grid,collision,m.position-origin,point-origin,unitSize(u));
            if(!localPoint || !map.activation.room(*localPoint)) {
                effectLimitations.insert("Original Leap Attack landing unavailable");return false;
            }
            landing=*localPoint+origin;
        } else {
            const auto found=findPlayerLeapLanding(u,m.position,point,*range,map,origin);
            if(!found) {effectLimitations.insert("Original leap landing unavailable");return false;}
            landing={float(found->first),float(found->second)};
        }
        // Retail C5F20 uses CharStats.RunVelocity. PD2 CltDo97 instead
        // uses Calc2*41+2560, with its native minimum of 2560.
        const int velocity=pd2?std::max(2560,nativeAdd(nativeMultiply(*speed,41),2560)):nativeShiftLeft(*speed,8);
        const int travel=std::max(1,unitCoordinateDistance(x,y,int(landing.x),int(landing.y)));
        const auto graphics=ClientMissileGraphics::leap(travel,velocity);if(!graphics) return false;
        Motion::PlayerLeap run;run.revision=revision;run.position=m.position;run.skill=*skill;run.started=started;
        run.destination=landing;
        run.velocity=float(velocity)*25.f/4096.f;run.nextTick=time+NativePathStep::seconds;
        // Retail C65B0/C7C80: distance-dependent gravity and zero initial
        // height; flight ticks use the common native graphics accumulator.
        run.graphics=*graphics;
        m.playerLeap=std::move(run);m.leapRevision=revision;m.leapFlying=true;m.leapLanding=-1;
        m.route.clear();m.goal.reset();m.correction={};m.correctionLeft=0;m.pathStep.reset();
    }
    auto &run=*m.playerLeap;
    while(run.nextTick<=time) {
        const Vec center{.5f,.5f};
        const auto step=run.path.advance(map.grid,run.position+center,run.destination+center,
            run.velocity*NativePathStep::seconds,{0,unitSize(u)},origin);
        m.look=run.destination-run.position;run.position=step.position-center;
        run.graphics.step();run.nextTick+=NativePathStep::seconds;
        if(step.reached || (run.graphics.position[2]>>11)<=0) {
            m.position=run.destination;m.pathMoving=false;m.leapLanding=time;m.playerLeap.reset();
            return true;
        }
        if(step.blocked) {
            effectLimitations.insert("Original leap flight path unavailable");m.playerLeap.reset();return false;
        }
    }
    m.position=run.position;m.pathMoving=true;
    return true;
}

bool RemoteScene::Impl::walking(const OnlineUnit &u) const {
    if (u.actionSkill) return false;
    if(u.key.type==1 && u.mode==8 && unitClass(u)) {
        const auto row=monsterRows.find(*unitClass(u));
        if(row!=monsterRows.end() && monstats.value(row->second,"AI")=="Vulture") return true;
    }
    return u.key.type == 0 ? (u.nativeMode ? (u.mode == 2 || u.mode == 3 || u.mode == 6)
        : (u.mode == 0 || u.mode == 1 || u.mode == 23 || u.mode == 24))
        : u.key.type == 1 && (u.mode == 2 || u.mode == 15);
}

float RemoteScene::Impl::movementSpeed(const OnlineUnit &u) const {
    if (!unitClass(u)) return 0;
    if (u.key.type == 0) {
        const auto pose=motion.find(u.key);
        return pose!=motion.end() && pose->second.movementTiming?float(pose->second.movementTiming->velocity)*25.f/4096.f:0;
    }
    const auto row = monsterRows.find(*unitClass(u));
    if (u.key.type != 1 || row == monsterRows.end() || !u.velocityPercent) return 0;
    if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[state,entry]:snapshot->second.states) {
            (void)entry;const auto graphics=stateRows.find(state);
            if(graphics==stateRows.end() || states.number(graphics->second,"gfxtype")!=2) continue;
            const auto cls=states.number(graphics->second,"gfxclass");
            if(cls && *cls>=0 && size_t(*cls)<characterRows.size()) return monsterMovementSpeed(charstats.number(characterRows[size_t(*cls)],"WalkVelocity").value_or(0),int(*u.velocityPercent));
        }
    // Native path velocity is MonStats.Velocity << 8, modified by the full wire percentage.
    return monsterMovementSpeed(monstats.number(row->second, "Velocity").value_or(0), int(*u.velocityPercent));
}

std::optional<uint8_t> RemoteScene::Impl::locomotionMode(const OnlineUnit &u,bool moving,bool town) const {
    if(u.actionSkill) return {};
    const auto &pose=motion.at(u.key);
    if(u.key.type==0 && !playerDead(u)) {
        const bool locomotion=u.nativeMode
            ? u.mode==1 || u.mode==2 || u.mode==3 || u.mode==5 || u.mode==6
            : !u.mode || u.mode==0 || u.mode==1 || u.mode==7 || u.mode==19 || u.mode==23 || u.mode==24;
        if(locomotion) return uint8_t(moving?(pose.running?3:town?6:2):town?5:1);
    }
    if(u.key.type==1 && !onlineMonsterCorpse(u) && (u.mode==1 || u.mode==2 || u.mode==15))
        return uint8_t(moving?(pose.running?15:2):1);
    return {};
}

std::optional<MovementTiming> RemoteScene::Impl::playerTiming(const OnlineUnit &u,bool running,const RemoteUiClients &clients) {
    const auto cls=unitClass(u);
    if(!cls || *cls>=characterRows.size()) return {};
    const auto row=characterRows[*cls];
    const auto walk=charstats.number(row,"WalkVelocity"),run=charstats.number(row,"RunVelocity");
    if(!walk || !run || *walk<=0 || *run<=0) return {};
    const auto timingFor=[&](bool runMode) {
        auto timing=clients.movement(u.key,runMode);
        if(!timing) {
            timing=playerMovementTiming(*walk,*run,0,0,runMode);
            effectLimitations.insert("Original player movement modifiers unavailable for unit "+std::to_string(u.key.id));
        }
        return *timing;
    };
    if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[state,entry]:snapshot->second.states) {
            (void)entry;const auto stateRow=stateRows.find(state);
            if(stateRow==stateRows.end() || !states.number(stateRow->second,"transform").value_or(0) || states.number(stateRow->second,"gfxtype")!=1) continue;
            const auto identity=states.number(stateRow->second,"gfxclass");if(!identity) return {};
            const auto monster=monsterRows.find(*identity);if(monster==monsterRows.end()) return {};
            const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) return {};
            uint16_t enabled=uint16_t(monstats2.number(extra->second,"mWL").value_or(0)!=0)<<2;
            enabled|=uint16_t(monstats2.number(extra->second,"mRN").value_or(0)!=0)<<15;
            const auto mode=shapeMonsterMode(running?3:2,enabled);
            if(mode!=2 && mode!=15) return {};
            const auto &rates=mode==15?monsterRunRates:monsterWalkRates;
            const auto rate=rates.find(*identity);const auto velocity=monstats.number(monster->second,"Velocity");
            if(rate==rates.end() || !velocity || *velocity<0) return {};
            return nativeMovementTiming(*velocity,rate->second,timingFor(mode==15).percentage);
        }
    return timingFor(running);
}

int RemoteScene::Impl::unitSize(const OnlineUnit &u) const {
    if (u.key.type == 0) return playerMovement.size;
    const auto row = monsterRows.find(unitClass(u).value_or(UINT16_MAX));
    if (u.key.type != 1 || row == monsterRows.end()) return 0;
    const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
    return extra == monsterExtra.end() ? 0 : monstats2.number(extra->second, "SizeX").value_or(0);
}

MovementCollisionRule RemoteScene::Impl::movementRule(const OnlineUnit &u) const {
    if(pd2 && combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[state,entry]:snapshot->second.states) if(pd2PhasingState(state))
            return {pd2PhasingMovementMask(u.key.type==1),unitSize(u)};
    if (u.key.type == 0) return playerMovement;
    const auto row = monsterRows.find(unitClass(u).value_or(UINT16_MAX));
    if (row == monsterRows.end()) return {};
    if(u.mode==8 && !u.actionSkill && monstats.value(row->second,"AI")=="Vulture") return {0,1};
    return monsterMovementCollision(monstats.value(row->second, "BaseId") == "wraith1",
        monstats.number(row->second, "flying").value_or(0),
        monstats.number(row->second, "opendoors").value_or(0), unitSize(u));
}

std::optional<std::pair<NativeCollisionPattern,uint16_t>> RemoteScene::Impl::unitFootprint(const OnlineUnit &unit) const {
    const auto key=unit.key;
    if(key.type>1 || !unit.position) return {};
    const bool dead=key.type==0?playerDead(unit):onlineMonsterCorpse(unit);
    auto pattern=nativeUnitCollisionPattern(unitSize(unit));
    uint16_t mask=key.type==0?0x80:0x100;
    if(key.type==0 && dead) {pattern=NativeCollisionPattern::SmallNoPresence;mask=0x8000;}
    if(key.type==1) {
        const auto row=monsterRows.find(unitClass(unit).value_or(UINT16_MAX));
        if(row==monsterRows.end()) return {};
        const auto ai=monstats.value(row->second,"AI");
        if(unit.mode==8 && !unit.actionSkill && (ai=="Vulture" || ai=="SandMaggot" || ai=="FrogDemon")) return {};
        if(ai=="FrogDemon") {
            const auto used=unit.actionSkill?unit.actionSkill:unit.stoppedSkill;
            const auto skill=used?skillRows.find(*used):skillRows.end();
            if(skill!=skillRows.end() && skills.number(skill->second,"srvstfunc")==51) return {};
        }
        const auto pose=motion.find(key);
        if(pose!=motion.end() && pose->second.scriptedSequence && pose->second.scriptedSequence->leapGraphics) return {};
        const bool town=monstats.number(row->second,"npc").value_or(0) || monstats.number(row->second,"inTown").value_or(0);
        pattern=nativeUnitCollisionPattern(unitSize(unit),town && !monstats.number(row->second,"interact").value_or(0));
        if(monstats.value(row->second,"BaseId")=="wraith1") pattern=NativeCollisionPattern::SmallNoPresence;
        const auto extra=monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
        if(dead && (extra==monsterExtra.end() || !monstats2.number(extra->second,"deadCol").value_or(0))) {
            pattern=NativeCollisionPattern::SmallNoPresence;mask=0x8000;
        }
    }
    return std::pair{pattern,mask};
}
void RemoteScene::Impl::writeCollision(const CollisionBody &body,bool occupied,const RoomBounds *onlyRoom) const {
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(onlyRoom && (body.x+dx<onlyRoom->x || body.y+dy<onlyRoom->y ||
            body.x+dx>=onlyRoom->x+onlyRoom->width || body.y+dy>=onlyRoom->y+onlyRoom->height)) continue;
        const auto mask=nativeCollisionFootprint(body.pattern,dx,dy,body.mask);
        if(!mask) continue;
        auto &cell=collisionCells[{body.x+dx,body.y+dy}];
        // Native collision is a bitfield, not an OR rebuilt from actors or
        // reference counts. Clearing one overlapping body clears shared bits.
        if(occupied) {cell.set|=mask;cell.cleared&=uint16_t(~mask);}
        else {cell.set&=uint16_t(~mask);cell.cleared|=mask;}
    }
}
void RemoteScene::Impl::prepareCollision(const Map &map,OnlinePoint origin) {
    std::erase_if(collisionBodies,[&](const auto &entry) {
        const auto unit=world().units.find(entry.first);
        if(unit!=world().units.end() && unit->second.assignmentRevision==entry.second.assignment) return false;
        writeCollision(entry.second,false);return true;
    });
    std::map<std::pair<int,int>,RoomBounds> rooms;
    if(map.grid.navigationRooms) for(auto room:*map.grid.navigationRooms) {
        room.x+=origin.x;room.y+=origin.y;rooms.emplace(std::pair{room.x,room.y},room);
    }
    const auto contains=[](const RoomBounds &room,int x,int y) {
        return x>=room.x && y>=room.y && x<room.x+room.width && y<room.y+room.height;
    };
    const auto retained=[&](const auto &entry) {
        const auto old=collisionRooms.find(entry.first);
        return old!=collisionRooms.end() && old->second.width==entry.second.width && old->second.height==entry.second.height;
    };
    // An unloaded/recreated room starts from its new terrain. Retained rooms
    // keep their Set/Reset history even when the map snapshot origin changes.
    if(rooms.size()!=collisionRooms.size() || !std::all_of(rooms.begin(),rooms.end(),retained)) {
        std::erase_if(collisionCells,[&](const auto &cell) {
            return std::none_of(rooms.begin(),rooms.end(),[&](const auto &entry) {
                return retained(entry) && contains(entry.second,cell.first.first,cell.first.second);
            });
        });
        for(const auto &entry:rooms) if(!retained(entry))
            for(const auto &[unit,body]:collisionBodies) {(void)unit;writeCollision(body,true,&entry.second);}
    }
    collisionRooms=std::move(rooms);
    for(const auto &[key,unit]:world().units) {(void)key;if(unit.position) syncCollision(unit);}
}
void RemoteScene::Impl::syncCollision(const OnlineUnit &unit) const {
    if(unit.key.type>1) return;
    CollisionBody next;next.assignment=unit.assignmentRevision;
    if(const auto footprint=unitFootprint(unit)) {
        auto point=localUnitPosition(unit.key,false);
        if(const auto found=motion.find(unit.key);found!=motion.end() && found->second.last &&
           found->second.assignmentRevision==unit.assignmentRevision) point=found->second.position;
        if(point) {
            next.x=int(std::floor(point->x+.5f));next.y=int(std::floor(point->y+.5f));
            next.pattern=footprint->first;next.mask=footprint->second;
        }
    }
    auto [at,inserted]=collisionBodies.try_emplace(unit.key,next);
    if(inserted) writeCollision(next,true);
    else if(at->second!=next) {
        if(unit.key.type==0 && at->second.mask==0x8000 && next.mask==0x80 && at->second.assignment==next.assignment) {
            auto live=at->second;live.pattern=NativeCollisionPattern::SmallUnit;live.mask=0x80;
            writeCollision(live,false);
        } else writeCollision(at->second,false);
        at->second=next;writeCollision(next,true);
    }
}
Grid RemoteScene::Impl::navigationCollision(const Grid &terrain,Vec origin,OnlineUnitKey self,
                             std::optional<OnlineUnitKey> target) const {
    Grid view;view.width=terrain.width;view.height=terrain.height;
    view.neighbours=terrain.neighbours;
    view.navigationRooms=terrain.navigationRooms;
    view.obstacleRevision=terrain.obstacleRevision;view.missileCollisionRevision=terrain.missileCollisionRevision;
    const EntityId object=target && target->type==2?EntityId{uint64_t(target->id)+1}:EntityId{};
    Vec navigationOrigin;auto navigation=terrain.linkedNavigation(navigationOrigin);
    view.collisionSample=[this,navigation=std::move(navigation),navigationOrigin,origin,object](int x,int y,EntityId ignored)->uint16_t {
        uint16_t mask=navigation.movementMask(x-int(navigationOrigin.x),y-int(navigationOrigin.y),ignored?ignored:object);
        const auto found=collisionCells.find({x+int(origin.x),y+int(origin.y)});
        return found==collisionCells.end()?mask:uint16_t((mask&~found->second.cleared)|found->second.set);
    };
    if(target) if(const auto at=world().units.find(*target);at!=world().units.end() &&
        (target->type==0?playerDead(at->second):onlineMonsterCorpse(at->second))) target.reset();
    // Grid path queries own this scope, including early exits. Restore target
    // before self, matching PATH_ComputePath; plain movement queries include both.
    view.collisionQuery=[this,self,target,depth=std::make_shared<unsigned>(0)](bool begin) {
        if(begin?(*depth)++!=0:--*depth!=0) return;
        const auto write=[&](OnlineUnitKey key) {if(const auto at=collisionBodies.find(key);at!=collisionBodies.end()) writeCollision(at->second,!begin);};
        if(begin) {write(self);if(target && *target!=self) write(*target);}
        else {if(target && *target!=self) write(*target);write(self);}
    };
    view.collisionStep=[this,self,origin,sample=view.collisionSample](int,int,int x,int y,MovementCollisionRule rule) {
        const auto at=collisionBodies.find(self);
        const auto previous=at==collisionBodies.end()?CollisionBody{}:at->second;
        writeCollision(previous,false);
        bool clear=true;
        for(int dy=-1;dy<=1 && clear;++dy) for(int dx=-1;dx<=1;++dx)
            if(nativeCollisionSizeCell(rule.size,dx,dy) && (sample(x+dx,y+dy,{})&rule.mask)) {clear=false;break;}
        if(clear && at!=collisionBodies.end()) {
            at->second.x=x+int(origin.x);at->second.y=y+int(origin.y);writeCollision(at->second,true);
        } else writeCollision(previous,true);
        return clear;
    };
    view.collisionSample=[sample=view.collisionSample,query=view.collisionQuery](int x,int y,EntityId ignored) {
        query(true);const auto value=sample(x,y,ignored);query(false);return value;
    };
    return view;
}

void RemoteScene::Impl::advanceMovement(const Map &map,OnlinePoint origin,const RemoteUiClients &clients) {
    std::vector<const OnlineUnit *> units;
    for(const auto &[key,unit]:world().units) if(unit.position) units.push_back(&unit);
    // Retail 1.13c A8110/A4DA0: server-backed players, then monsters;
    // A65F3 inserts each GUID in descending order within its 128-way bucket.
    std::sort(units.begin(),units.end(),[](const auto *a,const auto *b) {
        if(a->key.type!=b->key.type) return a->key.type<b->key.type;
        if((a->key.id&127)!=(b->key.id&127)) return (a->key.id&127)<(b->key.id&127);
        return a->key.id>b->key.id;
    });
    // Assignment/removal and state changes write footprints once. Retained
    // actors never reconstruct the occupied grid on a query or render frame.
    for(const auto *unit:units) syncCollision(*unit);
    const auto changed=[&](const OnlineUnit &unit) {
        const auto found=motion.find(unit.key);
        if(found==motion.end() || !found->second.last || found->second.assignmentRevision!=unit.assignmentRevision ||
           found->second.actionRevision!=unit.actionRevision || found->second.positionRevision!=unit.positionRevision ||
           found->second.discontinuity!=unit.positionDiscontinuity) return true;
        if(unit.key.type!=0 || unit.key.id!=playerId) return false;
        auto request=world().movementRequest;
        if(request && localCastBlocksMovement()) request.reset();
        const auto revision=request?request->revision:localCast && localCast->started<0 && castMeleeRange(unit,localCast->command)?localCast->revision:0;
        return found->second.requestRevision!=revision ||
            (localCast && localCast->started>=0 && localCast->revision>found->second.castRevision);
    };
    const auto endFrame=uint64_t(movementClock*25.0);
    const float renderTime=time;
    advancingMovement=true;
    for(;movementFrame<endFrame;) {
        time=float(double(++movementFrame)/25.0);movementElapsed=NativePathStep::seconds;
        for(const auto *unit:units) {
            // A newly observed command is not evidence that it was active
            // during a loading gap. Retained orders still catch up tick-wise.
            if(movementFrame<endFrame && changed(*unit)) continue;
            displayPosition(*unit,map,origin,clients);
        }
    }
    time=renderTime;movementElapsed=0;
    // Consume fresh wire/input changes even between ticks, without granting
    // every actor its own fractional simulation clock.
    for(const auto *unit:units) if(changed(*unit)) displayPosition(*unit,map,origin,clients);
    advancingMovement=false;
}

void RemoteScene::Impl::plan(Motion &m, Vec goal, const Map &map, Vec origin, MovementCollisionRule rule, float speed,
          const OnlineUnit &unit, std::optional<OnlineUnitKey> target, bool continuation) {
    m.route.clear(); m.goal = goal;
    m.plannedFrom=m.position;m.plannedGoal=goal;m.plannedEnd.reset();m.pathResult="no-path";
    // D2Common_10142 replaces path points, not precise coordinates or the
    // shared 25 Hz clock. Retargeting never starts a private actor clock.
    if (speed <= 0 || rule.size <= 0) return;
    // Native precise coordinates target cell centers; presentation adds
    // its half-cell offset when drawing. Match the control planner here.
    const Vec center{.5f, .5f};
    if(m.actionRevision!=unit.actionRevision || !m.effectivePathType) m.effectivePathType=unit.pathType;
    else if(unit.key.type==1 && (target || continuation) && m.effectivePathType &&
        (*m.effectivePathType==2 || *m.effectivePathType==13 || *m.effectivePathType==15)) m.effectivePathType=target?13:2;
    const auto pathType=m.effectivePathType;
    const bool nativeMonster=unit.key.type==1 && pathType &&
        (*pathType==0 || *pathType==1 || *pathType==2 || *pathType==3 || *pathType==5 || *pathType==6 || *pathType==7 || *pathType==12 || *pathType==13 || *pathType==15);
    if(unit.key.type==1 && !nativeMonster) {
        effectLimitations.insert("Native monster path parameters unavailable or unsupported");
        return;
    }
    std::optional<NativePathSearch> search;
    if(nativeMonster && pathType==0) {
        const auto start=map.grid.navigationRoom(m.position-origin+center);
        const auto end=map.grid.navigationRoom(goal-origin+center);
        if(!start || !end) {
            m.pathResult="unavailable-room";
            effectLimitations.insert("Native IDAStar room bounds unavailable");return;
        }
        search=NativePathSearch{*start,*end};
    }
    const bool removeTarget=unit.key.type==0 || (pathType && (*pathType==0 || *pathType==1 || *pathType==7 || *pathType==15));
    const auto grid=navigationCollision(map.grid,origin,unit.key,removeTarget?target:std::nullopt);
    std::optional<int> doorOrientation;
    if(target && target->type==2 && sceneBinding)
        for(const auto &entry:sceneBinding->mapTargets) if(entry.unit==*target) {doorOrientation=entry.doorOrientation;break;}
    auto path = unit.key.type==0
        ? grid.nativePlayerPath(m.position - origin + center, goal - origin + center, rule,target?target->type:255,doorOrientation)
        : nativeMonster ? grid.nativeUnitPath(m.position-origin+center,goal-origin+center,*pathType,unit.monsterPathMaxDistance,rule,target.has_value(),1,search?&*search:nullptr)
        : grid.path(m.position - origin + center, goal - origin + center, true, rule);
    if(nativeMonster && path.empty() && m.actionRevision==unit.actionRevision &&
       ((target && pathType==13) || (continuation && pathType==2))) {
        m.effectivePathType=15;
        const auto retry=navigationCollision(map.grid,origin,unit.key,target);
        path=retry.nativeUnitPath(m.position-origin+center,goal-origin+center,15,unit.monsterPathMaxDistance,rule,target.has_value());
    }
    for (auto point : path)
        m.route.push_back(point + origin - center);
    if(!m.route.empty()) {m.plannedEnd=m.route.back();m.pathResult="planned";}
}

bool RemoteScene::Impl::displayPlayerWhirlwind(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    if(pd2 || u.key.type!=0 || playerDead(u) || !hasVisualState(u.key,"whirlwind") || !hasVisualState(u.key,"skill_move"))
        return false;
    const bool local=u.key.id==playerId && localCast && localCast->started>=0;
    const auto skill=local?std::optional<int>{localCast->command.skill}:
        u.actionSkill?std::optional<int>{*u.actionSkill}:std::nullopt;
    const auto row=skill?skillRows.find(*skill):skillRows.end();
    if(row==skillRows.end() || skills.number(row->second,"cltdofunc")!=45) return false;
    const auto revision=local?localCast->revision:u.actionRevision;
    if(m.invalidatedSkillMovement==revision) return false;
    const auto grid=navigationCollision(map.grid,origin,u.key);
    constexpr Vec center{.5f,.5f};
    if(!m.scriptedSequence || m.scriptedSequence->revision!=revision) {
        const auto target=local?localCast->command.target:u.destinationUnit;
        auto point=local?localCast->command.point:u.destination;
        if(target) point=targetPosition(*target);
        if(!point || !u.classId || *u.classId>=characterRows.size()) return false;
        const int speed=charstats.number(characterRows[*u.classId],"WalkVelocity").value_or(0);
        const Vec aim{float(point->x),float(point->y)};
        const auto route=grid.nativeUnitPath(m.position-origin+center,aim-origin+center,7,100,{0xc01,unitSize(u)});
        if(route.empty() || speed<=0) return false;
        m.scriptedSequence=Motion::ScriptedSequence{};
        auto &run=*m.scriptedSequence;
        run.revision=revision;run.nextTick=time+NativePathStep::seconds;
        run.destination=route.back()+origin-center;run.velocity=float(speed)*25.f/16.f;run.moving=true;
        m.look=(run.destination-m.position).unit();
        m.route.clear();m.goal.reset();m.pathStep.reset();m.correction={};m.correctionLeft=0;
    }
    auto &run=*m.scriptedSequence;
    while(run.moving && time>=run.nextTick) {
        const auto step=m.pathStep.advance(grid,m.position-origin+center,run.destination-origin+center,
            run.velocity*NativePathStep::seconds,{0x401,unitSize(u)});
        m.position=step.position+origin-center;
        run.moving=!step.reached && !step.blocked;
        run.nextTick+=NativePathStep::seconds;
    }
    return true;
}
bool RemoteScene::Impl::displayScriptedSequence(const OnlineUnit &u,Motion &m,const Map &map,Vec origin,MovementCollisionRule collision) {
    const auto skill=u.actionSkill?skillRows.find(*u.actionSkill):skillRows.end();
    if(pd2 && u.key.type==1 && skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==114)
        return m.scriptedSequence && m.scriptedSequence->revision==u.actionRevision && !onlineMonsterCorpse(u);
    if(pd2 && u.key.type==1 && skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==37)
        return displayPd2MonsterCharge(u,m,map,origin);
    const bool whirlwind=skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==45;
    const bool pd2Leap=pd2 && skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==97;
    if(pd2Leap && m.invalidatedSkillMovement==u.actionRevision) return false;
    const bool leap=skill!=skillRows.end() && (skills.number(skill->second,"cltdofunc")==44 || pd2Leap);
    if(u.key.type!=1 || skill==skillRows.end() || (!whirlwind && !leap && skills.number(skill->second,"cltdofunc")!=59) || onlineMonsterCorpse(u)) {
        m.scriptedSequence.reset();return false;
    }
    const auto monster=monsterRows.find(unitClass(u).value_or(UINT16_MAX));
    if(monster==monsterRows.end()) return false;
    const auto sequence=monsterSequenceFlags.find(monsterSkillMode(monster->second,*u.actionSkill));
    if(sequence==monsterSequenceFlags.end() || sequence->second.empty()) return false;
    const auto movementGrid=navigationCollision(map.grid,origin,u.key);
    if(!m.scriptedSequence || m.scriptedSequence->revision!=u.actionRevision) {
        auto destination=u.destination?std::optional{Vec{float(u.destination->x),float(u.destination->y)}}:
            u.destinationUnit?localUnitPosition(*u.destinationUnit):std::nullopt;
        if(!destination) return false;
        if(pd2Leap) {
            if(!u.actionSkillLevel) return false;
            const auto range=skillCalculation(*u.actionSkill,*u.actionSkillLevel,"aurarangecalc",u.key);
            if(!range || *range<1) return false;
            const auto landing=findPlayerLeapLanding(u,m.position,*destination,*range,map,origin);
            if(!landing) return false;
            destination=Vec{float(landing->first),float(landing->second)};
        } else if(leap) {
            const int x=int(std::floor(m.position.x)),y=int(std::floor(m.position.y));
            int tx=int(std::floor(destination->x)),ty=int(std::floor(destination->y));
            const int distance=unitCoordinateDistance(x,y,tx,ty);if(!distance) return false;
            int landingX=tx+2*(tx-x)/distance,landingY=ty+2*(ty-y)/distance;
            std::map<std::pair<int,int>,uint16_t> occupied;
            for(const auto &[key,other]:world().units) if(key.type<=1 && other.position &&
                !(key.type?onlineMonsterCorpse(other):playerDead(other))) {
                const auto point=localUnitPosition(key);if(!point) continue;
                const auto flying=motion.find(key);
                if(flying!=motion.end() && flying->second.scriptedSequence && flying->second.scriptedSequence->leapGraphics) continue;
                auto pattern=nativeUnitCollisionPattern(unitSize(other));
                if(key.type==1) if(const auto row=monsterRows.find(unitClass(other).value_or(UINT16_MAX));row!=monsterRows.end() && monstats.value(row->second,"BaseId")=="wraith1") pattern=NativeCollisionPattern::SmallNoPresence;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                    occupied[{int(std::floor(point->x))+dx,int(std::floor(point->y))+dy}]|=nativeCollisionFootprint(pattern,dx,dy,key.type?0x100:0x80);
            }
            const auto clear=[&](int column,int row) {
                if(!map.activation.room(Vec{float(column),float(row)}-origin)) return false;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    if(!nativeCollisionSizeCell(unitSize(u),dx,dy)) continue;
                    if(map.grid.movementMask(column+dx-int(origin.x),row+dy-int(origin.y))&0x1c09) return false;
                    if(const auto found=occupied.find({column+dx,row+dy});found!=occupied.end() && (found->second&0x1c09)) return false;
                }
                return true;
            };
            if(!clear(landingX,landingY)) {
                const auto found=nativeSpawnCoordinate(tx,ty,50,clear);if(!found) return false;
                landingX=found->first;landingY=found->second;
            }
            destination=Vec{float(landingX),float(landingY)};
            if(!map.grid.collisionSegment(m.position-origin,*destination-origin,0x804)) return false;
        }
        m.scriptedSequence=Motion::ScriptedSequence{};auto &run=*m.scriptedSequence;
        run.animation.remaining=int(sequence->second.size())*256;run.revision=u.actionRevision;
        run.destination=*destination;run.nextTick=time+NativePathStep::seconds;
        const auto receivedNow=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if(u.actionReceivedMilliseconds && receivedNow>=u.actionReceivedMilliseconds)
            run.nextTick-=float(receivedNow-u.actionReceivedMilliseconds)/1000.f;
        m.route.clear();m.goal.reset();m.pathStep.reset();m.correction={};m.correctionLeft=0;
        if(whirlwind) {
            const Vec center{.5f,.5f};
            auto path=movementGrid.nativeUnitPath(m.position-origin+center,run.destination-origin+center,7,100,{0xc01,collision.size});
            for(const auto point:path) m.route.push_back(point+origin-center);
            run.velocity=monsterMovementSpeed(monstats.number(monster->second,"Velocity").value_or(0),100);
            m.goal=run.destination;run.moving=true;
        }
    }
    auto &run=*m.scriptedSequence;const auto row=skill->second;
    if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[id,state]:snapshot->second.states) {(void)id;if(state.name=="freeze") {run.nextTick=time+NativePathStep::seconds;return true;}}
    const auto parameter=[&](int slot){return skills.number(row,"Param"+std::to_string(slot)).value_or(0);};
    while(!run.ended && run.nextTick<=time+1e-5f) {
        run.nextTick+=NativePathStep::seconds;
        if(run.animation.remaining<=0) {run.ended=true;m.route.clear();m.goal.reset();break;}
        bool stopped=false;
        // ModeChange advances movement before Clt59. Path completion
        // sets flag2 and invokes Do immediately, before StopSequence.
        if(run.moving) {
            while(!m.route.empty() && (m.route.front()-m.position).length()<.01f) m.route.pop_front();
            if(!m.route.empty()) {
                const Vec center{.5f,.5f};const auto next=m.route.front();
                const auto step=m.pathStep.advance(movementGrid,m.position+center,next+center,
                    run.velocity*NativePathStep::seconds,leap?MovementCollisionRule{0,unitSize(u)}:whirlwind?MovementCollisionRule{0x401,collision.size}:collision,origin);
                m.look=next-m.position;m.position=step.position-center;
                if(step.blocked) m.route.clear();else if(step.reached) m.route.pop_front();
            }
            stopped=m.route.empty();
        }
        if(leap && run.leapGraphics) run.leapGraphics->step();
        const int frame=run.animation.frame>>8;
        if(leap) {
            if(stopped) run.leapFlags|=2;
            if(stopped || run.event==1 || run.event==2 || run.event==3 || run.event==4) {
                if(run.leapFlags&0x100) {
                    const int distance=unitCoordinateDistance(int(std::floor(m.position.x)),int(std::floor(m.position.y)),int(run.destination.x),int(run.destination.y));
                    if(run.leapGraphics && (run.leapGraphics->position[2]>>11)>0 && distance) {
                        bool distanceFrame=!pd2Leap || unitClass(u)==991;
                        if(pd2Leap) {
                            const auto base=monstats.value(monster->second,"BaseId");
                            for(const int id:{78,540}) if(const auto row=monsterRows.find(id);row!=monsterRows.end() &&
                                monstats.value(row->second,"Id")==base) distanceFrame=true;
                        }
                        run.animation.frame=(run.animation.frame&255)+(distanceFrame && distance<2?12:10)*256;
                        run.animation.remaining=(run.animation.remaining&~255)+256;
                    } else {
                        run.leapGraphics.reset();run.moving=false;m.route.clear();m.goal.reset();
                        run.leapFlags=0x1000;
                    }
                } else if(run.leapFlags&0x80) {
                    const int speed=monstats.number(monster->second,"Run").value_or(0);if(speed<=0) return false;
                    const int distance=std::max(1,unitCoordinateDistance(int(std::floor(m.position.x)),int(std::floor(m.position.y)),int(run.destination.x),int(run.destination.y)));
                    const auto graphics=ClientMissileGraphics::leap(distance,nativeShiftLeft(speed,8));if(!graphics) return false;
                    run.leapGraphics=*graphics;run.leapFlags=0x1101;run.velocity=float(speed)*25.f/16.f;
                    m.route={run.destination};m.goal=run.destination;m.pathStep.reset();run.moving=true;
                }
                if(stopped) {run.leapFlags=0;run.stopping=true;run.moving=false;run.leapGraphics.reset();m.route.clear();m.goal.reset();}
            }
            run.event=advanceMonsterAnimation(run.animation,int(sequence->second.size()),256,0,true,sequence->second).event;
            continue;
        }
        if(whirlwind) {
            if(stopped) {run.stopping=true;run.moving=false;m.route.clear();m.goal.reset();}
            else if(!run.stopping && (run.event==1 || run.event==2 || run.event==4)) {
                // Clt45 differs from monster Do76: rewind to 3 and keep
                // five frames, as C6583/C658B in the retail client.
                run.animation.frame=(run.animation.frame&255)+3*256;run.animation.remaining=5*256;
            }
            run.event=advanceMonsterAnimation(run.animation,int(sequence->second.size()),256,0,true,sequence->second).event;
            continue;
        }
        bool contact=false;
        if(u.destinationUnit) if(const auto target=world().units.find(*u.destinationUnit);target!=world().units.end())
            if(const auto at=localUnitPosition(*u.destinationUnit)) contact=nativeUnitDistance(m.position,unitSize(u),*at,unitSize(target->second))<=1;
        if((stopped || contact) && frame<=parameter(4)) {
            run.moving=false;run.animation.remaining=nativeShiftLeft(parameter(1),8);run.animation.frame=nativeShiftLeft(parameter(2),8);
            m.route.clear();m.goal.reset();
        } else if((run.event==1 || run.event==2 || run.event==4) && frame<=parameter(4)) {
            if(frame==parameter(3)) {
                const int velocity=skillCalculation(*u.actionSkill,u.actionSkillLevel.value_or(1),"calc1",u.key).value_or(0);
                // MONSTERS_Initialize's base is 75. The old walking wire
                // percentage includes its mode velocity and cannot stand
                // in for this new skill mode's native stat query.
                int percentage=75;
                if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
                    for(const auto &[id,state]:snapshot->second.states) {(void)id;for(const auto &stat:state.stats)
                        if(stat.id==monsterText->nativeStats->names.at("velocitypercent") && !stat.parameter) percentage=nativeAdd(percentage,nativeChecked(stat.value));}
                const auto raw=nativePercentage(std::max(256,nativeShiftLeft(velocity,8)),percentage,100);
                run.velocity=float(raw)*25.f/4096.f;
                const Vec center{.5f,.5f};
                auto path=movementGrid.nativeUnitPath(m.position-origin+center,run.destination-origin+center,1,5,collision);
                m.route.clear();for(const auto point:path) m.route.push_back(point+origin-center);
                m.goal=run.destination;m.pathStep.reset();run.moving=true;
            } else if(frame==parameter(4)) {
                run.animation.remaining=nativeShiftLeft(parameter(5),8);run.animation.frame=nativeShiftLeft(parameter(6),8);
            }
        }
        run.event=advanceMonsterAnimation(run.animation,int(sequence->second.size()),256,0,true,sequence->second).event;
    }
    return true;
}

Vec RemoteScene::Impl::displayPosition(const OnlineUnit &u, const Map &map, OnlinePoint mapOrigin,const RemoteUiClients &clients) {
    auto &m = motion[u.key];
    if(!advancingMovement) return m.position+m.correction;
    if (m.assignmentRevision != u.assignmentRevision) {
        m = {};
        m.assignmentRevision = u.assignmentRevision;
    }
    if(u.key.type==0 && m.updatedAt<0) {
        const float angle=-7.5f*2.f*pi/64.f;
        m.look={-std::sin(angle),std::cos(angle)};
    }
    if(u.key.type==1 && !m.boneWallDirection && playerId)
        if(const auto row=monsterRows.find(unitClass(u).value_or(UINT16_MAX));row!=monsterRows.end() &&
            monstats.value(row->second,"Id")=="bonewall") {
            // Native D2Client 1.13c RVA 4CF2A rolls the local player's visual
            // seed once on assignment, then PATH_SetDirection(random & 63).
            const OnlineUnitKey owner{0,*playerId};
            auto &random=boneWallVisualRandom.try_emplace(owner,initialRandom(owner.id)).first->second;
            m.boneWallDirection=uint8_t(rollRandom(random)&63);
            const float angle=(float(*m.boneWallDirection)-7.5f)*2.f*pi/64.f;
            m.look={-std::sin(angle),std::cos(angle)};
        }
    const Vec target{float(u.position->x), float(u.position->y)};
    const Vec origin{float(mapOrigin.x), float(mapOrigin.y)};
    struct CollisionUpdate {
        Impl &scene;const OnlineUnit &unit;
        ~CollisionUpdate() {scene.syncCollision(unit);}
    } collisionUpdate{*this,u};
    m.playerKnockback=false;
    const float frameTime = m.updatedAt < 0 ? 0 : movementElapsed;
    const float elapsed = std::min(frameTime,.1f);
    m.updatedAt = time;
    const bool own = u.key.type == 0 && u.key.id == playerId;
    if (m.npcClickStopped && (m.actionRevision != u.actionRevision || m.positionRevision != u.positionRevision ||
        m.discontinuity != u.positionDiscontinuity)) m.npcClickStopped = false;
    auto request = own ? world().movementRequest : std::nullopt;
    // An intent is not an accepted mode change. Preserve the cast's precise
    // origin and pending Clt missiles until its input lock ends, including
    // requests submitted outside the world mouse controller.
    if(request && localCastBlocksMovement()) request.reset();
    bool approachingCast = false;
    if (own && !request && localCast && localCast->started < 0 && castMeleeRange(u, localCast->command)) {
        approachingCast = true;
        request = OnlineMovementRequest{{}, localCast->command.target, true,
            localCast->revision};
    }
    const auto rule = movementRule(u);
    const bool alive = u.key.type == 0 ? !playerDead(u) : !onlineMonsterCorpse(u);
    const bool frozenMonster=u.key.type==1 && alive && hasVisualState(u.key,"freeze");
    const bool ordinaryMonsterWalk=u.key.type==1 && !u.actionSkill && (u.mode==2 || u.mode==15);
    // The wire action remains in the read-only replica after the local
    // path has ended. Only a new action may start another ordinary walk.
    const bool finishedMonsterWalk=ordinaryMonsterWalk &&
        (m.finishedMonsterMovement==u.actionRevision || u.movementTargetMissing);
    const bool monsterCheck=u.key.type==1 && u.positionCheckMode.has_value();
    const bool stopSample=monsterCheck && u.mode==1 && !u.actionSkill && u.wireAction==7 &&
        m.actionRevision!=u.actionRevision;
    if(m.actionRevision!=u.actionRevision || m.npcClickStopped || !alive) m.stopDestination.reset();
    // A handoff suppresses the old native walk after an attack starts. It
    // must not suppress the next attack's approach when its target escaped:
    // packet/world revisions may advance during the preceding animation.
    if (m.castHandoff && (!alive || approachingCast || (request && request->revision > m.castHandoff->revision) ||
        (u.actionRevision > m.castHandoff->revision &&
         (u.nativeMode ? (u.mode == 0 || u.mode == 4 || u.mode == 17 || u.mode == 18)
                       : (u.mode == 6 || u.mode == 8 || u.mode == 9 || u.mode == 18)))))
        m.castHandoff.reset();
    const bool beginCast = own && localCast && localCast->started >= 0 &&
        localCast->revision > m.castRevision;
    bool activeRequest = alive && request && request->revision > m.invalidatedRequest &&
        request->revision > u.actionRevision && !world().npcConversation && !world().waypointSource;
    if (!m.last || *m.last != *u.position || (request && request->revision != m.requestRevision))
        m.progressAt = time;
    bool corrected = false;
    if (!m.last) {
        m.position = target;
    } else if (m.discontinuity != u.positionDiscontinuity) {
        m.position = target; m.correction = {}; m.correctionLeft = 0;
        m.stopDestination.reset();
        m.pathStep.reset(); m.pathElapsed=0;
        m.effectivePathType.reset();m.staminaExhausted=false;m.runningRevision=0;m.requestedRunning=false;
        m.route.clear(); m.samples.clear(); m.goal.reset(); m.pathMoving = false;
        if(pd2 || m.playerLeap || (u.key.type==0 && m.scriptedSequence)) {
            m.invalidatedSkillMovement=own && localCast?localCast->revision:u.actionRevision;
            if(own && localCast && (m.playerLeap || m.pd2PlayerCharge)) localCast.reset();
            m.playerLeap.reset();m.pd2PlayerCharge.reset();m.scriptedSequence.reset();
            m.leapFlying=false;m.leapLanding=-1;
        }
        m.castHandoff.reset();
        if (beginCast && localCast) m.castRevision = localCast->revision;
        if (request) m.invalidatedRequest = request->revision;
        activeRequest = false;
    } else if(monsterCheck && m.positionRevision!=u.positionRevision && alive) {
        // Retail 1.13c AF2B0 -> 2A1B0; action 7 (4E981..4EA01)
        // approaches a nearby stop on a real path when either axis exceeds 1.
        const int dx=std::abs(int(std::floor(m.position.x+.5f))-int(u.position->x));
        const int dy=std::abs(int(std::floor(m.position.y+.5f))-int(u.position->y));
        const int error=std::max(dx,dy),mode=*u.positionCheckMode;
        const int tolerance=(mode>=3 && mode<=5)?5:(mode>=6 && mode<=11)?7:15;
        m.positionCheckResult="within-tolerance";
        if(stopSample) {m.route.clear();m.goal.reset();m.pathMoving=false;}
        // 2A1B0 requires nonzero coordinates and a real destination room
        // before Common #10906 relocates a remote monster and its footprint.
        const auto localTarget=target-origin+Vec{.5f,.5f};
        const bool knownPosition=u.position->x && u.position->y &&
            map.grid.navigationRoom(localTarget).has_value();
        if(error>tolerance && knownPosition) {
            m.position=target;m.correction={};m.correctionLeft=0;m.pathStep.reset();m.pathElapsed=0;
            m.stopDestination.reset();corrected=true;
            m.positionCheckResult="relocated";
        } else if(stopSample && error>1 && knownPosition && u.pathType && u.velocityPercent && !m.npcClickStopped) {
            const auto row=monsterRows.find(unitClass(u).value_or(UINT16_MAX));
            if(row!=monsterRows.end()) {
                const auto extra=monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
                const auto effects=combatBinding->states().find(u.key);
                const bool attached=effects!=combatBinding->states().end() && effects->second.states.contains(0x8f);
                if(!attached && extra!=monsterExtra.end() && monstats2.number(extra->second,"mWL").value_or(0)) {
                    m.stopDestination=target;m.positionCheckResult="stop-approach";
                }
            }
        }
        if(!knownPosition) m.positionCheckResult="unavailable-position";
    } else if (own && !request && m.requestRevision && !beginCast) {
        // An interaction intent stops the local approach before the next
        // sparse resource sample. Retain its precise endpoint until an
        // actual position correction/new action replaces it.
        const Vec displayed = m.position + m.correction;
        m.castHandoff = CastHandoff{world().revision, *u.position, m.goal ? *m.goal - displayed : m.look};
        m.position = displayed; m.correction = {}; m.correctionLeft = 0;
        m.route.clear(); m.goal.reset(); m.pathMoving = false;
    } else if (beginCast) {
        // PlrModes changes the action at the current precise position. A
        // canceled walk request is not a native position correction.
        const Vec displayed = m.position + m.correction;
        m.castHandoff = CastHandoff{localCast->revision, *u.position,
            m.goal ? *m.goal - displayed : m.look};
        m.castRevision = localCast->revision;
        m.position = displayed; m.correction = {}; m.correctionLeft = 0;
        m.route.clear(); m.goal.reset(); m.pathMoving = false;
    } else if (u.key.type==1 && u.mode==0 && m.actionRevision!=u.actionRevision) {
        // Death interrupts the path at the current displayed cell.
        m.position=m.position+m.correction;
        m.correction={};m.correctionLeft=0;m.pathStep.reset();m.pathElapsed=0;
        m.route.clear();m.goal.reset();m.pathMoving=false;m.scriptedSequence.reset();
    } else if (u.key.type == 1 && !walking(u) && m.actionRevision != u.actionRevision &&
               m.positionRevision == u.positionRevision) {
        // Target-only monster actions (including 4C/4D skills)
        // stop the path without supplying a current position. Keep the
        // continuous feet instead of returning to the last native sample.
        // Fresh position samples, including DEAD, still reconcile below.
        m.pathMoving = false;
    } else if ((m.positionRevision != u.positionRevision && (u.key.type==1 || *m.last != *u.position) &&
                !(own && u.positionRevision==u.pathVerificationRevision)) ||
               (m.actionRevision != u.actionRevision && !walking(u)) ||
               (own && m.requestRevision && !request && m.goal)) {
        const Vec displayed = m.position + m.correction;
        // Ordinary path samples may lag behind continuous presentation.
        // Keep progress along the current collision-checked leg instead of
        // moving back and playing that leg again on every sample. Native
        // discontinuities and newer stop actions still replace prediction.
        const Vec ahead = m.goal ? *m.goal - displayed : Vec{};
        const Vec lag = displayed - target;
        const bool previousPath = activeRequest && request->destination && u.verifiedDestination &&
            (std::abs(int(request->destination->x)-u.verifiedDestination->x)>1 ||
             std::abs(int(request->destination->y)-u.verifiedDestination->y)>1);
        const bool seenOnPath = (activeRequest || m.castHandoff) && std::any_of(m.samples.begin(),m.samples.end(),[&](const auto &sample) {
            return time-sample.first <= onlineMovementProgressTimeoutSeconds &&
                std::abs(sample.second.x-target.x)<1.f && std::abs(sample.second.y-target.y)<1.f;
        });
        const bool stoppedWalkSample = m.castHandoff &&
            (*u.position == m.castHandoff->nativePosition ||
             (walking(u) && m.castHandoff->direction.length() > .001f &&
              lag.x * m.castHandoff->direction.x + lag.y * m.castHandoff->direction.y >= 0 &&
              std::abs(lag.x * m.castHandoff->direction.y - lag.y * m.castHandoff->direction.x) <=
                  std::max(1.f, m.castHandoff->direction.length()) &&
              map.grid.segment(target - origin, displayed - origin, {}, rule)));
        const bool behind = previousPath || seenOnPath || stoppedWalkSample || (u.key.type==0 && m.goal && (activeRequest || walking(u)) &&
            lag.x * ahead.x + lag.y * ahead.y >= 0 &&
            std::abs(lag.x * ahead.y - lag.y * ahead.x) <= 2.f * std::max(1.f, ahead.length()) &&
            map.grid.segment(target - origin, displayed - origin, {}, rule));
        if (!behind && (m.position.x!=target.x || m.position.y!=target.y)) {
            m.position = target; m.correction = displayed - target; m.correctionLeft = .12f;
            corrected = true;
        }
        // A correction must not carry a sprite through a closed door or wall.
        if (!map.grid.segment(target - origin, displayed - origin, {}, rule)) m.correction = {};
    }
    if(u.key.type==1 && u.wireAction==6 && m.actionRevision!=u.actionRevision)
        m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
    syncCollision(u);
    // Keep the old native walking destination from restarting motion after
    // the owner starts a cast (the server normally omits its skill packet).
    if (m.castHandoff) activeRequest = false;
    if(displayPlayerLeap(u,m,map,origin) || (pd2 && displayPd2PlayerCharge(u,m,map,origin))) {
        m.actionRevision=u.actionRevision;m.last=u.position;m.positionRevision=u.positionRevision;m.discontinuity=u.positionDiscontinuity;
        m.requestRevision=request?request->revision:0;
        return m.position+m.correction;
    }
    if(displayPlayerWhirlwind(u,m,map,origin) || displayScriptedSequence(u,m,map,origin,rule)) {
        m.pathMoving=m.scriptedSequence && m.scriptedSequence->moving;
        m.actionRevision=u.actionRevision;m.last=u.position;m.positionRevision=u.positionRevision;m.discontinuity=u.positionDiscontinuity;
        return m.position+m.correction;
    }
    auto pointForUnit = [&](const std::optional<OnlineUnitKey> &key) -> std::optional<Vec> {
        if (!key) return {};
        if (key->type == 4) {
            const auto item = world().items.find(key->id);
            if (item == world().items.end() || item->second.mode != 3) return {};
            return Vec{float(item->second.groundX), float(item->second.groundY)};
        }
        if (key->type <= 1) return localUnitPosition(*key,false);
        const auto found = world().units.find(*key);
        if (found == world().units.end() || !found->second.position) return {};
        const auto p = *found->second.position; return Vec{float(p.x), float(p.y)};
    };
    std::optional<Vec> goal;
    bool requested = activeRequest;
    if (requested && time - m.progressAt >= onlineMovementProgressTimeoutSeconds) {
        // PlrMsg may omit unchanged resources and small position deltas.
        // Finishing a predicted leg does not imply rejection: wait for the
        // same lack-of-native-progress timeout as navigation, not travel time.
        m.invalidatedRequest = request->revision; requested = false;
        // Missing progress is not a teleport. Stop at the local precise
        // position; 5F/15 owns reconciliation with the server.
        m.position=m.position+m.correction;m.correction={};m.correctionLeft=0;
    }
    if (requested) {
        if (request->destination) goal = Vec{float(request->destination->x), float(request->destination->y)};
        else goal = pointForUnit(request->unit);
        // 18/95/96 verify the native path; they neither replace its target
        // nor reposition its owner. D2Client 1.13c RVA 2A1B0 sends 5F on
        // excessive drift and leaves precise movement to the local path.
        if(request->revision!=m.requestRevision || request->run!=m.requestedRunning || request->runningRevision!=m.runningRevision) {
            m.staminaExhausted=world().stamina && !*world().stamina;
            m.running=request->run && !m.staminaExhausted;
        }
        m.requestedRunning=request->run;
        m.runningRevision=request->runningRevision;
    } else if(m.stopDestination) {
        goal=m.stopDestination;m.running=false;
    } else if (alive && walking(u) && !finishedMonsterWalk && !m.npcClickStopped && !m.castHandoff && !(own && request &&
               request->revision <= m.invalidatedRequest && u.actionRevision < request->revision)) {
        if (u.destination) goal = Vec{float(u.destination->x), float(u.destination->y)};
        else goal = pointForUnit(u.destinationUnit);
        m.running = u.key.type == 0 ? (u.nativeMode ? u.mode == 3 : (u.mode == 23 || u.mode == 24)) : u.mode == 15;
    }
    // PathMisc::sub_6FD5DB70 checks an ordinary player's target unit on
    // every movement step. A plain 02/04 has the same nStepNum=0 rule as
    // an interaction approach; it must not keep drawing into the NPC.
    const auto playerTarget = u.key.type == 0 ? (requested ? request->unit : u.destinationUnit) : std::nullopt;
    const auto playerUnitGoal = playerTarget ? pointForUnit(playerTarget) : std::nullopt;
    int playerTargetSize = -1;
    if (playerUnitGoal) {
        const auto targetUnit = world().units.find(*playerTarget);
        if (targetUnit != world().units.end() && playerTarget->type<=1) playerTargetSize = unitSize(targetUnit->second);
        else if(playerTarget->type==4 || playerTarget->type==5) playerTargetSize=1;
        else if(playerTarget->type==2 && sceneBinding)
            for(const auto &entry:sceneBinding->mapTargets) if(entry.unit==*playerTarget) {playerTargetSize=entry.collisionWidth;break;}
    }
    const auto playerPathArrived = [&](Vec position) {
        return playerUnitGoal && playerTargetSize >= 0 &&
            nativeUnitDistance(position, rule.size, *playerUnitGoal, playerTargetSize) == 0;
    };
    const auto monsterPathArrived = [&](Vec position) {
        if(u.key.type!=1 || !u.destinationUnit) return false;
        const auto targetUnit=world().units.find(*u.destinationUnit);
        const auto point=pointForUnit(u.destinationUnit);
        if(targetUnit==world().units.end() || !point) return false;
        const int targetSize=unitSize(targetUnit->second);
        return targetSize>0 && nativeUnitDistance(position,unitSize(u),*point,targetSize)<=u.monsterPathStopDistance;
    };
    if (goal && playerPathArrived(m.position)) {
        m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
        goal.reset();
    }
    if (own && request && request->unit && goal && request->interaction) {
        const auto key = *request->unit;
        const Vec position = m.position + m.correction;
        bool reached = false;
        if (key.type == 4) {
            const auto point = pointForUnit(request->unit);
            reached = point && nativePickupReach(map.grid,position-origin,*point-origin,{});
        } else if (sceneBinding) {
            const auto found = std::find_if(sceneBinding->mapTargets.begin(), sceneBinding->mapTargets.end(),
                [&](const auto &entry) { return entry.unit == key; });
            if (found != sceneBinding->mapTargets.end()) {
                const Vec point{float(found->position.x), float(found->position.y)};
                if (key.type == 2)
                    reached = interactionClear(map.grid, position - origin,
                        {EntityId{uint64_t(key.id) + 1}, point - origin, point - origin,
                            found->collisionWidth, found->collisionHeight, 0, true});
                else if (key.type > 1) reached = nativeUnitDistance(position, 2, point, found->collisionWidth) <= 4;
            }
        }
        if (reached) goal.reset();
    }
    if(goal && !frozenMonster && monsterPathArrived(m.position)) {
        m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
        goal.reset();
    }
    if (u.key.type == 1 && u.pathType && (*u.pathType == 8 || *u.pathType == 9 || *u.pathType == 11)) goal.reset();
    const bool playerKnocked=alive && !requested && u.key.type==0 && (u.nativeMode?u.mode==19:u.mode==20);
    const bool knocked=playerKnocked || (alive && u.key.type==1 && u.mode==13 &&
        m.finishedMonsterKnockback!=u.actionRevision && u.pathType && (*u.pathType==8 || *u.pathType==11));
    if(playerKnocked && alive && u.destination) {
        if(m.actionRevision!=u.actionRevision) {
            const Vec center{.5f,.5f};
            const Vec destination{float(u.destination->x),float(u.destination->y)};
            const auto grid=navigationCollision(map.grid,origin,u.key);
            m.route.clear();m.castHandoff.reset();
            m.goal=grid.nativeMovementTarget(m.position-origin+center,destination-origin+center,rule)+origin-center;
            m.route.push_back(*m.goal);
            m.pathElapsed=0;
        }
        goal=m.goal;m.running=false;
    } else if(knocked && alive && !m.castHandoff) {
        const auto endpoint=u.destination ? std::optional{Vec{float(u.destination->x),float(u.destination->y)}}:pointForUnit(u.destinationUnit);
        if(endpoint && m.actionRevision!=u.actionRevision) {
            const Vec center{.5f,.5f};
            const auto grid=navigationCollision(map.grid,origin,u.key);
            const auto destination=*u.pathType==8?knockbackDestination(m.position,*endpoint,(u.pathSteps.value_or(0)>>1)+1):*endpoint;
            m.route.clear();m.goal=grid.nativeMovementTarget(m.position-origin+center,destination-origin+center,rule)+origin-center;
            m.route.push_back(*m.goal);m.pathElapsed=0;
        }
        goal=m.goal;
    }
    if(u.key.type==0) m.movementTiming=playerTiming(u,m.running,clients);
    float speed = knocked ? 25.f : movementSpeed(u);
    float pathElapsed=u.key.type==0?frameTime:elapsed;
    if(knocked) pathElapsed=(m.actionRevision!=u.actionRevision || corrected)?(frameTime>.1f?0:frameTime):frameTime;
    if (goal && !knocked) {
        const auto unitTarget=u.key.type==0?playerTarget:u.destinationUnit;
        const bool changedTarget = !m.goal || (unitTarget
            ? nativeUnitTargetMoved(*m.goal,*goal,unitTarget->type)
            : (*m.goal - *goal).length() > .5f);
        // A retained monster path runs on game ticks, even across a slow
        // render frame. Do not charge that gap to a new order or position.
        if(u.key.type==1 && !changedTarget && !m.route.empty() && m.positionRevision==u.positionRevision &&
            m.actionRevision==u.actionRevision && m.discontinuity==u.positionDiscontinuity)
            pathElapsed=frameTime;
        // An admitted ordinary input recomputes the native path even if
        // its coordinate is unchanged, just as StartXY/StartID on authority.
        const bool newRequest=requested && request->revision!=m.requestRevision;
        if(newRequest || corrected || m.actionRevision!=u.actionRevision) pathElapsed=frameTime>.1f?0:frameTime;
        if ((changedTarget && !frozenMonster) || corrected || newRequest ||
            m.actionRevision != u.actionRevision)
            plan(m, *goal, map, origin, rule, speed, u,
                requested?request->unit:u.destinationUnit);
        if(m.route.empty() && u.key.type<=1)
            m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
    } else if(!knocked) { m.route.clear(); m.goal.reset(); }
    m.actionRevision = u.actionRevision;
    m.requestRevision = request ? request->revision : 0;
    std::optional<Grid> stepCollision;
    const auto advanceRoute = [&](float remaining) {
        if(!stepCollision) stepCollision.emplace(navigationCollision(map.grid,origin,u.key));
        const auto &movementGrid=*stepCollision;
        while (!m.route.empty() && remaining > 0) {
            if (!knocked && (playerPathArrived(m.position) || monsterPathArrived(m.position))) {
                m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
                m.route.clear(); m.goal.reset(); break;
            }
            const auto next = m.route.front(), delta = next - m.position;
            const float distance = delta.length();
            if (u.key.type <= 1) {
                const Vec center{.5f, .5f};
                const auto previous=m.position;
                const auto step = m.pathStep.advance(movementGrid, m.position + center,
                    next + center, remaining, (playerKnocked || (knocked && u.pathType==11))?MovementCollisionRule{0,rule.size}:rule, origin);
                // Consuming an already reached path point supplies no new facing.
                if (delta.x != 0 || delta.y != 0) m.look = knocked?delta*-1.f:delta;
                m.position = step.position - center;
                if (step.blocked) {
                    m.pathResult="blocked";
                    if(u.key.type==1 && !knocked && m.goal) {
                        // Step::sub_6FDACEC0 retries only client monsters
                        // (path flag 0x10), through Common D820. Planning
                        // reads the old precise position before RecacheRoom
                        // installs the collision-clipped position. Consume
                        // this tick even if the replacement path succeeds.
                        const auto destination=*m.goal;
                        m.position=previous;
                        plan(m,destination,map,origin,rule,speed,u,u.destinationUnit,true);
                        m.position=step.position-center;
                        ++m.collisionReplans;
                        m.pathResult=m.route.empty()?"blocked-no-path":"collision-replanned";
                    } else m.route.clear();
                    break;
                }
                if (!step.reached) break;
                remaining -= step.travelled; m.route.pop_front();
                if(m.route.empty()) m.pathResult="arrived";
                if (step.travelled > 0) break;
                continue;
            }
            const auto step = distance <= remaining ? next : m.position + delta.unit() * remaining;
            const Vec center{.5f,.5f};
            const bool clear = movementGrid.segment(m.position-origin+center,step-origin+center,{},rule);
            if (!clear) { m.route.clear(); break; }
            if (distance > 0) m.look = delta;
            m.position = step;
            if (distance > remaining) break;
            remaining -= distance; m.route.pop_front();
        }
    };
    if (u.key.type <= 1) {
        // Ordinary dynamic paths use the same 25Hz step as authority, while
        // presentation frames only accumulate time for that simulation.
        if (!frozenMonster && (m.route.empty() || speed<=0)) m.pathElapsed = 0;
        else if(!frozenMonster) {
            m.pathElapsed=0;
            if(pathElapsed>0) {
                advanceRoute(speed * NativePathStep::seconds);
                if(!knocked && own && requested && m.running && world().stamina && !*world().stamina) {
                    m.running=false;m.staminaExhausted=true;
                    m.movementTiming=playerTiming(u,false,clients);speed=movementSpeed(u);
                    if(goal && !m.route.empty()) plan(m,*goal,map,origin,rule,speed,u,playerTarget);
                }
            }
        }
    } else advanceRoute(speed * elapsed);
    if (m.correctionLeft > 0) {
        m.correction = m.correction * (1.f - std::min(1.f, elapsed / m.correctionLeft));
        m.correctionLeft = std::max(0.f, m.correctionLeft - elapsed);
    }
    // D2Client's native mode loop consumes PATH's completion result in the
    // same game frame. Keep walking between 25Hz steps, but never add a
    // rendering-time grace period after arrival, collision or cancellation.
    m.pathMoving=!m.route.empty() && speed>0;
    if(ordinaryMonsterWalk && !m.pathMoving && (!frozenMonster || m.route.empty())) {
        // Common E259/E325 and ResetMovement: arrival, collision, failed
        // planning or an unresolved target consume this order permanently.
        m.finishedMonsterMovement=u.actionRevision;
        m.route.clear();m.goal.reset();m.pathElapsed=0;
        m.position=m.pathStep.stop(m.position+Vec{.5f,.5f})-Vec{.5f,.5f};
    }
    m.playerKnockback=playerKnocked && m.pathMoving;
    if(!m.pathMoving) m.stopDestination.reset();
    m.last = u.position;
    m.positionRevision = u.positionRevision;
    m.discontinuity = u.positionDiscontinuity;
    if (own) {
        if (m.samples.empty() || time-m.samples.back().first >= 1.f/25.f)
            m.samples.emplace_back(time,m.position+m.correction);
        while (!m.samples.empty() && time-m.samples.front().first > onlineMovementProgressTimeoutSeconds)
            m.samples.pop_front();
    }
    return m.position + m.correction;
}
} // namespace d2x
