#include "remote_scene_impl.hpp"
#include "scene_direction.hpp"
#include "world/native_path_step.hpp"
#include "world/room_activation_snapshot.hpp"
#include "presentation/world/overlay_geometry.hpp"
#include <functional>

namespace d2x {
namespace {
Vec subtileCenter(Vec point) {
    return {std::floor(point.x) + .5f, std::floor(point.y) + .5f};
}
int critterDistance(Vec first, int firstSize, Vec second, int secondSize) {
    return unitDistanceWithSizes(int(first.x),int(first.y),firstSize,int(second.x),int(second.y),secondSize);
}
Vec flightEndpoint(Vec from, Vec target, const Grid &grid) {
    const Vec delta=target-from;
    const float length=std::max(std::abs(delta.x),std::abs(delta.y));
    float fraction=77.f/std::max(77.f,length);
    // Stop at the loaded extent without changing the heading.
    if (delta.x>0) fraction=std::min(fraction,(float(grid.width)-.5f-from.x)/delta.x);
    if (delta.x<0) fraction=std::min(fraction,(.5f-from.x)/delta.x);
    if (delta.y>0) fraction=std::min(fraction,(float(grid.height)-.5f-from.y)/delta.y);
    if (delta.y<0) fraction=std::min(fraction,(.5f-from.y)/delta.y);
    return subtileCenter(from+delta*fraction);
}
}

const ActorAnimationRequest *RemoteScene::Impl::CritterDefinition::animation(int mode) const {
    const auto &request = animations[mode == Critter::Skill ? 2 : mode == Critter::Walk ? 1 : 0];
    return request ? &*request : nullptr;
}

int RemoteScene::Impl::CritterDefinition::travelAnimationRate(int percentage) const {
    return std::max(1,walkRate*std::max(25,percentage)/100);
}

void RemoteScene::Impl::prepareCritters(const AnimDataTable &timing) {
    if (archives.pd2Presets) return;
    // D2Client 1802B dispatches by compiled MonStats class. Bind the shipped
    // Levels programs to the identities in this mount.
    using Think = CritterDefinition::Think;
    const std::map<std::string_view, Think, std::less<>> programs{
        {"chicken",Think::Chicken}, {"bug",Think::Chicken}, {"bunny",Think::Bunny},
        {"rat",Think::Scavenger}, {"scorpion",Think::Scavenger}, {"larva",Think::Scavenger},
        {"minispider",Think::Scavenger}, {"bat",Think::Bat},
        {"snake",Think::Snake}, {"parrot",Think::Bird}
    };
    for (const auto &[identity,row] : monsterRows) {
        const auto id=monstats.value(row,"Id");
        critterClasses.emplace(id,identity);
        const auto program=programs.find(id);
        if (program==programs.end() || monstats.number(row,"zoo").value_or(0)!=0) continue;
        const auto extra=monsterExtra.find(monstats.value(row,"MonStatsEx"));
        if (extra==monsterExtra.end()) continue;
        const auto e=extra->second;
        CritterDefinition definition;
        definition.think=program->second;
        definition.minGroup=monstats.number(row,"MinGrp").value_or(0);
        definition.maxGroup=monstats.number(row,"MaxGrp").value_or(0);
        if (definition.minGroup<0 || definition.maxGroup<0 ||
            definition.minGroup>UINT8_MAX || definition.maxGroup>UINT8_MAX) continue;
        definition.size=monstats2.number(e,"SizeX").value_or(0);
        if (definition.size<0 || definition.size>3) continue;
        definition.walkMode=monstats2.number(e,"mWL").value_or(0)!=0;
        definition.skillMoves=monstats2.number(e,"S1mv").value_or(0)!=0;
        // D2Common 30C70 uses the pre-expansion mode table for S1.
        definition.skillUsesWalkRate=identity<410 && !monstats.number(row,"npc").value_or(0);
        definition.flight=monstats.number(row,"flying").value_or(0)!=0;
        definition.ignoredByFlock=id=="chicken" || id=="bunny";
        definition.velocity=monstats.number(row,"Velocity").value_or(0);
        if (const auto rate=monsterWalkRates.find(identity);rate!=monsterWalkRates.end())
            definition.walkRate=rate->second;
        definition.walkMask=monsterMovementCollision(false,definition.flight,
            monstats.number(row,"opendoors").value_or(0)!=0,definition.size).mask;
        ActorAnimationRequest request;
        request.category="monsters";
        request.appearance.token=lower(std::string(monstats.value(row,"Code")));
        request.appearance.components.fill("nil");
        request.shadow=monstats2.number(e,"Shadow").value_or(0)!=0;
        request.nativeMonsterTransform=true;
        const int transform=monstats.number(row,"TransLvl").value_or(0);
        request.paletteTransform=transform>=0 && transform<8 ? transform+2 : 2;
        for (size_t part=0;part<componentCodes.size();++part) {
            if (!monstats2.number(e,componentCodes[part]).value_or(0)) continue;
            const auto values=variants(monstats2.value(e,std::string(componentCodes[part])+"v"));
            if (!values.empty()) request.appearance.components[part]=values.front();
        }
        constexpr std::array modes{"nu","wl","s1"};
        constexpr std::array fields{"mNU","mWL","mS1"};
        for (size_t mode=0;mode<modes.size();++mode) {
            if (!monstats2.number(e,fields[mode]).value_or(0)) continue;
            request.mode=modes[mode];
            request.appearance.weapon=monsterModeWeapon(archives,request.appearance.token,request.mode,monstats2.value(e,"BaseW"));
            if (!request.appearance.weapon.empty()) definition.animations[mode]=request;
        }
        const auto motionTiming=[&](Critter::Mode mode) -> std::optional<MonsterMotionTiming> {
            if (const auto *art=definition.animation(mode))
                return loadMonsterMotionTiming(timing,art->appearance.token,art->mode,art->appearance.weapon);
            return {};
        };
        if (const auto animation=motionTiming(Critter::Skill)) {
            definition.skillFrames=animation->frames;
            definition.skillRate=animation->speed;
        }
        if (const auto animation=motionTiming(Critter::Neutral))
            definition.startFrameBound=animation->frames*256;
        critterDefinitions.emplace(identity,std::move(definition));
    }
}

void RemoteScene::Impl::advanceCritters(const Map &map, Vec origin, uint64_t ticks) {
    if (!map.terrain.preparedRooms || !map.roomActivation || critterClasses.empty()) return;
    using Think = CritterDefinition::Think;
    const auto catalog=sharedWorldCatalog(archives,worldDifficulty);
    const auto &lifetimes=*map.roomActivation;
    const auto &active=lifetimes.activeSerials;
    std::erase_if(critterRooms,[&](uint64_t serial) {return !active.contains(serial);});
    std::erase_if(critters,[&](const Critter &critter) {return !active.contains(critter.room);});
    const auto &rooms=map.terrain.rooms;
    const auto &bounds=map.activation.rooms();
    if (bounds.size()!=rooms.size() || lifetimes.rooms.size()!=rooms.size()) return;
    const auto activation=[&](const MapTerrain::PreparedRoom &room) -> const RoomActivationSnapshot::Room & {
        return lifetimes.rooms[size_t(&room-rooms.data())];
    };
    const auto roomAt=[&](Vec global) -> const MapTerrain::PreparedRoom * {
        if (const auto *room=map.activation.room(global-origin)) return &rooms[size_t(room-bounds.data())];
        return nullptr;
    };
    Grid collision;
    collision.width=map.grid.width;
    collision.height=map.grid.height;
    collision.collisionSample=[&](int x,int y,EntityId) {
        const auto mask=map.grid.nativeMovementMask(x,y);
        const auto units=collisionCells.find({x+int(origin.x),y+int(origin.y)});
        return units==collisionCells.end() ? mask : uint16_t((mask&~units->second.cleared)|units->second.set);
    };

    // Retail 440C0 visits the active Room1 prepend chain. One population pass per
    // active lifetime, including empty draws and persistent adjoining rooms.
    std::vector<const MapTerrain::PreparedRoom *> births;
    for (const auto &room : rooms)
        if (!critterRooms.contains(activation(room).serial)) births.push_back(&room);
    std::ranges::sort(births,std::greater{},[&](const auto *room) {return activation(*room).serial;});
    for (const auto *entry : births) {
        const auto &room=*entry;
        const auto serial=activation(room).serial;
        critterRooms.insert(serial);
        if (!activation(room).initialRandom) continue;
        auto random=*activation(room).initialRandom;
        const auto &population=catalog->level(room.level).population;
        for (size_t slot=0;slot<population.critters.size();++slot) {
            const auto &name=population.critters[slot];
            if (name.empty()) break;
            const auto cls=critterClasses.find(name);
            if (cls==critterClasses.end()) break;
            if (rollRandom(random)%100>=uint32_t(population.critterChance[slot])) continue;
            const auto definition=critterDefinitions.find(cls->second);
            if (definition==critterDefinitions.end()) {
                effectLimitations.insert("Original client critter program or definition unavailable: "+name);
                continue;
            }
            const auto &data=definition->second;
            const int range=data.maxGroup-data.minGroup;
            int count=data.minGroup+int(limitedRandom(random,uint32_t(range)));
            const int amount=population.critterAmount[slot];
            if (amount<0 || amount>INT16_MAX) {
                effectLimitations.insert("Invalid original client critter amount: "+name);
                continue;
            }
            if (amount) count*=amount;
            for (int member=0;member<count;++member) {
                for (int attempt=0;attempt<10;++attempt) {
                    const int x=room.x*5+int(limitedRandom(random,uint32_t(room.width*5-1)));
                    const int y=room.y*5+int(limitedRandom(random,uint32_t(room.height*5-1)));
                    if (!collision.movementClear(x,y,{0x3f11,data.size})) continue;
                    auto seed=childRandom(random);
                    // 4D384 chooses the initial neutral phase before 4D512
                    // draws facing. Flying classes retain the supplied zero.
                    const int start=int(limitedRandom(seed,uint32_t(data.startFrameBound)));
                    const auto facing=uint8_t(data.walkMode && !data.flight ? rollRandom(seed)&63 : 0);
                    Critter critter;
                    critter.identity=cls->second;
                    critter.id=++critterSerial;
                    critter.room=serial;
                    critter.random=seed;
                    critter.modeFrame=critterFrame;
                    critter.startFrame=start;
                    critter.position=origin+Vec{float(x)+.5f,float(y)+.5f};
                    critter.facing=NativePathFacing(facing);
                    critters.push_back(std::move(critter));
                    break;
                }
            }
        }
    }
    if (!ticks || critters.empty()) {
        critterFrame+=ticks;
        return;
    }

    struct Neighbour {
        Vec position;
        int size{}, type{}, identity{};
        uint64_t assignment{};
        bool dead{};
    };
    std::map<uint64_t,std::vector<Neighbour>> observed;
    std::vector<Neighbour> players;
    std::optional<Neighbour> local;
    for (const auto &[key,unit] : world().units) {
        if (key.type>1 || !unit.position) continue;
        const auto current=localUnitPosition(key,false).value_or(Vec{float(unit.position->x),float(unit.position->y)});
        const auto identity=unitClass(unit).value_or(UINT16_MAX);
        const bool dead=key.type==0 ? playerDead(unit) : onlineMonsterCorpse(unit);
        // 16D80's eight live slots exclude native modes 0 and 12.
        const bool canApproach=key.type==0 && !world().corpseOwners.contains(key.id) &&
            (unit.nativeMode ? unit.mode!=0 && unit.mode!=12 : !dead);
        Neighbour sample{subtileCenter(current+Vec{.5f,.5f}),unitSize(unit),key.type,identity,
            unit.assignmentRevision,dead};
        if (const auto *room=roomAt(sample.position)) observed[activation(*room).serial].push_back(sample);
        if (key.type!=0) continue;
        if (key.id==playerId) local=sample;
        if (canApproach) players.push_back(sample);
    }
    // Replica assignments retain insertion order even though native room
    // unit-link history and all eight player slots are not exposed.
    for (auto &[room,units] : observed) {
        (void)room;
        std::ranges::stable_sort(units,std::greater{},&Neighbour::assignment);
    }
    std::ranges::stable_sort(players,std::ranges::less{},&Neighbour::assignment);
    const auto excluded=[&](int identity) {
        const auto at=critterDefinitions.find(identity);
        return at!=critterDefinitions.end() && at->second.ignoredByFlock;
    };
    const auto changeMode=[&](Critter &critter,Critter::Mode mode) {
        if (critter.mode==mode) return;
        critter.mode=mode;
        critter.modeFrame=critterFrame;
        critter.startFrame=0;
    };
    const auto stand=[&](Critter &critter,int wait) {
        critter.wait=wait;
        critter.route.clear();
        critter.position=NativePathStep::stop(critter.path, critter.position);
        changeMode(critter,Critter::Neutral);
    };
    const auto move=[&](Critter &critter,Vec goal,MovementCollisionRule rule,Critter::Mode mode,int speed,int maxDistance=14) {
        const auto &data=critterDefinitions.at(critter.identity);
        if (speed>0) critter.velocityPercent=speed;
        critter.movement=rule;
        const bool moves=mode==Critter::Walk ? data.walkMode : data.skillMoves && data.skillFrames>0;
        if (moves && data.velocity>0 && roomAt(goal))
            critter.route=collision.nativeUnitPath(critter.position-origin,goal-origin,2,uint8_t(maxDistance),rule);
        for (auto &point : critter.route) point=point+origin;
        const auto aim=critter.route.empty() ? goal : critter.route.front();
        if (const auto facing=NativePathStep::directionTo(critter.path, critter.position,aim)) critter.facing.aim(*facing);
        if (mode==Critter::Skill && data.skillFrames>0) {
            const int rate=data.skillUsesWalkRate && data.walkRate>0
                ? data.travelAnimationRate(critter.velocityPercent) : data.skillRate;
            critter.skillDuration=monsterAnimationTicks({data.skillFrames,rate});
            critter.skillSoundFrame=critterFrame;
            changeMode(critter,mode);
        } else if (mode==Critter::Walk && !critter.route.empty()) changeMode(critter,Critter::Walk);
        else critter.route.clear();
    };
    const auto wander=[&](Critter &critter,int radius,MovementCollisionRule rule,int speed,Critter::Mode mode=Critter::Walk) {
        const float dx=(rollRandom(critter.random)&1) ? -float(radius) : float(radius);
        const float dy=(rollRandom(critter.random)&1) ? -float(radius) : float(radius);
        move(critter,subtileCenter(critter.position)+Vec{dx,dy},rule,mode,speed);
    };
    const auto flee=[&](Critter &critter,Vec threat,int radius,MovementCollisionRule rule,int speed,Critter::Mode mode=Critter::Walk) {
        const auto next=monsterRetreatPoint(critter.position,threat,1)-origin;
        if (next.x<0 || next.y<0 || next.x>=float(collision.width) || next.y>=float(collision.height) ||
            !collision.movementClear(int(next.x),int(next.y),{rule.mask,1})) return false;
        move(critter,monsterRetreatPoint(critter.position,threat,radius),rule,mode,speed);
        return true;
    };
    const auto nearUnits=[&](const MapTerrain::PreparedRoom &room,const auto &visit) {
        for (const auto index : room.near) {
            const auto serial=lifetimes.rooms[index].serial;
            if (const auto found=observed.find(serial);found!=observed.end())
                for (const auto &unit : found->second) visit(unit);
        }
    };

    for (uint64_t tick=0;tick<ticks;++tick) {
        ++critterFrame;
        for (auto &critter : critters) {
            if (critter.gone || !roomAt(critter.position)) continue;
            const auto &data=critterDefinitions.at(critter.identity);
            const MovementCollisionRule walkRule{data.walkMask,data.size==1 ? 2 : data.size};
            if (!critter.route.empty()) {
                const Vec next=critter.route.front();
                if (const auto facing=NativePathStep::directionTo(critter.path, critter.position,next)) critter.facing.aim(*facing);
                const auto step=NativePathStep::advance(critter.path, collision,critter.position,next,
                    monsterMovementSpeed(data.velocity,critter.velocityPercent)*NativePathStep::seconds,critter.movement,origin);
                critter.position=step.position;
                if (const auto *room=roomAt(critter.position)) critter.room=activation(*room).serial;
                if (step.reached) critter.route.pop_front();
                if (step.blocked) critter.route.clear();
            }
            critter.facing.advance();
            if ((critter.mode==Critter::Walk && critter.route.empty()) ||
                (critter.mode==Critter::Skill && critterFrame-critter.modeFrame>=uint64_t(critter.skillDuration)))
                stand(critter,critter.wait);
            if (critter.wait>0) {--critter.wait;continue;}
            const Neighbour *player=nullptr;
            int distance=std::numeric_limits<int>::max();
            for (const auto &candidate : players) {
                const int gap=critterDistance(critter.position,data.size,candidate.position,candidate.size);
                if (gap<distance) {distance=gap;player=&candidate;}
            }
            if (!player && local) player=&*local; // Retail leaves distance at INT_MAX.
            if (!player) {stand(critter,25);continue;}
            if (distance>30) {stand(critter,std::min(distance-15,200));continue;}
            if (critter.mode!=Critter::Neutral) continue;
            const auto *room=roomAt(critter.position);
            if (!room) continue;
            const Vec cell=subtileCenter(critter.position);
            const bool sharesPlayerCell=cell.x==player->position.x && cell.y==player->position.y;
            switch (data.think) {
            case Think::Chicken:
            case Think::Bunny: {
                if (data.think==Think::Bunny &&
                    (collision.movementMask(int(critter.position.x-origin.x),int(critter.position.y-origin.y))&0x1c0)) {
                    stand(critter,0);
                    break;
                }
                const int radius=data.think==Think::Bunny ? 6 : 4;
                std::optional<Vec> closest;
                int closestDistance=std::numeric_limits<int>::max();
                const auto consider=[&](Vec position,int size) {
                    const int gap=critterDistance(critter.position,data.size,position,size);
                    if (gap<closestDistance) {closestDistance=gap;closest=position;}
                };
                nearUnits(*room,[&](const Neighbour &unit) {
                    if (!unit.dead && (unit.type==0 || !excluded(unit.identity))) consider(unit.position,unit.size);
                });
                for (const auto &other : critters) {
                    if (other.id==critter.id || other.gone || excluded(other.identity)) continue;
                    if (std::none_of(room->near.begin(),room->near.end(),[&](size_t index) {
                        return lifetimes.rooms[index].serial==other.room;
                    })) continue;
                    consider(other.position,critterDefinitions.at(other.identity).size);
                }
                if (!closest) break;
                const MovementCollisionRule relaxed{0x0c01,walkRule.size};
                if (closestDistance<radius) {
                    // Retail 17E80 uses action 12 (S1); 17800 uses WL
                    // for the bunny. +0x10 in the action is velocity percent.
                    const auto mode=data.think==Think::Chicken ? Critter::Skill : Critter::Walk;
                    if (!flee(critter,*closest,radius,relaxed,120,mode))
                        wander(critter,radius,relaxed,120,mode);
                } else if (rollRandom(critter.random)%100<30) wander(critter,2,{0x3c01,walkRule.size},0);
                else stand(critter,5);
                break;
            }
            case Think::Scavenger: {
                if (distance<2 && sharesPlayerCell) {
                    stand(critter,0);
                } else if (distance<4) flee(critter,player->position,6,walkRule,0);
                else {
                    std::optional<Vec> corpse;
                    if (distance>7 && local) nearUnits(*room,[&](const Neighbour &unit) {
                        if (!unit.dead) return;
                        const auto gap=critterDistance(critter.position,data.size,unit.position,unit.size);
                        if (gap>=3 && gap<=25 && critterDistance(local->position,local->size,unit.position,unit.size)>7 &&
                            limitedRandom(critter.random,2)) corpse=unit.position;
                    });
                    if (critter.marked) {
                        if (limitedRandom(critter.random,100)<20) critter.marked=false;
                        stand(critter,32);
                    } else if (corpse) {
                        move(critter,*corpse,walkRule,Critter::Walk,6);
                        critter.marked=true;
                    } else if (limitedRandom(critter.random,100)>=75) wander(critter,5,walkRule,0);
                    else stand(critter,30);
                }
                break;
            }
            case Think::Bat:
                if (distance>=5) wander(critter,9,walkRule,0);
                else if (rollRandom(critter.random)%100<66) flee(critter,player->position,8,walkRule,30);
                else wander(critter,12,walkRule,30);
                break;
            case Think::Snake:
                if (sharesPlayerCell) stand(critter,0);
                else if (distance<2) flee(critter,player->position,4,walkRule,0);
                else if (limitedRandom(critter.random,100)<75) stand(critter,32);
                else wander(critter,4,walkRule,0);
                break;
            case Think::Bird: {
                if (critter.marked && local && critterDistance(critter.position,data.size,local->position,local->size)>25) {
                    critter.gone=true;
                    break;
                }
                if (!local || !data.flight) break;
                critter.marked=true;
                const bool alongX=(rollRandom(critter.random)&1)!=0;
                const float offset=float(limitedRandom(critter.random,40));
                const float dx=(rollRandom(critter.random)&1 ? -1.f : 1.f)*(alongX ? 40.f : offset);
                const float dy=(rollRandom(critter.random)&1 ? -1.f : 1.f)*(alongX ? offset : 40.f);
                const auto position=subtileCenter(critter.position);
                const auto goal=(local->position+Vec{dx,dy})*3.f-position*2.f;
                const auto endpoint=flightEndpoint(position-origin,goal-origin,collision);
                const auto destination=collision.nativeMovementTarget(position-origin,endpoint,walkRule)+origin;
                move(critter,destination,walkRule,Critter::Walk,0,77);
                break;
            }
            }
        }
        std::erase_if(critters,[](const Critter &critter) {return critter.gone;});
    }
}

void RemoteScene::Impl::drawCritters(WorldDrawView &scene,SceneView &shared,std::vector<SoundActorView> &soundActors,bool paused) {
    const auto &map=*scene.map;
    for (auto &critter : critters) {
        const Vec feet=critter.position-scene.terrainOrigin;
        if (!map.activation.room(feet)) continue;
        const auto &definition=critterDefinitions.at(critter.identity);
        const auto *prepared=definition.animation(critter.mode);
        if (!prepared) {
            effectLimitations.insert("Original client critter mode unavailable for class "+std::to_string(critter.identity));
            continue;
        }
        auto request=*prepared;
        if ((critter.mode==Critter::Walk || (critter.mode==Critter::Skill && definition.skillUsesWalkRate)) &&
            definition.walkRate>0)
            request.animationRate=definition.travelAnimationRate(critter.velocityPercent);
        const auto *visual=shared.actorAnimation(request,artPalette);
        if (!visual || !visual->ready()) {
            effectLimitations.insert("Original client critter art unavailable: "+request.appearance.token+" "+request.mode);
            continue;
        }
        const auto position=shared.screen(feet)+unitScreenOffset+visual->offset;
        if (visual->animation.intersects(position,{0,0,float(W),float(H)})) {
            const double phase=double(critterFrame-critter.modeFrame)*visual->fps/25.0+double(critter.startFrame)/256.0;
            const float frame=float(visual->cycle ? std::fmod(phase,visual->frameCount) : phase);
            if (const auto *image=visual->sample(0,0,nativeFacingVector(critter.facing.direction()),frame);
                image && visible(*image,position))
                scene.items.push_back({.image=image,.position=feet,.pixelOffset=unitScreenOffset+visual->offset,
                    .shadow=visual->shadow,.orderFlag=visual->order});
        }
        // Sound ownership is local to the presentation mixer and is never a
        // unit key in the replica, selection, missile, or command paths.
        const EntityId soundSource{(uint64_t{1}<<62)|critter.id};
        soundActors.push_back({.id=soundSource,.identity=critter.identity,.actionRevision=critter.modeFrame,
            .position=critter.position,.neutral=critter.mode==Critter::Neutral,
            .frozen=paused,
            .audible=shared.worldContains(shared.screen(feet))});
        if (const auto event=std::exchange(critter.skillSoundFrame,std::nullopt);event && !paused)
            soundEvents.push_back({.kind=PresentationSoundEvent::Kind::Skill1,.source=soundSource,
                .actionRevision=*event,.age=float(critterFrame-*event)/25.f});
    }
}
} // namespace d2x
