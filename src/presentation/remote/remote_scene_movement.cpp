#include "remote_scene_impl.hpp"
#include "gameplay/rules/pd2_phasing.hpp"

namespace d2x {

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

Grid RemoteScene::Impl::navigationCollision(const Grid &terrain,Vec origin,OnlineUnitKey self,
                             std::optional<OnlineUnitKey> target) const {
    using Cell=std::pair<int,int>;
    std::vector<std::pair<Cell,uint16_t>> occupied;
    occupied.reserve(5*world().units.size());
    for(const auto &[key,unit]:world().units) {
        if(key.type>1 || key==self || !unit.position) continue;
        const bool dead=key.type==0?playerDead(unit):onlineMonsterCorpse(unit);
        if(target && key==*target && !dead) continue;
        auto pattern=nativeUnitCollisionPattern(unitSize(unit));
        uint16_t mask=key.type==0?0x80:0x100;
        if(key.type==0 && dead) {pattern=NativeCollisionPattern::SmallNoPresence;mask=0x8000;}
        if(key.type==1) {
            const auto row=monsterRows.find(unitClass(unit).value_or(UINT16_MAX));
            if(row==monsterRows.end()) continue;
            const auto ai=monstats.value(row->second,"AI");
            if(unit.mode==8 && !unit.actionSkill && (ai=="Vulture" || ai=="SandMaggot" || ai=="FrogDemon")) continue;
            const auto pose=motion.find(key);
            if(pose!=motion.end() && pose->second.scriptedSequence && pose->second.scriptedSequence->leapGraphics) continue;
            const bool town=monstats.number(row->second,"npc").value_or(0) || monstats.number(row->second,"inTown").value_or(0);
            pattern=nativeUnitCollisionPattern(unitSize(unit),town && !monstats.number(row->second,"interact").value_or(0));
            if(monstats.value(row->second,"BaseId")=="wraith1") pattern=NativeCollisionPattern::SmallNoPresence;
            const auto extra=monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
            if(dead && (extra==monsterExtra.end() || !monstats2.number(extra->second,"deadCol").value_or(0))) {
                pattern=NativeCollisionPattern::SmallNoPresence;mask=0x8000;
            }
        }
        const auto point=localUnitPosition(key);if(!point) continue;
        const int x=int(std::floor(point->x+.5f)),y=int(std::floor(point->y+.5f));
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(const auto value=nativeCollisionFootprint(pattern,dx,dy,mask)) occupied.emplace_back(Cell{x+dx,y+dy},value);
    }
    std::sort(occupied.begin(),occupied.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    size_t count{};
    for(const auto &entry:occupied) {
        if(count && occupied[count-1].first==entry.first) occupied[count-1].second|=entry.second;
        else occupied[count++]=entry;
    }
    occupied.resize(count);
    Grid view;view.width=terrain.width;view.height=terrain.height;
    view.neighbours=terrain.neighbours;
    view.obstacleRevision=terrain.obstacleRevision;view.missileCollisionRevision=terrain.missileCollisionRevision;
    const EntityId object=target && target->type==2?EntityId{uint64_t(target->id)+1}:EntityId{};
    Vec navigationOrigin;auto navigation=terrain.linkedNavigation(navigationOrigin);
    view.collisionSample=[navigation=std::move(navigation),navigationOrigin,origin,object,occupied=std::move(occupied)](int x,int y,EntityId ignored)->uint16_t {
        uint16_t mask=navigation.movementMask(x-int(navigationOrigin.x),y-int(navigationOrigin.y),ignored?ignored:object);
        const Cell key{x+int(origin.x),y+int(origin.y)};
        const auto found=std::lower_bound(occupied.begin(),occupied.end(),key,
            [](const auto &entry,const Cell &value){return entry.first<value;});
        return found==occupied.end() || found->first!=key?mask:uint16_t(mask|found->second);
    };
    return view;
}

void RemoteScene::Impl::plan(Motion &m, Vec goal, const Map &map, Vec origin, MovementCollisionRule rule, float speed,
          const OnlineUnit &unit, std::optional<OnlineUnitKey> target) {
    m.route.clear(); m.goal = goal;
    m.pathStep.reset(); m.pathElapsed = 0;
    if (speed <= 0 || rule.size <= 0) return;
    // Native precise coordinates target cell centers; presentation adds
    // its half-cell offset when drawing. Match the control planner here.
    const Vec center{.5f, .5f};
    const bool nativeMonster=unit.key.type==1 && unit.pathType && unit.pathDistance &&
        (*unit.pathType==1 || *unit.pathType==2 || *unit.pathType==7 || *unit.pathType==13 || *unit.pathType==15);
    const bool removeTarget=unit.key.type==0 || (unit.pathType && (*unit.pathType==1 || *unit.pathType==7 || *unit.pathType==15));
    const auto grid=navigationCollision(map.grid,origin,unit.key,removeTarget?target:std::nullopt);
    auto path = unit.key.type==0
        ? grid.nativePlayerPath(m.position - origin + center, goal - origin + center, rule,target?target->type:255)
        : nativeMonster ? grid.nativeUnitPath(m.position-origin+center,goal-origin+center,*unit.pathType,*unit.pathDistance,rule,unit.destinationUnit.has_value())
        : grid.path(m.position - origin + center, goal - origin + center, true, rule);
    for (auto point : path)
        m.route.push_back(point + origin - center);
}

bool RemoteScene::Impl::displayScriptedSequence(const OnlineUnit &u,Motion &m,const Map &map,Vec origin,MovementCollisionRule collision) {
    const auto skill=u.actionSkill?skillRows.find(*u.actionSkill):skillRows.end();
    if(pd2 && u.key.type==1 && skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==37)
        return displayPd2MonsterCharge(u,m,map,origin);
    const bool whirlwind=skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==45;
    const bool pd2Leap=pd2 && skill!=skillRows.end() && skills.number(skill->second,"cltdofunc")==97;
    if(pd2Leap && m.pd2InvalidatedMovement==u.actionRevision) return false;
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
            const auto landing=findPd2LeapLanding(u,m.position,*destination,*range,map,origin);
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
                    int frames=distance*16/speed;if(frames>1) --frames;frames=std::max(1,frames);
                    ClientMissileGraphics graphics;graphics.flags=2;graphics.remaining=frames;
                    const int gravity=std::max(500,distance<27?8462-270*distance:1224-72*distance);
                    graphics.acceleration[2]=-gravity;graphics.velocity[2]=nativeDivide(nativeMultiply(nativeMultiply(frames,frames),gravity)/2,frames);
                    run.leapGraphics=graphics;run.leapFlags=0x1101;run.velocity=float(speed)*25.f/16.f;
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
    if (m.assignmentRevision != u.assignmentRevision) {
        m = {};
        m.assignmentRevision = u.assignmentRevision;
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
    // Camera and actor rendering consult this method in the same frame.
    if (m.updatedAt == time) return m.position + m.correction;
    const float elapsed = m.updatedAt < 0 ? 0 : std::clamp(time - m.updatedAt, 0.f, .1f);
    m.updatedAt = time;
    const bool own = u.key.type == 0 && u.key.id == playerId;
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
        m.route.clear(); m.samples.clear(); m.goal.reset(); m.pathMoving = false;
        if(pd2) {
            m.pd2InvalidatedMovement=own && localCast?localCast->revision:u.actionRevision;
            if(own && localCast && (m.pd2PlayerLeap || m.pd2PlayerCharge)) localCast.reset();
            m.pd2PlayerLeap.reset();m.pd2PlayerCharge.reset();m.scriptedSequence.reset();
            m.leapFlying=false;m.leapLanding=-1;
        }
        m.castHandoff.reset();
        if (beginCast && localCast) m.castRevision = localCast->revision;
        if (request) m.invalidatedRequest = request->revision;
        activeRequest = false;
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
    } else if (u.key.type == 1 && !walking(u) && m.actionRevision != u.actionRevision &&
               m.positionRevision == u.positionRevision) {
        // Target-only monster actions (including 4C/4D skills and DEATH)
        // stop the path without supplying a current position. Keep the
        // continuous feet instead of returning to the last native sample.
        // Fresh position samples, including DEAD, still reconcile below.
        m.position = m.position + m.correction;
        m.correction = {}; m.correctionLeft = 0; m.pathMoving = false;
    } else if ((m.positionRevision != u.positionRevision && *m.last != *u.position &&
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
        const bool behind = previousPath || seenOnPath || stoppedWalkSample || (m.goal && (activeRequest || walking(u)) &&
            lag.x * ahead.x + lag.y * ahead.y >= 0 &&
            std::abs(lag.x * ahead.y - lag.y * ahead.x) <= 2.f * std::max(1.f, ahead.length()) &&
            map.grid.segment(target - origin, displayed - origin, {}, rule));
        if (!behind) {
            m.position = target; m.correction = displayed - target; m.correctionLeft = .12f;
            corrected = true;
        }
        // A correction must not carry a sprite through a closed door or wall.
        if (!map.grid.segment(target - origin, displayed - origin, {}, rule)) m.correction = {};
    }
    // Keep the old native walking destination from restarting motion after
    // the owner starts a cast (the server normally omits its skill packet).
    if (m.castHandoff) activeRequest = false;
    if(pd2 && (displayPd2PlayerLeap(u,m,map,origin) || displayPd2PlayerCharge(u,m,map,origin))) {
        m.actionRevision=u.actionRevision;m.last=u.position;m.positionRevision=u.positionRevision;m.discontinuity=u.positionDiscontinuity;
        m.requestRevision=request?request->revision:0;
        return m.position+m.correction;
    }
    if(displayScriptedSequence(u,m,map,origin,rule)) {
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
        if (key->type <= 1) return localUnitPosition(*key);
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
        m.running = request->run && (!world().stamina || *world().stamina != 0);
    } else if (alive && walking(u) && !m.castHandoff && !(own && request &&
               request->revision <= m.invalidatedRequest && u.actionRevision < request->revision)) {
        if (u.destination) goal = Vec{float(u.destination->x), float(u.destination->y)};
        else goal = pointForUnit(u.destinationUnit);
        m.running = u.key.type == 0 ? (u.nativeMode ? u.mode == 3 : (u.mode == 23 || u.mode == 24)) : u.mode == 15;
    }
    // PathMisc::sub_6FD5DB70 checks an ordinary player's target unit on
    // every movement step. A plain 02/04 has the same nStepNum=0 rule as
    // an interaction approach; it must not keep drawing into the NPC.
    const auto playerTarget = u.key.type == 0 ? (requested ? request->unit : u.destinationUnit) : std::nullopt;
    const auto playerUnitGoal = playerTarget && playerTarget->type <= 1 ? pointForUnit(playerTarget) : std::nullopt;
    int playerTargetSize = 0;
    if (playerUnitGoal) {
        const auto targetUnit = world().units.find(*playerTarget);
        if (targetUnit != world().units.end()) playerTargetSize = unitSize(targetUnit->second);
    }
    const auto playerPathArrived = [&](Vec position) {
        return playerUnitGoal && playerTargetSize > 0 &&
            nativeUnitDistance(position, rule.size, *playerUnitGoal, playerTargetSize) == 0;
    };
    const auto monsterPathArrived = [&](Vec position) {
        if(u.key.type!=1 || !u.destinationUnit || !u.pathSteps || !*u.pathSteps) return false;
        const auto targetUnit=world().units.find(*u.destinationUnit);
        const auto point=pointForUnit(u.destinationUnit);
        if(targetUnit==world().units.end() || !point) return false;
        const int targetSize=unitSize(targetUnit->second);
        return targetSize>0 && nativeUnitDistance(position,unitSize(u),*point,targetSize)<=int(*u.pathSteps)-1;
    };
    if (goal && playerPathArrived(m.position)) goal.reset();
    // Plain 02/04 object paths still stop in interaction range; only the
    // later 13 request authorizes the local barrel Kick above.
    if (own && request && request->unit && goal && (request->interaction || request->unit->type == 2)) {
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
    if(goal && monsterPathArrived(m.position)) goal.reset();
    // Circle/knockback/leap paths need their own native client solver; never substitute a straight chase.
    if (u.key.type == 1 && u.pathType && (*u.pathType == 5 || *u.pathType == 6 ||
        *u.pathType == 8 || *u.pathType == 9 || *u.pathType == 11)) goal.reset();
    const bool knocked=u.key.type==1 && u.mode==13 && u.pathType && (*u.pathType==8 || *u.pathType==11);
    if(knocked && alive && !m.castHandoff) {
        auto sourcePoint=u.destination ? std::optional{Vec{float(u.destination->x),float(u.destination->y)}}:pointForUnit(u.destinationUnit);
        if(sourcePoint && m.actionRevision!=u.actionRevision) {
            m.route.clear();m.goal=knockbackDestination(m.position,*sourcePoint,u.pathSteps.value_or(0));m.route.push_back(*m.goal);
        }
        goal=m.goal;
    }
    if(u.key.type==0) m.movementTiming=playerTiming(u,m.running,clients);
    const float speed = knocked ? 25.f : movementSpeed(u);
    if (goal && !knocked) {
        const bool changedTarget = !m.goal || (u.key.type == 0 && playerTarget
            ? (std::floor(m.goal->x+.5f) != std::floor(goal->x+.5f) ||
               std::floor(m.goal->y+.5f) != std::floor(goal->y+.5f))
            : (*m.goal - *goal).length() > .5f);
        if (changedTarget || corrected ||
            m.actionRevision != u.actionRevision)
            plan(m, *goal, map, origin, rule, speed, u,
                requested?request->unit:u.destinationUnit);
    } else if(!knocked) { m.route.clear(); m.goal.reset(); }
    m.actionRevision = u.actionRevision;
    m.requestRevision = request ? request->revision : 0;
    const auto advanceRoute = [&](float remaining) {
        bool obstructed = false;
        const auto movementGrid=navigationCollision(map.grid,origin,u.key);
        while (!m.route.empty() && remaining > 0) {
            if (playerPathArrived(m.position) || monsterPathArrived(m.position)) { m.route.clear(); m.goal.reset(); break; }
            const auto next = m.route.front(), delta = next - m.position;
            const float distance = delta.length();
            if (u.key.type == 0 || (u.key.type == 1 && !knocked)) {
                const Vec center{.5f, .5f};
                const auto step = m.pathStep.advance(movementGrid, m.position + center,
                    next + center, remaining, rule, origin);
                m.look = delta; m.position = step.position - center;
                if (step.blocked) { m.route.clear(); obstructed = true; break; }
                if (monsterPathArrived(m.position)) {m.route.clear();m.goal.reset();break;}
                if (!step.reached) break;
                remaining -= step.travelled; m.route.pop_front();
                if (step.travelled > 0) break;
                continue;
            }
            const auto step = distance <= remaining ? next : m.position + delta.unit() * remaining;
            const Vec center{.5f,.5f};
            const bool clear = movementGrid.segment(m.position-origin+center,step-origin+center,{},rule);
            if (!clear) { m.route.clear(); break; }
            m.look = delta;
            m.position = step;
            if (distance > remaining) break;
            remaining -= distance; m.route.pop_front();
        }
        if (!obstructed && u.key.type == 0 && playerTarget && m.route.empty() && goal) {
            std::optional<InteractionTarget> arrival;
            if (playerUnitGoal && playerTargetSize > 0)
                arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, *playerUnitGoal - origin,
                    *playerUnitGoal - origin, playerTargetSize, playerTargetSize, 0, false, true, false};
            else if (playerTarget->type == 4)
                arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, *goal - origin,
                    *goal - origin, 1, 1, 4, false, true, true};
            else if (playerTarget->type == 2 && sceneBinding) {
                const auto object = std::find_if(sceneBinding->mapTargets.begin(), sceneBinding->mapTargets.end(),
                    [&](const auto &entry) { return entry.unit == *playerTarget; });
                if (object != sceneBinding->mapTargets.end()) {
                    const Vec target{float(object->position.x), float(object->position.y)};
                    arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, target - origin,
                        target - origin, object->collisionWidth, object->collisionHeight, 0, true};
                }
            }
            if (arrival) {
                const auto pathGrid=navigationCollision(map.grid,origin,u.key,playerTarget);
                auto route = nativeTargetPath(pathGrid, m.position - origin, *arrival, playerTarget->type);
                for (auto &point : route) point = point + origin;
                m.route = std::move(route); m.pathStep.reset();
            }
        }
    };
    if (u.key.type == 0 || (u.key.type == 1 && !knocked)) {
        // Ordinary dynamic paths use the same 25Hz step as authority, while
        // presentation frames only accumulate time for that simulation.
        if (m.route.empty()) m.pathElapsed = 0;
        else {
            m.pathElapsed += elapsed;
            while (m.pathElapsed >= NativePathStep::seconds) {
                m.pathElapsed -= NativePathStep::seconds;
                advanceRoute(speed * NativePathStep::seconds);
                if (m.route.empty()) { m.pathElapsed = 0; break; }
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
