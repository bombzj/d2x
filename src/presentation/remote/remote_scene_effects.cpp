#include "remote_scene_impl.hpp"

namespace d2x {

EntityId RemoteScene::Impl::effectOwner(OnlineUnitKey key) {
    return {(uint64_t(key.type) << 32) + key.id + 1};
}

void RemoteScene::Impl::emitSound(PresentationSoundEvent::Kind kind, const OnlineUnit &source, float age,
               int skill, float releaseTime, std::string sound, int mode) {
    soundEvents.push_back({kind,effectOwner(source.key),source.actionRevision,skill,age,releaseTime,std::move(sound),mode});
}

void RemoteScene::Impl::monsterSoundEvent(const OnlineCombatEvent &event, const OnlineUnit &source, float age) {
    if (!event.action) return;
    using Kind = PresentationSoundEvent::Kind;
    switch (*event.action) {
    case 10: case 11: case 16: case 17:
        // The alternate-target flag alone is not a new native attack.
        if (event.skill || source.wireAction == event.action)
            emitSound(*event.action == 16 || *event.action == 17 ? Kind::Attack2 : Kind::Attack1,source,age);
        break;
    case 6: emitSound(Kind::Hit,source,age); break;
    case 8: emitSound(Kind::Death,source,age); break;
    case 12: case 13: emitSound(Kind::Skill1,source,age); break;
    case 14: case 15: emitSound(Kind::Skill2,source,age); break;
    case 26: case 27: emitSound(Kind::Skill3,source,age); break;
    case 28: case 29: emitSound(Kind::Skill4,source,age); break;
    default: break;
    }
}

void RemoteScene::Impl::emitDruidStates(const OnlineView &view,SceneView &shared,const RemoteCombat &combat,bool town) {
    const auto frame=uint64_t(double(time)*25.0);std::set<std::pair<OnlineUnitKey,int>> active;
    if(!town) for(const auto &[key,snapshot]:combat.states()) {
        const auto actor=view.world.units.find(key);
        if(!snapshot.decoded || actor==view.world.units.end() || !actor->second.position ||
           (key.type==1?onlineMonsterCorpse(actor->second):playerDead(actor->second))) continue;
        const auto at=*actor->second.position;const Vec center{float(at.x)+.5f,float(at.y)+.5f};
        for(const auto &[state,record]:snapshot.states) {
            const auto nativeState=stateRows.find(state);
            if(nativeState!=stateRows.end() && states.number(nativeState->second,"cltactivefunc")==89 && vineVisualSkill>=0) {
                const auto identity=std::pair{key,state};active.insert(identity);
                auto &clock=druidParticles.try_emplace(identity,StateParticles{UINT64_MAX,initialRandom(key.id),0,center}).first->second;
                if(clock.frame==frame) continue;
                clock.elapsed+=clock.frame==UINT64_MAX?1:frame-clock.frame;clock.frame=frame;
                const auto row=skillRows.at(vineVisualSkill);const int period=skillCalculation(vineVisualSkill,1,"cltcalc2",key).value_or(0);
                if(period>0 && (clock.elapsed>=uint64_t(period) || unitCoordinateDistance(int(center.x),int(center.y),int(clock.position.x),int(clock.position.y))>=period)) {
                    std::string field;if(actor->second.mode==1) field="cltmissiled";
                    else if(actor->second.mode==2) field="cltmissilec";
                    if(!field.empty()) {
                        const auto child=missileNames.find(skills.value(row,field));
                        if(child!=missileNames.end()) {
                            const int id=missiles.number(child->second,"Id").value_or(-1)+(actor->second.mode==2?int(limitedRandom(clock.random,2)):0);
                            shared.launchClientMissile(id,center,center,1,0,{},-1,effectOwner(key),combat.hostileSource(actor->second));
                            clock.elapsed=0;clock.position=center;
                        }
                    }
                }
                continue;
            }
            const auto skill=druidStateSkills.find(state);if(skill==druidStateSkills.end()) continue;
            const auto identity=std::pair{key,state};active.insert(identity);
            auto &clock=druidParticles.try_emplace(identity,StateParticles{UINT64_MAX,initialRandom(key.id)}).first->second;
            if(clock.frame==frame) continue;clock.frame=frame;
            const auto row=skillRows.at(skill->second);int rank=1;
            const auto rankStat=monsterText->nativeStats->names.find("modifierlist_level");
            if(rankStat!=monsterText->nativeStats->names.end())
                for(const auto &stat:record.stats) if(stat.id==rankStat->second && !stat.parameter) rank=int(stat.value);
            const int count=skillCalculation(skill->second,rank,"cltcalc1",key).value_or(0);
            const int height=skillCalculation(skill->second,rank,"cltcalc2",key).value_or(0);
            if(count<=0 || height<=0) continue;
            std::array<int,3> children{-1,-1,-1};
            for(int index=0;index<3;++index) if(const auto child=missileNames.find(skills.value(row,"cltmissile"+std::string(1,char('a'+index))));child!=missileNames.end())
                children[size_t(index)]=missiles.number(child->second,"Id").value_or(-1);
            constexpr std::array<Vec,8> offsets{Vec{7,0},Vec{5,5},Vec{0,7},Vec{-5,5},Vec{-7,0},Vec{-5,-5},Vec{0,-7},Vec{5,-5}};
            for(int index=0;index<count;++index) for(int particle=0;particle<3;++particle) {
                const auto offset=offsets[size_t(rollRandom(clock.random)&7)];int child=children[0];
                if(children[1]>0 && children[2]>0) {const auto pick=rollRandom(clock.random)%3;child=children[pick==0?1:pick==1?2:0];}
                else if(children[1]>0 && !(rollRandom(clock.random)&1)) child=children[1];
                ClientMissileInitialization init;init.parameters=std::array<int32_t,2>{int32_t(at.x),int32_t(at.y)};
                init.pixelHeight=int(limitedRandom(clock.random,uint32_t(height)));
                const auto start=center+offset;
                if(!shared.launchClientMissile(child,start,start+Vec{offset.y,-offset.x},rank,0,{},-1,effectOwner(key),combat.hostileSource(actor->second),0,ClientMissileSource::Program,0,{},0,init))
                    effectLimitations.insert("Druid state missile unavailable: "+std::to_string(child));
            }
        }
    }
    std::erase_if(druidParticles,[&](const auto &entry){return !active.contains(entry.first);});
}

void RemoteScene::Impl::emitWoundBlood(const OnlineView &view, SceneView &shared, const RemoteCombat &combat,
                   const Grid &grid, Vec origin) {
    // Retail state callback 85 (RVA C9000) owns one shared client timer.
    // Strict frame > next, then next = frame + 5; never replay missed ticks.
    const auto frame = uint64_t(double(time) * 25.0);
    std::set<OnlineUnitKey> active;
    for (const auto &[key, snapshot] : combat.states()) {
        const auto found = view.world.units.find(key);
        if (!snapshot.decoded || found == view.world.units.end() || key.type != 1) continue;
        const auto &unit = found->second;
        if (!unit.position || !unitClass(unit) || onlineMonsterCorpse(unit)) continue;
        const bool bleeding = std::any_of(snapshot.states.begin(), snapshot.states.end(), [&](const auto &entry) {
            const auto row = stateRows.find(entry.first);
            return row != stateRows.end() && states.number(row->second,"cltactivefunc") == 85;
        });
        if (!bleeding) continue;
        active.insert(key);
        if (frame <= nextWoundBloodFrame) continue;
        nextWoundBloodFrame = frame + 5;
        // Client seeds are not transmitted. Keep this cosmetic stream
        // separate from combat, stable through a refresh of the same state.
        auto &random = woundBloodRandom.try_emplace(key, initialRandom(key.id)).first->second;
        if (unit.mode == 3 && limitedRandom(random,3) != 0) continue;
        const auto monster = monsterRows.find(*unitClass(unit));
        if (monster == monsterRows.end()) continue;
        const auto extra = monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));
        if (extra == monsterExtra.end()) continue;
        // The native MonStats2 byte 0x11D is Bleed, not localBlood (0x11C).
        const int blood = monstats2.number(extra->second,"Bleed").value_or(0);
        if (!blood) continue;
        if (blood < 0 || blood > int(bloodMissiles.size() / 2)) {
            effectLimitations.insert("Open Wounds blood type " + std::to_string(blood)); continue;
        }
        int64_t observedCount = 0;
        for (const auto &[id, state] : snapshot.states) {
            (void)id;
            for (const auto &stat : state.stats)
                if (stat.id == damageBloodStat && !stat.parameter)
                    observedCount += std::clamp<int64_t>(stat.value,-2048,2048);
        }
        // Native helper defaults to one; only published damage_blood can
        // override it. Do not infer hidden monster stat lists.
        const int count = int(std::clamp<int64_t>(observedCount ? observedCount : 1,0,2048));
        constexpr std::array<Vec, 8> offsets{{{0,2},{-1,1},{-2,0},{-1,-1},{0,-2},{1,-1},{2,0},{1,1}}};
        const Vec center{float(unit.position->x) + .5f, float(unit.position->y) + .5f};
        for (int particle = 0; particle < count; ++particle) {
            const bool moving = (unit.mode == 2 || unit.mode == 15) && unit.direction.has_value();
            const int first = moving ? (int(*unit.direction / 8) + int(limitedRandom(random,7)) - 3) & 7
                                     : int(limitedRandom(random,8));
            // RVA 4C500 checks seven candidate points against WALL only.
            for (int candidate = 0; candidate < 7; ++candidate) {
                const auto target = center + offsets[size_t((first + candidate) & 7)];
                if (!grid.walkable(target - origin, {1,1})) continue;
                const int missile = bloodMissiles[limitedRandom(random,uint32_t(blood * 2))];
                // Native B0450 creates at the unit, aimed at the free point.
                if (missile < 0 || !shared.launchClientMissile(missile,center,target,1,0,{},-1,effectOwner(key)))
                    effectLimitations.insert("Open Wounds blood missile " + std::to_string(missile));
                break;
            }
        }
    }
    std::erase_if(woundBloodRandom,[&](const auto &entry) { return !active.contains(entry.first); });
}

void RemoteScene::Impl::observeEffects(const OnlineView &v, SceneView &shared, const RemoteCombat &combat, bool town,const Grid &collision,Vec worldOrigin) {
    const auto observed=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    std::erase_if(missileCastRevisions, [&](const auto &cast) {
        const auto source = v.world.units.find(cast.first);
        bool interrupted = source == v.world.units.end();
        if (!interrupted && source->second.actionRevision != cast.second) {
            const auto &unit = source->second;
            const bool ownerConfirmation = playerId && cast.first == OnlineUnitKey{0, *playerId} &&
                localCast && localCast->started >= 0 && time < localCast->started + localCast->duration &&
                unit.actionSkill == localCast->command.skill;
            interrupted = !ownerConfirmation && (walking(unit) || unit.actionSkill.has_value() ||
                (unit.key.type == 1 ? (onlineMonsterCorpse(unit) || unit.mode == 3)
                    : unit.nativeMode ? (unit.mode == 0 || unit.mode == 4 || unit.mode == 17 || unit.mode == 18)
                                      : (unit.mode == 6 || unit.mode == 8 || unit.mode == 9 || unit.mode == 18)));
        }
        if (interrupted) shared.cancelPendingClientMissiles(effectOwner(cast.first));
        return interrupted;
    });
    auto overlay = [&](int id, OnlineUnitKey unit) {
        if (const auto *visual = shared.overlayVisual(id))
            overlayVisuals.push_back({id, unit, time, visual->frames / visual->fps});
    };
    auto launch = [&](size_t row, Vec start, Vec target, int level, float delay, std::optional<float> remaining,
                      OnlineUnitKey owner, int pathIndex = -1, int pierce = 0,
                      ClientMissileSource origin = ClientMissileSource::Program,EntityId guidedTarget = {},int velocityBonus = 0,ClientMissileInitialization initialization = {}) {
        const auto id = missiles.number(row, "Id");
        const auto actor = v.world.units.find(owner);
        if(origin!=ClientMissileSource::Synchronization && missiles.number(row,"Pierce").value_or(0) && owner.type==0 && owner.id==v.load.playerUnitId)
            if(const auto chance=shared.characterView().missilePierceChance) pierce=missilePierceCount(*chance,0);
        int slow=0;
        if(const auto snapshot=combat.states().find(owner);snapshot!=combat.states().end() && snapshot->second.decoded)
            for(const auto &[state,definition]:snapshot->second.states) { (void)state;for(const auto &stat:definition.stats) if(stat.id==slowMissileStat) slow=int(stat.value); }
        const int function=missiles.number(row,"pCltDoFunc").value_or(0);
        if(id && (function==48 || function==50)) if(const auto skill=missileSkills.find(*id);skill!=missileSkills.end()) {
            if(!initialization.streamRange) initialization.streamRange=skillCalculation(skill->second,level,function==48?"calc1":"aurarangecalc",owner).value_or(0);
            if(!initialization.streamPeriod) initialization.streamPeriod=skillCalculation(skill->second,level,function==48?"calc2":"calc4",owner).value_or(0);
        }
        const bool created=id && shared.launchClientMissile(*id, start, target, level, delay, remaining, pathIndex,
            effectOwner(owner), actor != v.world.units.end() && combat.hostileSource(actor->second), pierce, origin,slow,guidedTarget,velocityBonus,initialization);
        if(id && !created)
            effectLimitations.insert("Missile client program unavailable: " + std::string(missiles.value(row, "Missile")));
        return created;
    };
    auto monsterAttack = [&](const OnlineCombatEvent &event, OnlineUnit actor, std::string_view pose, float age) {
        if (!unitClass(actor) || !actor.position) return;
        const auto identity=monsterRows.find(*unitClass(actor));
        if (identity==monsterRows.end()) return;
        const auto missile=missileNames.find(monstats.value(identity->second,"Miss"+std::string(pose)));
        if (missile==missileNames.end() || missiles.number(missile->second,"ClientSend").value_or(0)) return;
        auto target=event.point;
        if (event.target) {
            const auto unit=v.world.units.find(*event.target);
            if (unit!=v.world.units.end()) target=unit->second.position;
        }
        if (!target) return;
        actor.actionSkill.reset(); actor.nativeMode=true;
        actor.mode=pose=="A1"?4:pose=="A2"?5:pose=="S1"?8:pose=="S2"?9:pose=="S3"?10:11;
        const auto *animation=monster(actor, false, shared);
        if (!animation || animation->releaseTime<0) return;
        const auto difficulty=v.load.difficulty.value_or(0);
        if (difficulty>=difficultyLevels.rows().size()) return;
        const int level=1+difficultyLevels.number(difficulty,"MonsterSkillBonus").value_or(0);
        shared.cancelPendingClientMissiles(effectOwner(actor.key));
        missileCastRevisions[actor.key]=v.world.units.at(actor.key).actionRevision;
        const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
        const Vec end{float(target->x)+.5f,float(target->y)+.5f};
        launch(missile->second,start,end,level,animation->releaseTime-age,{},actor.key);
        if (monstats.value(identity->second,"AI")=="QuillRat") {
            // MonsterMode's extra quills use a fresh SEIS seed and +/-5 offsets.
            auto seed=initialRandom(0x53454953);
            const auto suffix=difficulty==1?"(N)":difficulty==2?"(H)":"";
            const int count=monstats.number(identity->second,"aip3"+std::string(suffix)).value_or(0);
            for (const Vec aim : monsterQuillTargets(end,count,seed))
                launch(missile->second,start,aim,1,animation->releaseTime-age,{},actor.key);
        }
    };
    auto skillEffect = [&](const OnlineCombatEvent &event, std::optional<Vec> presentationOrigin = {}) {
        const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds
            ?float(now-event.receivedMilliseconds)/1000.f:0.f;
        if (age>.25f) return;
        const auto row = skillRows.find(*event.skill);
        const auto source = v.world.units.find(event.source);
        if (row == skillRows.end() || source == v.world.units.end() || !source->second.position) return;
        if(auto channel=channels.find(event.source);channel!=channels.end() && channel->second.skillRow==row->second) {
            auto target=event.point;
            if(!target && event.target) target=targetPosition(*event.target);
            if(target) {channel->second.destination=*target;channel->second.target=event.target;channel->second.revision=source->second.actionRevision;}
            return;
        }
        auto actor = source->second; actor.actionSkill = event.skill;actor.actionSkillLevel=event.level;
        const auto *castAnimation = actor.key.type == 0 ? character(actor, false, town, shared) : monster(actor, false, shared);
        if (!castAnimation || castAnimation->animation.frames.empty())
            effectLimitations.insert("Skill animation unavailable: " + std::string(skills.value(row->second, "skill")));
        if (actor.key.type == 1) {
            // MonsterMsg sends a skill notification instead of an action packet
            // whenever the monster has a used skill, including ordinary Attack.
            auto action = event;
            const auto monsterRow=monsterRows.find(unitClass(actor).value_or(UINT16_MAX));
            auto pose=monsterRow==monsterRows.end()?std::string{}:monsterSkillMode(monsterRow->second,*event.skill);
            if (pose.starts_with("seq_")) pose = shared.actorSequenceMode(pose);
            for (auto &ch:pose) ch=char(std::toupper(static_cast<unsigned char>(ch)));
            if (pose == "A1") action.action = 10;
            else if (pose == "A2") action.action = 16;
            else if (pose == "S1") action.action = 12;
            else if (pose == "S2") action.action = 14;
            else if (pose == "S3") action.action = 26;
            else if (pose == "S4") action.action = 28;
            else action.action.reset();
            monsterSoundEvent(action, source->second, age);
        }
        emitSound(PresentationSoundEvent::Kind::Cast,source->second,age,*event.skill,
            castAnimation ? castAnimation->releaseTime : -1.f);
        const auto castOverlay = overlayNames.find(skills.value(row->second, "castoverlay"));
        if (castOverlay != overlayNames.end()) overlay(int(castOverlay->second), event.source);
        if(skills.number(row->second,"cltstfunc")==44) {
            // Native CltSt44 constructs in this order, at skill start.
            constexpr std::array<std::string_view,6> names{"hellmeteordown","hellmeteorball",
                "hellmeteorup","hellmeteorball","hellmeteorlaunch2","hellmeteorlaunch1"};
            std::array<size_t,6> rows{};
            for(size_t index=0;index<names.size();++index) {
                const auto missile=missileNames.find(names[index]);
                if(missile==missileNames.end()) {
                    effectLimitations.insert("HellMeteor client missile unavailable: "+std::string(names[index]));
                    return;
                }
                rows[index]=missile->second;
            }
            const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
            auto point=event.point;if(!point && event.target) point=targetPosition(*event.target);
            const int level=event.level.value_or(1);
            // A1540 rejects missing/zero coordinates and targets over 100 subtiles.
            if(point && point->x && point->y) {
                const int dx=nativeSubtract(int(point->x),int(actor.position->x));
                const int dy=nativeSubtract(int(point->y),int(actor.position->y));
                if(nativeAdd(nativeMultiply(dx,dx),nativeMultiply(dy,dy))<=10000) {
                    const Vec at{float(point->x)+.5f,float(point->y)+.5f};
                    ClientMissileInitialization falling;ClientMissileGraphics graphics;
                    graphics.position[2]=0xAFC800;graphics.velocity[2]=-0xC800;falling.graphics=graphics;
                    launch(rows[0],at,at,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,falling);
                    launch(rows[1],at,at,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,falling);
                }
            }
            ClientMissileInitialization rising;ClientMissileGraphics graphics;
            graphics.velocity[2]=0x7800;rising.graphics=graphics;
            launch(rows[2],start,start,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,rising);
            launch(rows[3],start,start,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,rising);
            // Common #10441 is UNITS_SetAnimationSpeed in this binary;
            // 0x80 is an animation speed, rather than missile flags.
            ClientMissileInitialization launchAnimation;launchAnimation.animationRate=0x80;
            const bool secondCreated=launch(rows[4],start,start,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,launchAnimation);
            launch(rows[5],start,start,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,
                secondCreated?launchAnimation:ClientMissileInitialization{});
            return;
        }
        if(skills.number(row->second,"cltstfunc")==47) {
            const auto missile=missileNames.find(skills.value(row->second,"cltmissilea"));
            const auto chain=catapultChainOffsets.find(unitClass(actor).value_or(UINT16_MAX));
            auto point=event.point;if(!point && event.target) point=targetPosition(*event.target);
            if(missile==missileNames.end() || chain==catapultChainOffsets.end() || !point) return;
            const auto direction=catapultDirection(chain->second);if(!direction) return;
            const int range=std::max(1,int(int16_t(missiles.number(missile->second,"Range").value_or(0))));
            const int level=event.level.value_or(1);
            const auto height=skillCalculation(*event.skill,level,"cltcalc1",event.source);if(!height) return;
            const int x=nativeMultiply(int(direction->x)-int(direction->y),16);
            const int y=nativeMultiply(int(direction->x)+int(direction->y),8);
            ClientMissileInitialization init;ClientMissileGraphics graphics;
            graphics.position={nativeShiftLeft(nativeMultiply(-range,x),11),nativeShiftLeft(nativeMultiply(-range,y),11),nativeShiftLeft(nativeMultiply(*height,range),11)};
            graphics.velocity={nativeShiftLeft(x,11),nativeShiftLeft(y,11),nativeShiftLeft(nativeSubtract(0,*height),11)};
            graphics.step();init.graphics=graphics;
            init.impactCount=skillCalculation(*event.skill,level,"calc4",event.source);
            const Vec at{float(point->x)+.5f,float(point->y)+.5f};
            launch(missile->second,at,at,level,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
            return;
        }
        if(skills.number(row->second,"cltstfunc")==46) {
            const auto missile=missileNames.find(skills.value(row->second,"cltmissilea"));
            auto point=event.point;if(!point && event.target) point=targetPosition(*event.target);
            const auto sound=skills.value(row->second,"prgsound");
            if(!sound.empty()) emitSound(PresentationSoundEvent::Kind::Original,source->second,age,-1,-1,std::string(sound));
            // CltSt46 constructs the control at start, before A1's release.
            if(missile!=missileNames.end() && point) {
                const Vec at{float(point->x)+.5f,float(point->y)+.5f};
                launch(missile->second,at,at,event.level.value_or(1),-age,{},event.source);
            }
            return;
        }
        if(skills.value(row->second,"charclass")=="ass" && skills.number(row->second,"cltstfunc")==5 && event.target) {
            const auto targetOverlay=overlayNames.find(skills.value(row->second,"tgtoverlay"));
            if(targetOverlay!=overlayNames.end() && castAnimation && castAnimation->releaseTime>=0)
                if(const auto *visual=shared.overlayVisual(int(targetOverlay->second)))
                    overlayVisuals.push_back({int(targetOverlay->second),*event.target,time+castAnimation->releaseTime-age,visual->frames/visual->fps});
        }
        if(skills.number(row->second,"cltstfunc")==45) {
            bool transformed=false;
            if(const auto snapshot=combat.states().find(event.source);snapshot!=combat.states().end())
                for(const auto &[state,record]:snapshot->second.states) { (void)record;const auto native=stateRows.find(state);
                    if(native!=stateRows.end() && states.value(native->second,"state")==skills.value(row->second,"aurastate")) transformed=true;
                }
            const auto visual=overlayNames.find(skills.value(row->second,transformed?"cltoverlayb":"cltoverlaya"));
            if(visual!=overlayNames.end()) overlay(int(visual->second),event.source);
        }
        if(skills.number(row->second,"cltstfunc")==49) {
            const int rank=event.level.value_or(1);const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
            if(const auto facade=missileNames.find(skills.value(row->second,"cltmissilea"));facade!=missileNames.end())
                launch(facade->second,start,start,rank,-age,{},event.source);
            if(const auto vines=missileNames.find(skills.value(row->second,"cltmissileb"));vines!=missileNames.end()) {
                const int count=skillCalculation(*event.skill,rank,"cltcalc1",event.source).value_or(0);
                constexpr std::array<Vec,8> offsets{Vec{0,1},Vec{0,-1},Vec{1,0},Vec{-1,0},Vec{1,1},Vec{-1,-1},Vec{-1,1},Vec{1,-1}};
                auto aim=event.point;if(!aim && event.target) aim=targetPosition(*event.target);
                const Vec origin=aim?Vec{float(aim->x)+.5f,float(aim->y)+.5f}:start;
                const int seedX=int(origin.x);
                for(int index=0;index<count;++index) {
                    ClientMissileInitialization init;init.path=ClientMissileInitialization::Path::Charged;init.seed=uint32_t(seedX+index);
                    launch(vines->second,origin,origin+offsets[size_t(index)&7],rank,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
                }
            }
            return;
        }
        if(skills.number(row->second,"cltstfunc")==48) {
            const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
            if(const auto departure=missileNames.find(skills.value(row->second,"cltmissilea"));departure!=missileNames.end())
                launch(departure->second,start,start,event.level.value_or(1),-age,{},event.source);
        }
        const int function = skills.number(row->second, "cltdofunc").value_or(0);
        if (v.protocolProfile == "pd2-cn-normal-20261020") {
            // Fixed CN CltDo table: these callbacks replace native code, not
            // skill identities. Details and both DLL versions are in PD2_CN_DLL_ANALYSIS.
            if (function == 103) return; // Aura visuals arrive through unit states.
            if (function == 105) {
                // Core 3A6F30 reads Skills.SrvMissileA, not CltMissileA.
                const auto missile = missileNames.find(skills.value(row->second,"srvmissilea"));
                if (missile == missileNames.end() || !castAnimation || castAnimation->releaseTime < 0) return;
                const Vec at{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
                shared.cancelPendingClientMissiles(effectOwner(event.source));
                missileCastRevisions[event.source] = source->second.actionRevision;
                launch(missile->second,at,at,event.level.value_or(1),castAnimation->releaseTime-age,{},event.source);
                return;
            }
            if (function == 100 || function == 101 || function == 102 || function == 113 || function == 116 || function == 117) {
                if (!castAnimation || castAnimation->releaseTime < 0) return;
                auto target = event.point;
                if (event.target) {
                    if (const auto position = targetPosition(*event.target)) target = position;
                    else if (function == 102 || function == 116) return;
                }
                if ((function == 101 || function == 102 || function == 117) && !target) return;
                if ((function == 102 || function == 116) && !event.target) return;
                const int level = std::max(1, int(event.level.value_or(1)));
                const Vec origin = presentationOrigin.value_or(Vec{float(actor.position->x)+.5f,float(actor.position->y)+.5f});
                const Vec destination = target ? Vec{float(target->x)+.5f,float(target->y)+.5f} : origin;
                const auto missileFor = [&](std::string_view field) {
                    return missileNames.find(skills.value(row->second,field));
                };
                const bool directed = function == 116 || function == 117;
                auto missile = missileFor(directed ? "cltmissile" : "cltmissilea");
                if (directed && (missile == missileNames.end() || missiles.number(missile->second,"Id").value_or(0) <= 0))
                    missile = missileFor("cltmissilea");
                if (missile == missileNames.end()) {
                    effectLimitations.insert("PD2 skill missile missing: " + std::to_string(*event.skill));
                    return;
                }
                if (directed && missiles.number(missile->second,"Id").value_or(0) <= 0) return;
                if (function == 117) {
                    if (missiles.number(missile->second,"Id").value_or(0) <= 0 || !target->x || !target->y) return;
                    const int dx = nativeSubtract(int(target->x),int(actor.position->x));
                    const int dy = nativeSubtract(int(target->y),int(actor.position->y));
                    if (nativeAdd(nativeMultiply(dx,dx),nativeMultiply(dy,dy)) > 10000) return;
                }
                const auto count = function == 113 ? skillCalculation(*event.skill,level,"cltcalc1",event.source) :
                    function == 100 ? skillCalculation(*event.skill,level,"calc2",event.source) :
                    std::optional<int32_t>{function == 101 ? 8 : 6};
                if (!directed && (!count || *count < 0)) {
                    effectLimitations.insert("PD2 skill ring count unresolved: " + std::to_string(*event.skill));
                    return;
                }
                if (function == 113 && *count == 0) return;
                shared.cancelPendingClientMissiles(effectOwner(event.source));
                missileCastRevisions[event.source] = source->second.actionRevision;
                const float release = castAnimation->releaseTime-age;
                if (directed) {
                    launch(missile->second,origin,destination,level,release,{},event.source,-1,0,ClientMissileSource::Cast,
                        function==116?effectOwner(*event.target):EntityId{});
                } else {
                    // CN helper 369AF0 / 36BBC0 advances by integer 64/count.
                    // Six requests therefore emit seven rays; do not resample
                    // a floating-point six-sided circle or round the step.
                    const int rays = *count == 0 || *count > 64 ? 64 : *count;
                    const Vec center = function == 100 || function == 113 ? origin : destination;
                    for (int direction = 0; direction < 64; direction += 64/rays)
                        launch(missile->second,center,center+missileRingDirection(direction),level,release,{},event.source,
                            -1,0,ClientMissileSource::Program);
                    if (function == 101) {
                        const auto second = missileFor("cltmissileb"), third = missileFor("cltmissilec");
                        if (second != missileNames.end() && third != missileNames.end()) {
                            launch(second->second,center,center,level,release,{},event.source);
                            launch(third->second,center,center,level,release,{},event.source);
                        } else effectLimitations.insert("PD2 skill secondary missile missing: " + std::to_string(*event.skill));
                    }
                }
                return;
            }
            if (function == 83 || function == 94 || function == 97 || function == 98 ||
                (function >= 104 && function <= 119)) {
                effectLimitations.insert("PD2 skill client program unavailable: " + std::to_string(function));
                return;
            }
        }
        if(function==73) {
            // Clt73 samples the caster's position at its action callback,
            // after any authoritative placement/correction has arrived.
            const auto arrival=missileNames.find(skills.value(row->second,"cltmissileb"));
            if(arrival!=missileNames.end() && castAnimation && castAnimation->releaseTime>=0)
                ownerMissiles[event.source]={arrival->second,*event.skill,1,
                    time+castAnimation->releaseTime-age,source->second.assignmentRevision,
                    source->second.actionRevision,source->second.positionDiscontinuity};
            return;
        }
        if(function==75) {
            if(castAnimation && castAnimation->releaseTime>=0)
                shared.shakeScreen(skills.number(row->second,"Param1").value_or(0),skills.number(row->second,"Param2").value_or(0),
                    skills.number(row->second,"Param3").value_or(0),skills.number(row->second,"Param4").value_or(0),castAnimation->releaseTime-age);
            return;
        }
        if(function==42 && skills.number(row->second,"seqnum")==15) {
            if(!castAnimation || castAnimation->releaseTimes.size()!=2) return;
            auto target=event.point;if(!target && event.target) target=targetPosition(*event.target);if(!target) return;
            const Vec origin{float(actor.position->x)+.5f,float(actor.position->y)+.5f},destination{float(target->x)+.5f,float(target->y)+.5f};
            shared.cancelPendingClientMissiles(effectOwner(event.source));missileCastRevisions[event.source]=source->second.actionRevision;
            for(int hand=0;hand<2;++hand) for(const auto &[key,item]:v.world.items) {
                (void)key;if(item.ownerType!=event.source.type || item.owner!=event.source.id || item.mode!=1 || item.body!=4+hand) continue;
                const auto id=shared.weaponMissile(item.code);if(!id || !missileRows.contains(*id)) continue;
                launch(missileRows.at(*id),origin,destination,event.level.value_or(1),castAnimation->releaseTimes[size_t(hand)]-age,{},event.source);
            }
            return;
        }
        auto missile = missileNames.find(skills.value(row->second, function ? "cltmissilea" : "cltmissile"));
        if(function==95 && actor.key.type==1) {
            const auto identity=monsterRows.find(unitClass(actor).value_or(UINT16_MAX));
            if(identity==monsterRows.end()) return;
            const auto extra=monsterExtra.find(monstats.value(identity->second,"MonStatsEx"));
            if(extra==monsterExtra.end()) return;
            try {
                net::protocol::BitReader bits(actor.appearanceBits);bits.read(4);const bool components=bits.read(1)!=0;
                unsigned element=0;
                for(size_t part=0;part<=11;++part) {
                    const auto values=variants(monstats2.value(extra->second,std::string(componentCodes[part])+"v"));
                    const auto selected=components?bits.read(values.size()>=3?std::bit_width(unsigned(values.size()-1)):1):0;
                    if(part==11) element=selected;
                }
                if(missile==missileNames.end() || element>=4) return;
                const int id=missiles.number(missile->second,"Id").value_or(-1)+int(element);
                const auto variant=missileRows.find(id);if(variant==missileRows.end()) return;
                missile=missileNames.find(missiles.value(variant->second,"Missile"));
            } catch(const std::exception &) {return;}
        }
        if (function == 1 || function == 2 || function == 17 || function==18 || function==20) {
            const bool secondary=skills.value(row->second,"skill")=="Left Hand Throw" || skills.value(row->second,"skill")=="Left Hand Swing";
            int primaryBody=5;
            for(const auto &[key,item]:v.world.items) {
                (void)key;if(item.ownerType!=source->second.key.type || item.owner!=source->second.key.id || item.mode!=1 || item.body!=4) continue;
                for(size_t weapon=0;weapon<weapons.rows().size();++weapon) if(weapons.value(weapon,"code")==item.code) primaryBody=4;
            }
            for (const auto &[id, item] : v.world.items) {
                (void)id;
                if (item.ownerType != source->second.key.type || item.owner != source->second.key.id ||
                    item.mode != 1 || (item.body != 4 && item.body != 5)) continue;
                for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon) {
                    if (weapons.value(weapon, "code") != item.code) continue;
                    const auto kind = weapons.value(weapon, "wclass");
                    if((function==1 || function==2) && item.body!=(secondary?5:primaryBody)) continue;
                    if ((function == 17 || function==18 || function==20) && kind == "xbw" && !skills.value(row->second, "cltmissileb").empty())
                        missile = missileNames.find(skills.value(row->second, "cltmissileb"));
                    if ((function == 1 && (kind == "bow" || kind == "xbw")) ||
                        function == 2)
                        if (const auto id = shared.weaponMissile(item.code); id && missileRows.contains(*id))
                            missile = missileNames.find(missiles.value(missileRows.at(*id), "Missile"));
                }
            }
        }
        if (missile == missileNames.end()) {
            if (actor.key.type==1 && skills.value(row->second,"skill")=="Attack")
                monsterAttack(event,actor,"A1",age);
            // Quick Strike names its missile in srvMissileA; it is not ClientSend.
            if (actor.key.type==1 && skills.number(row->second,"srvdofunc")==92)
                missile=missileNames.find(skills.value(row->second,"srvmissilea"));
            if (missile==missileNames.end()) return;
        }
        // Retail CltDo28 creates its center at the aimed point (RVA 73DF0
        // -> A1540 -> AFF10), including ClientSend missiles. The flag
        // permits native visibility synchronization; it is not a cast gate.
        const bool pd2MindBlast = function == 8 && v.protocolProfile == "pd2-cn-normal-20261020";
        if (!pd2MindBlast && function != 26 && function != 28 && function!=55 && function!=58 && function!=70 && function!=76 && missiles.number(missile->second, "ClientSend").value_or(0)) return;
        auto target = event.point;
        if (!target && event.target) target=targetPosition(*event.target);
        if (!target && (function == 25 || function==55)) target = actor.position; // Nova has no required target.
        if (!target) return;
        const auto *animation = castAnimation;
        if (!animation || animation->animation.frames.empty() || animation->releaseTime < 0) return;
        shared.cancelPendingClientMissiles(effectOwner(event.source));
        missileCastRevisions[event.source] = source->second.actionRevision;
        const Vec start = presentationOrigin.value_or(Vec{float(actor.position->x) + .5f, float(actor.position->y) + .5f});
        const Vec end{float(target->x) + .5f, float(target->y) + .5f};
        const int level = std::max(1, int(event.level.value_or(1)));
        auto emit = [&](Vec destination, int index = -1) {
            launch(missile->second, start, destination, level, animation->releaseTime-age, {}, event.source, index);
        };
        if((function==24 && (skills.number(row->second,"seqnum")==6 || skills.number(row->second,"seqnum")==18)) || function==54 || function==56 || function==71 || function==77 || (function==6 && skills.value(row->second,"charclass")=="ass")) {
            channels[event.source]={row->second,missile->second,level,time+(function==54?1.f/25.f:animation->releaseTime)-age,time-age,source->second.actionRevision,*target,event.target,initialRandom(source->second.key.id)};
            channels[event.source].releaseAt=time+animation->releaseTime-age;
            if(function==77) channels[event.source].until=time-age+float(std::max(1,skillCalculation(*event.skill,level,"calc2",event.source).value_or(1)))/25.f;
        }
        else if(function==72) {
            const int frames=std::max(1,missiles.number(missile->second,"Range").value_or(0)+level*missiles.number(missile->second,"LevRange").value_or(0));
            int height=skillCalculation(*event.skill,level,"cltcalc1",event.source).value_or(0);
            if(const auto pose=motion.find(event.source);pose!=motion.end()) height-=int(pose->second.riderOffset.y);
            ClientMissileInitialization init;ClientMissileGraphics graphics;
            graphics.position[2]=nativeShiftLeft(height,11);graphics.velocity[2]=nativeShiftLeft(-height/frames,11);init.graphics=graphics;
            launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
        }
        else if(function==74) {
            ClientMissileInitialization init;ClientMissileGraphics graphics;
            if(event.target) if(const auto destination=v.world.units.find(*event.target);destination!=v.world.units.end()) {
                const auto pose=motion.find(*event.target);
                if(pose!=motion.end()) if(const auto *visual=monster(destination->second,walking(destination->second),shared))
                    if(const auto [clock,elapsed]=pose->second.animationTime(time); const auto anchor=visual->riderAnchor(clock,elapsed,pose->second.look,pose->second.animationFrame())) {
                        graphics.position[0]=nativeShiftLeft(int(anchor->x),11);graphics.position[2]=nativeShiftLeft(-int(anchor->y),11);
                    }
            }
            init.graphics=graphics;
            const auto arrival=missileNames.find(skills.value(row->second,"cltmissileb"));
            if(arrival!=missileNames.end()) launch(arrival->second,end,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
        }
        else if(function==5 && skills.value(row->second,"charclass")=="ass") {
            const auto count=skillCalculation(*event.skill,level,"prgcalc1",event.source),range=skillCalculation(*event.skill,level,"aurarangecalc",event.source);
            if(!count || !range) return;
            auto random=initialRandom(uint32_t(int(end.x)));
            for(int index=0;index<*count;++index) {
                const int x=int(end.x)+int(limitedRandom(random,uint32_t(std::max(0,2**range))))-*range;
                const int y=int(end.y)+int(limitedRandom(random,uint32_t(std::max(0,2**range))))-*range;
                const int dx=x-int(start.x),dy=y-int(start.y);
                if(dx*dx+dy*dy>=4)
                    emit({float(x)+.5f,float(y)+.5f});
            }
        }
        else if(function==7 && skills.value(row->second,"charclass")=="ass")
            launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
        else if(function==67 || function==68 || function==69) {
            const int count=skillCalculation(*event.skill,level,"calc1",event.source).value_or(0);
            for(int index=0;index<count;++index) {
                ClientMissileInitialization initialization;initialization.animationRate=function==68?256:512;
                if(function!=67 || index) {
                    initialization.path=ClientMissileInitialization::Path::Charged;
                    initialization.seed=uint32_t(int(end.x)+(function==67?index-1:index));
                }
                launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
            }
        }
        else if(function==76) {
            // Retail CltDo76 (C4CE0): trail positions use integer 100ths,
            // spacing/draw delay from CltMissileB's server Param1/2.
            const int distance=std::max(1,unitCoordinateDistance(int(start.x),int(start.y),int(end.x),int(end.y)));
            const auto trail=missileNames.find(skills.value(row->second,"cltmissileb"));
            int count=0,drawDelay=1;
            if(trail!=missileNames.end()) {
                const int spacing=std::max(1,missiles.number(trail->second,"Param1").value_or(0));
                drawDelay=std::max(1,missiles.number(trail->second,"Param2").value_or(0));
                const int dx=100*(int(end.x)-int(start.x))/distance,dy=100*(int(end.y)-int(start.y))/distance;
                for(int advance=spacing;advance<distance;advance+=spacing,++count) {
                    const int frames=missiles.number(trail->second,"Range").value_or(0)+level*missiles.number(trail->second,"LevRange").value_or(0);
                    ClientMissileInitialization init;init.totalFrames=frames+count*drawDelay;init.hiddenUntilCurrentFrame=frames-count*drawDelay;
                    const Vec at{float(int(start.x)+advance*dx/100)+.5f,float(int(start.y)+advance*dy/100)+.5f};
                    launch(trail->second,at,at,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
                }
            }
            const int frames=missiles.number(missile->second,"Range").value_or(0)+level*missiles.number(missile->second,"LevRange").value_or(0);
            ClientMissileInitialization init;init.totalFrames=frames+count*drawDelay;init.hiddenUntilCurrentFrame=frames-count*drawDelay;
            if(const auto rate=skillCalculation(*event.skill,level,"calc1",event.source);rate && *rate>0) init.animationRate=*rate;
            launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
        }
        else if(function==70 || function==81) {
            ClientMissileInitialization initialization;
            initialization.parameters=std::array<int32_t,2>{-(int(end.y)-int(start.y)),int(end.x)-int(start.x)};
            launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
        }
        else if(function==78) {
            launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
            const auto blast=missileNames.find(skills.value(row->second,"cltmissileb"));
            if(blast!=missileNames.end()) launch(blast->second,end,end,level,animation->releaseTime-age,{},event.source);
        }
        else if (function == 26) {
            // Retail CltDo26 / RVA 74770 creates two perpendicular makers
            // at the target, then their CltSubMissile1 center fire.
            const Vec heading = missileWallDirection({float(actor.position->x), float(actor.position->y)}, end);
            for (const auto direction : {heading, heading * -1.f})
                launch(missile->second, end, end + direction, level, animation->releaseTime-age, {}, event.source,
                    -1, 0, ClientMissileSource::Cast);
            const auto fire = missileNames.find(missiles.value(missile->second,"CltSubMissile1"));
            if (fire != missileNames.end())
                launch(fire->second, end, end, level, animation->releaseTime-age, {}, event.source,
                    -1, 0, ClientMissileSource::Cast);
        }
        else if(function==8 && skills.value(row->second,"charclass")=="ass") {
            ClientMissileInitialization initialization;
            const auto child=missileNames.find(skills.value(row->second,"cltmissileb"));
            initialization.streamMissile=child==missileNames.end()?-1:missiles.number(child->second,"Id").value_or(-1);
            initialization.streamRange=skillCalculation(*event.skill,level,"aurarangecalc",event.source);
            if (pd2MindBlast) {
                // CN CltDo8 extends the base radius by the observed charge
                // state's AuraStat1, rather than the actor's cumulative stat.
                const auto snapshot = combat.states().find(event.source);
                if (snapshot == combat.states().end() || !snapshot->second.decoded || !initialization.streamRange) {
                    effectLimitations.insert("PD2 skill radius/state unresolved: " + std::to_string(*event.skill));
                    return;
                }
                for (const auto &[state, record] : snapshot->second.states) {
                    (void)state;
                    if (record.name != skills.value(row->second,"aurastate")) continue;
                    const auto stat = std::find_if(monsterText->itemStats.begin(),monsterText->itemStats.end(),
                        [&](const auto &entry) { return entry.name == skills.value(row->second,"aurastat1"); });
                    const auto increment = skillCalculation(*event.skill,level,"calc1",event.source);
                    if (stat == monsterText->itemStats.end() || !stat->id || !increment) {
                        effectLimitations.insert("PD2 skill charge radius unresolved: " + std::to_string(*event.skill));
                        return;
                    }
                    int count = 0;
                    for (const auto &value : record.stats)
                        if (value.id == *stat->id && !value.parameter) count = int(value.value);
                    initialization.streamRange = nativeAdd(*initialization.streamRange,
                        nativeMultiply(std::max(count,1)-1,*increment));
                    break;
                }
            }
            launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source,
                -1,0,ClientMissileSource::Cast,{},0,initialization);
        }
        else if (function == 28 || function==36 || function==30 || function==31 || function==32 || function==33)
            launch(missile->second, end, end, level, animation->releaseTime-age, {}, event.source,
                -1, 0, ClientMissileSource::Cast);
        else if(function==18) launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,event.target?effectOwner(*event.target):EntityId{});
        else if(function==22 && event.target) {
            // Retail CltDo22 RVA15660: origin is the struck unit; Calc1
            // searches a GUID successor, Calc2 carries the full hop count.
            const auto radius=skills.number(row->second,"calc1").value_or(0);
            std::vector<uint64_t> eligible;
            for(const auto &[key,u]:v.world.units) if(key.type==1 && key!=*event.target && u.position && combat.hostileSource(u) && !onlineMonsterCorpse(u)) {
                const Vec at{float(u.position->x)+.5f,float(u.position->y)+.5f};
                const Vec d{std::floor(at.x)-std::floor(end.x),std::floor(at.y)-std::floor(end.y)};
                if(d.x*d.x+d.y*d.y<=float(radius*radius) && collision.missileSegment(end-worldOrigin,at-worldOrigin,{4,1})) eligible.push_back(key.id);
            }
            const auto id=missileChainSuccessor(event.target->id,eligible);
            if(const auto u=v.world.units.find({1,uint32_t(id)});id && u!=v.world.units.end() && u->second.position)
                launch(missile->second,end,{float(u->second.position->x)+.5f,float(u->second.position->y)+.5f},level,animation->releaseTime-age,{},event.source);
        }
        else if(function==20) {
            std::vector<uint64_t> targets;
            const auto radius=skills.number(row->second,"Param5").value_or(0);
            for(const auto &[key,u]:v.world.units) if(key.type==1 && u.position && combat.hostileSource(u) && u.mode!=0 && u.mode!=12 && missileDistance(start,{float(u.position->x),float(u.position->y)})<=radius) targets.push_back(key.id);
            uint64_t previous=event.target?event.target->id:0;
            bool first=true;
            for(float release:animation->releaseTimes) {
                const auto id=first && std::find(targets.begin(),targets.end(),previous)!=targets.end()?previous:missileChainSuccessor(previous,targets);
                first=false;
                const auto u=v.world.units.find({1,uint32_t(id)});
                if(!id || u==v.world.units.end() || !u->second.position) break;
                launch(missile->second,start,{float(u->second.position->x)+.5f,float(u->second.position->y)+.5f},level,release-age,{},event.source);
                previous=id;
            }
        }
        else if(function==19 && skills.value(row->second,"calc1")=="par1+lvl/par2") {
            const auto count=skills.number(row->second,"Param1").value_or(0)+level/std::max(1,skills.number(row->second,"Param2").value_or(0));
            // Retail CltDo19 RVA15AB0: origin=target, aim=2*target-owner.
            for(int index=0;index<count && index<256;++index) launch(missile->second,end,end*2.f-start,level,animation->releaseTime-age,{},event.source,index);
        }
        else if(function==79 && skills.number(row->second,"cltstfunc")==51) {
            // Retail CltSt51 retains the corpse coordinates; CltDo79
            // constructs cltmissilea there at the action frame.
            launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
        }
        else if (!function || function == 1 || function == 2 || function==27 || function==35 || function==95 ||
            (function == 29 && missiles.number(missile->second, "pCltDoFunc") == 19)) emit(end);
        else if(function==58) {
            const auto count=skillCalculation(*event.skill,level,"calc1",event.source);
            if(!count || *count<0 || *count>512) return;
            for(int index=0;index<*count;++index) {
                ClientMissileInitialization init;init.path=ClientMissileInitialization::Path::DiabWall;
                init.seed=uint32_t(int(std::floor(end.x))+index);
                launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
            }
        }
        else if(function==55) {
            // Retail CltDo55 (581F0) mirrors Do99: flags0x1F, two raw
            // fixed speeds, loop extension and the same ordered 16 rays.
            const auto loops=skillCalculation(*event.skill,level,"calc1",event.source);
            const auto spacing=skillCalculation(*event.skill,level,"calc2",event.source);
            if(!loops || !spacing) return;
            const auto emitCloud=[&](int index,int parameter) {
                ClientMissileInitialization init;
                init.velocityRaw=nativeShiftLeft(missiles.number(missile->second,parameter==1?"Param1":"Param2").value_or(0),6);
                init.totalFrames=missiles.number(missile->second,"Range").value_or(0)+level*missiles.number(missile->second,"LevRange").value_or(0);
                if(missiles.number(missile->second,"SubLoop").value_or(0))
                    *init.totalFrames+=*loops*(missiles.number(missile->second,"SubStop").value_or(0)-missiles.number(missile->second,"SubStart").value_or(0));
                launch(missile->second,start,start+primePoisonNovaDirection(index),level,animation->releaseTime-age,{},event.source,
                    -1,0,ClientMissileSource::Cast,{},0,init);
            };
            for(int index=0;index<16;index+=2) emitCloud(index,1);
            const int step=std::max(*spacing,1);
            if(step>1) for(int index=0;index<15;index+=step) emitCloud(index+1,2);
        }
        else if (function == 25 || function==61) {
            // Retail CltDo25 / RVA 73CB0 -> A0DB0 emits all 64 integer
            // ring directions. Trap Nova shares this with Nova/Frost Nova.
            const int bonus=skills.value(row->second,"cltcalc1")=="par1 * (lvl-1)"
                ?skills.number(row->second,"Param1").value_or(0)*(level-1):0;
            for (int direction = 0; direction < 64; ++direction)
                launch(missile->second,start,start+missileRingDirection(direction),level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Program,{},bonus);
        } else if (function == 51 && actor.key.type == 1) {
            // Retail GargoyleTrap / RVA 585C0 and ObjMode's real GT actor:
            // choose the closer cardinal axis, four subtiles, then use its
            // original muzzle offset. Sequence release flags supply timing.
            const auto ray=gargoyleTrapRay(start,end,GargoyleRaySide::Client);
            auto fire = [&](float release) {
                launch(missile->second,ray.first+Vec{.5f,.5f},ray.second+Vec{.5f,.5f},level,
                       release-age,{},event.source);
            };
            if (animation->releaseTimes.empty()) fire(animation->releaseTime);
            else for (const float release : animation->releaseTimes) fire(release);
        }
        else if (function==48 && skills.number(row->second,"srvdofunc")==88 && animation->releaseTimes.size()==9) {
            for (size_t index=0;index<animation->releaseTimes.size();++index) {
                const auto ray=andarielSprayRay(start,end,4+int(index));
                // Keep the renderer's subtile centre; native packets carry cells.
                launch(missile->second,start,ray.second+Vec{.5f,.5f},
                    level,animation->releaseTimes[index]-age,{},event.source);
            }
        } else if (function == 23 || (function == 17 && skills.number(row->second, "srvdofunc") == 8)) {
            auto formula = skills.value(row->second, "calc1");
            if (formula.size() >= 2 && formula.front() == '"' && formula.back() == '"') {
                formula.remove_prefix(1); formula.remove_suffix(1);
            }
            int count = skillCalculation(*event.skill,level,"calc1",event.source).value_or(0);
            if (formula == "lvl+2") count = level + 2;
            else if (formula.starts_with("min(") && formula.ends_with(",ln12)")) {
                const auto limit = formula.substr(4, formula.size() - 10);
                int cap = 0;
                const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), cap);
                if (parsed.ec == std::errc{} && parsed.ptr == limit.data() + limit.size())
                    count = std::min(cap, skillRankBonus({skills.number(row->second,"Param1").value_or(0),
                        skills.number(row->second,"Param2").value_or(0)},level));
            } else if (formula.starts_with("min(ln12,") && formula.ends_with(")")) {
                const auto limit = formula.substr(9, formula.size() - 10);
                int cap = 0;
                const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), cap);
                if (parsed.ec == std::errc{} && parsed.ptr == limit.data() + limit.size())
                    count = std::min(cap, skillRankBonus({skills.number(row->second,"Param1").value_or(0),
                        skills.number(row->second,"Param2").value_or(0)},level));
            }
            if (count > 0 && count <= 256) {
                if (function == 23)
                    for (int index = 0; index < count; ++index) emit(end, index);
                else {
                    auto facing = (end - start).unit();
                    if (facing.length() < .001f && source->second.direction) {
                        const auto direction = *source->second.direction;
                        facing = missileRingDirection(direction);
                    }
                    for (const auto destination : missileFanTargets(start, end, count, facing)) emit(destination);
                }
            }
            else effectLimitations.insert("Unresolved client bolt count: " + std::string(skills.value(row->second, "skill")));
        } else effectLimitations.insert("Skill client program unavailable: " + std::string(skills.value(row->second, "skill")));
        if(function==30 || function==32 || function==33) {
            const auto extra=missileNames.find(skills.value(row->second,"cltmissilec"));
            if(extra!=missileNames.end()) launch(extra->second,function==30?start:end,function==30?start:end,level,animation->releaseTime-age,{},event.source);
        }
        if(function==32) {const auto blast=missileNames.find(skills.value(row->second,"cltmissileb"));if(blast!=missileNames.end()) launch(blast->second,end,end,level,animation->releaseTime-age,{},event.source);}
        if(function==33) {
            const auto cloud=missileNames.find(skills.value(row->second,"cltmissileb"));
            if(cloud!=missileNames.end()) for(const auto direction:poisonCloudDirections(2,0)) launch(cloud->second,end,end+direction.direction,level,animation->releaseTime-age,{},event.source);
        }
    };
    for (const auto &event : v.world.combatEvents) {
        if (event.sequence <= combatSequence) continue;
        combatSequence = event.sequence;
        if(event.hitClass && (event.kind==OnlineCombatEvent::Kind::Hit ||
            (event.kind==OnlineCombatEvent::Kind::Action && event.source.type==1 && event.action==6))) {
            const auto now=observed;
            const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds?float(now-event.receivedMilliseconds)/1000.f:0;
            // Retail 1.13c RVA 19A1F / 2A5C7: nonzero low nibble
            // enables the element overlay attached to the damaged unit.
            std::string_view name;
            if((*event.hitClass&15)!=0 && age<=.25f) switch(*event.hitClass&0xf0) {
                case 0x10:name="doubledamage1";break;
                case 0x20:name="fire_hit";break;
                case 0x30:name="ice_explode";break;
                case 0x40:name="lightning";break;
                case 0x70:name="bash";break;
                case 0x80:name="hit_thorns";break;
                case 0x90:name="sanctuaryknockback";break;
            }
            if(const auto hit=overlayNames.find(name);!name.empty() && hit!=overlayNames.end()) overlay(int(hit->second),event.source);
        }
        if(event.source.type==1 && (event.kind==OnlineCombatEvent::Kind::Action || event.kind==OnlineCombatEvent::Kind::Hit)) {
            const auto unit=v.world.units.find(event.source);
            if(unit!=v.world.units.end() && unit->second.position) {
                auto actor=unit->second;
                if(event.action==6) {actor.mode=3;actor.actionSkill.reset();}
                else if(event.action==8) {actor.mode=0;actor.actionSkill.reset();}
                const auto now=observed;
                const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds?float(now-event.receivedMilliseconds)/1000.f:0;
                // Resource decoding must not make a newly received death callback stale.
                const auto *animation=monster(actor,false,shared);
                const auto identity=monsterIdentities.find(event.source);
                if(age<=.25f) {
                    const auto has=[&](int modifier){return identity!=monsterIdentities.end() && std::find(identity->second.modifiers.begin(),identity->second.modifiers.end(),modifier)!=identity->second.modifiers.end();};
                    const auto original=monsterRows.find(unitClass(unit->second).value_or(UINT16_MAX));
                    const auto ai=original==monsterRows.end()?std::string_view{}:monstats.value(original->second,"AI");
                    const auto suffix=worldDifficulty==1?"(N)":worldDifficulty==2?"(H)":"";
                    const int suicideParameter=(ai=="SuicideMinion" || has(33)) && original!=monsterRows.end()
                        ?monstats.number(original->second,std::string("aip1")+suffix).value_or(0):-1;
                    monsterEffects.observe(unit->second,{identity!=monsterIdentities.end() && identity->second.unique,has(17),has(18),ai=="Scarab",ai=="SandMaggotQueen",suicideParameter},event,time,age,animation?animation->fps:0);
                }
                if(event.action==8 && age<=.25f && unitClass(unit->second) && deathCallbacks.insert(event.source).second) {
                    const auto row=monsterRows.find(*unitClass(unit->second));
                    if(row!=monsterRows.end()) {
                        const auto code=monstats.value(row->second,"Id");
                        const Vec at{float(unit->second.position->x)+.5f,float(unit->second.position->y)+.5f};
                        const auto deathMissile=[&](const char *name) {
                            if(const auto missile=missileNames.find(name);missile!=missileNames.end()) launch(missile->second,at,at,1,-age,{},event.source,-1,0,ClientMissileSource::Death);
                        };
                        // Retail death dispatcher 20FB0: a fresh DEATH notification,
                        // never inferred from an already assigned corpse.
                        if(code=="andariel") deathMissile("andycontrol0");
                        else if(code=="bloodraven") {
                            deathMissile("brdeathcontrol"); deathMissile("brdeathspirits1"); deathMissile("brdeathsmokes1");
                        }
                        else if(code=="ancientbarb1" || code=="ancientbarb2" || code=="ancientbarb3") {
                            const int operation=code=="ancientbarb1"?63:code=="ancientbarb2"?64:62;
                            for(const auto &[key,statue]:v.world.units) if(key.type==2 && statue.position) {
                                bool matches=false;
                                for(size_t object=0;object<objects.rows().size();++object)
                                    if(objects.number(object,"Id")==unitClass(statue) && objects.number(object,"OperateFn")==operation) {matches=true;break;}
                                if(!matches) continue;
                                if(const auto center=missileNames.find("ancient death center");center!=missileNames.end()) {
                                    ClientMissileInitialization initialization;initialization.distanceLimited=true;
                                    initialization.parameters=std::array{int32_t(key.id),int32_t{0}};
                                    launch(center->second,at,Vec{float(statue.position->x)+.5f,float(statue.position->y)+.5f},1,-age,{},
                                        event.source,-1,0,ClientMissileSource::Death,{},0,initialization);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (event.kind == OnlineCombatEvent::Kind::Sound) {
            // PlayerStats_LevelUp attaches event 2 to the levelling player;
            // SUnitMsg sends original 0x2C. Never infer a level/reward locally.
            const auto source = v.world.units.find(event.source);
            const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            const float age = event.receivedMilliseconds && now >= event.receivedMilliseconds
                ? float(now-event.receivedMilliseconds)/1000.f : 0.f;
            if (source != v.world.units.end() && event.source.type==0 && event.auxiliary==2 && age<=.25f)
                emitSound(PresentationSoundEvent::Kind::LevelUp,source->second,age);
            if (source != v.world.units.end() && age <= .25f) {
                using Kind = PresentationSoundEvent::Kind;
                // Original 1.13c attached sound event dispatch, not Sounds.Index.
                if (event.source.type == 0 && event.auxiliary == 22)
                    emitSound(Kind::NeedKey,source->second,age);
                else if (event.source.type == 0 && event.auxiliary == 11)
                    emitSound(Kind::Original,source->second,age,-1,-1,"item_key_used");
                else if (event.source.type == 2 && (event.auxiliary == 13 || event.auxiliary == 14))
                    emitSound(Kind::Original,source->second,age,-1,-1,
                        event.auxiliary == 13 ? "object_trap_trigger" : "object_trap_release");
            }
            continue;
        }
        if (event.kind == OnlineCombatEvent::Kind::Action) {
            const auto source = v.world.units.find(event.source);
            if (source == v.world.units.end() || !event.action) continue;
            const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            const float age = event.receivedMilliseconds && now >= event.receivedMilliseconds
                ? float(now - event.receivedMilliseconds) / 1000.f : 0.f;
            if (age > .25f) continue; // Never replay actions accumulated during loading or debug pause.
            if (event.source.type == 1) {
                monsterSoundEvent(event, source->second, age);
                const auto action=*event.action;
                const auto pose=action==10||action==11?"A1":action==16||action==17?"A2":
                    action==12||action==13?"S1":action==14||action==15?"S2":
                    action==26||action==27?"S3":action==28||action==29?"S4":"";
                if (*pose) monsterAttack(event,source->second,pose,age);
            }
            else if (event.source.type == 0 && event.packet == 0x0D && unitClass(source->second) &&
                     *unitClass(source->second) < characterRows.size() && (*event.action == 6 || *event.action == 8)) {
                emitSound(*event.action == 6 ? PresentationSoundEvent::Kind::Hit :
                    PresentationSoundEvent::Kind::Death, source->second, age);
            }
            continue;
        }
        if (event.kind == OnlineCombatEvent::Kind::Overlay && event.overlay) overlay(*event.overlay, event.source);
        else if (event.kind == OnlineCombatEvent::Kind::Missile && event.missile && event.point && event.missileDestination) {
            const auto row = missileRows.find(*event.missile);
            if (event.flags != 0)
                effectLimitations.insert("Missile packet flags unavailable: " + std::to_string(*event.missile));
            if (row != missileRows.end() && event.flags == 0 && event.auxiliary > 0)
                launch(row->second, {float(event.point->x) + .5f, float(event.point->y) + .5f},
                    {float((*event.missileDestination)[0]) + .5f, float((*event.missileDestination)[1]) + .5f},
                    event.level.value_or(1), 0, float(event.auxiliary) / 25.f, event.source, -1, event.pierce.value_or(0),
                    ClientMissileSource::Synchronization);
        } else if(event.kind==OnlineCombatEvent::Kind::Skill && event.packet==0xA3 && event.skill && !event.target && event.point) {
            const auto row=skillRows.find(*event.skill);if(row==skillRows.end()) continue;
            const int function=skills.number(row->second,"cltprgfunc1").value_or(0),rank=event.level.value_or(1);
            const Vec point{float(event.point->x)+.5f,float(event.point->y)+.5f};
            if(function==91 || function==92) {
                for(int index=0;index<2;++index) {
                    const auto child=missileNames.find(skills.value(row->second,index?"cltmissileb":"cltmissilea"));
                    if(child==missileNames.end()) continue;ClientMissileInitialization init;
                    if(function==91 && index) {
                        init.parameters=std::array<int32_t,2>{std::bit_cast<int32_t>(event.flags),0};
                        init.streamRange=skillCalculation(*event.skill,rank,"aurarangecalc",event.source).value_or(1);
                        init.streamPeriod=skillCalculation(*event.skill,rank,"calc4",event.source).value_or(1);
                    }
                    if(function==92) {
                        const int frames=skillCalculation(*event.skill,rank,"cltcalc1",event.source).value_or(0);
                        const int z=skillCalculation(*event.skill,rank,"cltcalc2",event.source).value_or(0);
                        const int x=skillCalculation(*event.skill,rank,"cltcalc3",event.source).value_or(0);
                        if(frames<=0 || z<=0 || x<=0) continue;
                        ClientMissileGraphics graphics;
                        graphics.position={nativeShiftLeft(nativeMultiply(x,frames),11),0,nativeShiftLeft(nativeMultiply(z,frames),11)};
                        graphics.velocity={nativeShiftLeft(-x,11),0,nativeShiftLeft(-z,11)};init.graphics=graphics;
                    }
                    launch(child->second,point,point,rank,0,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
                }
            }
        } else if(event.kind==OnlineCombatEvent::Kind::Skill && event.packet==0xA3 && event.skill && event.target) {
            const auto row=skillRows.find(*event.skill);const auto target=v.world.units.find(*event.target);
            if(row!=skillRows.end() && target!=v.world.units.end() && target->second.position && event.flags>0 && event.flags<=3 && skills.number(row->second,"progressive")==1) {
                const auto at=*target->second.position;const Vec point{float(at.x)+.5f,float(at.y)+.5f};
                const auto level=std::max(1,int(event.level.value_or(1)));
                std::array<uint16_t,3> functions{};
                for(int i=0;i<3;++i) functions[size_t(i)]=uint16_t(skills.number(row->second,"cltprgfunc"+std::to_string(i+1)).value_or(0));
                const auto steps=assassinProgressiveSteps(int(event.flags),skills.number(row->second,"prgstack")==1,functions);
                const auto calculate=[&](std::string_view field,int count) {return skillCalculation(*event.skill,level,field,event.source,count);};
                for(const auto &step:steps) {
                    const int function=step.function;if(!function) continue;
                    if(function==12 || function==13) {
                        const auto visual=overlayNames.find(skills.value(row->second,"prgoverlay"));
                        if(visual!=overlayNames.end()) overlay(int(visual->second)+step.charge-1,function==12?*event.target:event.source);
                        continue;
                    }
                    const auto name=skills.value(row->second,"cltmissile"+std::string(1,char('a'+step.charge-1)));
                    const auto missile=missileNames.find(name);if(missile==missileNames.end()) continue;
                    const auto emit=[&](Vec start,Vec end,ClientMissileInitialization initialization={}) {
                        launch(missile->second,start,end,level,0,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
                    };
                    auto seed=initialRandom(event.skillArguments?(*event.skillArguments)[0]:0);
                    auto radius=calculate("prgcalc"+std::to_string(step.charge),step.charge);
                    if(!radius || !*radius) radius=calculate("aurarangecalc",step.charge);
                    if(function==14) emit(point,point);
                    else if(function==10) for(int index=0;index<64;++index) emit(point,point+missileRingDirection(index));
                    else if(function==9 && radius && *radius>0) {
                        for(int index=0;index<*radius**radius;++index) {
                            const int x=*radius-int(limitedRandom(seed,uint32_t(2**radius))),y=*radius-int(limitedRandom(seed,uint32_t(2**radius)));
                            if(x*x+y*y<=*radius**radius) {const auto cell=point+Vec{float(x),float(y)};emit(cell,cell);}
                        }
                    } else if((function==11 || function==83) && radius && *radius>0) {
                        for(int index=0;index<64;index+=*radius) {
                            ClientMissileInitialization initialization;initialization.seed=rollRandom(seed);
                            initialization.path=function==11?ClientMissileInitialization::Path::Charged:ClientMissileInitialization::Path::Spiral;
                            emit(point,point+missileRingDirection(index),initialization);
                        }
                    } else if(function==15 && radius && *radius>0) {
                        for(int index=0;index<*radius;++index) {
                            int x=int(rollRandom(seed)%40)-20,y=int(rollRandom(seed)%40)-20;if(!x && !y) x=20;
                            ClientMissileInitialization initialization;
                            initialization.parameters=std::array<int32_t,2>{nativeNarrow(uint32_t(seed)),nativeNarrow(uint32_t(uint16_t(x))|(uint32_t(uint16_t(y))<<16))};
                            emit(point,point+Vec{float(x),float(y)},initialization);
                        }
                    }
                }
            } else if(row!=skillRows.end() && target!=v.world.units.end() && target->second.position && event.flags==0) {
                const auto missile=missileNames.find(skills.value(row->second,skills.number(row->second,"cltprgfunc1")==93?"cltmissileb":"cltmissilea"));
                if(missile!=missileNames.end()) {
                    const auto at=*target->second.position;const Vec point{float(at.x)+.5f,float(at.y)+.5f};
                    launch(missile->second,point,point,event.level.value_or(1),0,{},event.source);
                }
            }
        } else if (event.kind == OnlineCombatEvent::Kind::Skill && event.skill &&
                   (event.packet == 0x4C || event.packet == 0x4D || event.packet == 0x99 || event.packet == 0x9A)) {
            // Forced owner synchronization replaces the local pose, without duplicating its effects.
            const bool own = playerId && event.source == OnlineUnitKey{0, *playerId};
            const bool alreadyDisplayed = own && localCast && localCast->started >= 0 &&
                localCast->command.skill == *event.skill && time < localCast->started + localCast->duration;
            if (!alreadyDisplayed) skillEffect(event);
            else if (const auto owner = v.world.units.find(event.source); owner != v.world.units.end())
                missileCastRevisions[event.source] = owner->second.actionRevision;
            if (own) localCast.reset();
        }
    }
    for(const auto &release:monsterEffects.advance(v.world,time)) {
        const auto source=v.world.units.find(release.owner);const auto row=missileRows.find(release.missile);
        if(source==v.world.units.end() || !source->second.position || row==missileRows.end()) continue;
        // These MPQ visuals have no rank-dependent velocity or lifetime.
        // Do not infer a hidden monster level if a different table needs it.
        if(missiles.number(row->second,"VelLev").value_or(0) || missiles.number(row->second,"LevRange").value_or(0)) {
            effectLimitations.insert("Monster enchantment requires an unavailable unit level: "+std::to_string(release.missile));continue;
        }
        const Vec start{float(source->second.position->x)+.5f,float(source->second.position->y)+.5f};
        if(release.missile==195)
            for(const auto &ray:monsterLightningRays()) launch(row->second,start,start+ray.offset,1,0,{},release.owner,ray.pathIndex);
        else if(release.missile>=426 && release.missile<=428)
            launch(row->second,start,start,1,0,{},release.owner);
        else if(release.missile==225 || release.missile==321) {
            constexpr std::array<Vec,4> directions{Vec{0,-1},Vec{1,0},Vec{0,1},Vec{-1,0}};
            for(Vec heading:directions) for(int branch=0;branch<2;++branch) launch(row->second,start,start+heading,1,0,{},release.owner,branch);
        }
        else for(const Vec direction:missileRingBurst(1)) launch(row->second,start,start+direction,1,0,{},release.owner);
    }
    const auto source = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
    const auto &interaction=v.world.movementRequest;
    if(interaction && interaction->interaction && interaction->unit && interaction->unit->type==2 &&
        interaction->revision>localObjectKickRevision) {
        localObjectKickRevision=interaction->revision;
        const auto target=v.world.units.find(*interaction->unit);
        if(source!=v.world.units.end() && !onlinePlayerDead(v.world) && target!=v.world.units.end() && unitClass(target->second) && target->second.mode==0 &&
            (!localCast || (localCast->started>=0 && time>=localCast->started+localCast->duration))) {
            for(size_t row=0;row<objects.rows().size();++row)
                if(objects.number(row,"Id")==unitClass(target->second) && objects.number(row,"OperateFn")==5)
                    for(const auto &[id,skillRow]:skillRows)
                        if(skills.number(skillRow,"srvstfunc")==2 && skills.number(skillRow,"srvdofunc")==2) {
                            // Native barrel operation invokes hidden KK; PlrMsg omits the owner's 4C.
                            OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Cast;
                            command.skill=id;command.target=*interaction->unit;command.stationary=true;
                            localCast=LocalCast{command,source->second.actionRevision,time,-1,0,interaction->revision};
                        }
        }
    }
    if (const auto &request = v.world.combatRequest; request && request->sequence > localRequestSequence) {
        localRequestSequence = request->sequence;
        if (request->command.action == OnlineCombatCommand::Action::Cast &&
            request->state == OnlineCombatRequest::State::SentNoAck && source != v.world.units.end()) {
            // PlrMsg::sub_6FC81D20 normally omits skill packets for the owner.
            // Repeated hold requests must not restart an animation before its release frame.
            if (!localCast || localCast->started < 0 || time >= localCast->started + localCast->duration)
                localCast = LocalCast{request->command, source->second.actionRevision, time,
                    -1, 0, request->revision};
        } else if(request->command.action==OnlineCombatCommand::Action::Stop) {
            // Native Rcv0x12 only clears STATE_INFERNO. Mouse-up ends a
            // held gesture, not a normal cast that has yet to release.
            // Keep its pose and scheduled Clt missiles through the action
            // frame; movement, hit recovery and death still interrupt below.
            const bool localChannel = localCast && channelSkill(localCast->command.skill);
            if (playerId) {
                const OnlineUnitKey owner{0, *playerId};
                const bool activeChannel = channels.erase(owner) != 0;
                if (localChannel || activeChannel)
                    shared.cancelPendingClientMissiles(effectOwner(owner));
            }
            if (localChannel) localCast.reset();
        } else if (request->command.action != OnlineCombatCommand::Action::SelectSkill &&
                   request->command.action != OnlineCombatCommand::Action::BindHotkey &&
                   request->command.action != OnlineCombatCommand::Action::Stop)
            localCast.reset();
    }
    if (localCast) {
        const auto &u = source != v.world.units.end() ? source->second : OnlineUnit{};
        const bool interrupted = u.actionRevision != localCast->authorityRevision &&
            (u.actionSkill || (u.nativeMode ? (u.mode == 0 || u.mode == 4 || u.mode == 17 || u.mode == 19)
                : (u.mode == 6 || u.mode == 8 || u.mode == 9 || u.mode == 18 || u.mode == 20)));
        if (!u.position || onlinePlayerDead(v.world) || interrupted ||
             (v.world.movementRequest && v.world.movementRequest->revision > localCast->revision) ||
            (localCast->started < 0 && time - localCast->requested > 15.f)) {
            if (playerId) shared.cancelPendingClientMissiles(effectOwner({0, *playerId}));
            localCast.reset();
        }
        else if (localCast->started < 0) {
            const auto row = skillRows.find(localCast->command.skill);
            auto target = localCast->command.point;
            int targetSize = 0, targetMotionBonus = 0;
            if (localCast->command.target) {
                target=targetPosition(*localCast->command.target);
                const auto found = v.world.units.find(*localCast->command.target);
                if (found != v.world.units.end()) {
                    targetSize = unitSize(found->second);
                    targetMotionBonus = walking(found->second)?1:0;
                }
            }
            bool ready = row != skillRows.end() && target.has_value();
            const auto meleeRange = castMeleeRange(u, localCast->command);
            if (ready && meleeRange) {
                const auto displayed=motion.find(u.key);
                const Vec feet=displayed!=motion.end() && displayed->second.last
                    ? displayed->second.position+displayed->second.correction : Vec{float(u.position->x),float(u.position->y)};
                const auto victim = localCast->command.target ? localUnitPosition(*localCast->command.target)
                    : std::optional<Vec>{Vec{float(target->x),float(target->y)}};
                ready = targetSize > 0 && victim && playerMeleeReach(collision,feet-worldOrigin,*victim-worldOrigin,targetSize,*meleeRange+targetMotionBonus);
            }
            if (ready) {
                auto actor = u; actor.actionSkill = localCast->command.skill;
                const auto *visual = character(actor, false, town, shared);
                if (!visual || visual->fps <= 0) localCast.reset();
                else {
                    localCast->started = time; localCast->duration = visual->holdFrame>=0?std::numeric_limits<float>::infinity():visual->duration();
                    motion[u.key].animation.startedAt = time;
                    OnlineCombatEvent event; event.source = u.key; event.skill = actor.actionSkill;
                    event.point = target; event.target = localCast->command.target;
                    if (const auto rank = v.world.playerSkills.find(*event.skill); rank != v.world.playerSkills.end())
                        event.level = rank->second;
                    const auto &display = motion[u.key];
                    skillEffect(event, display.last ?
                        std::optional{display.position + display.correction + Vec{.5f, .5f}} : std::nullopt);
                }
            }
        }
    }
    for(auto it=ownerMissiles.begin();it!=ownerMissiles.end();) {
        const auto owner=v.world.units.find(it->first);const auto &pending=it->second;
        if(owner==v.world.units.end() || !owner->second.position || owner->second.assignmentRevision!=pending.assignment ||
           onlineMonsterCorpse(owner->second) || (owner->second.actionRevision!=pending.revision &&
           owner->second.positionDiscontinuity==pending.discontinuity && owner->second.actionSkill!=pending.skill)) {
            it=ownerMissiles.erase(it);continue;
        }
        if(time<pending.due) {++it;continue;}
        const auto point=*owner->second.position;const Vec at{float(point.x)+.5f,float(point.y)+.5f};
        launch(pending.missileRow,at,at,pending.level,pending.due-time,{},it->first);
        it=ownerMissiles.erase(it);
    }
    for(auto it=channels.begin();it!=channels.end();) {
        auto &channel=it->second;const auto owner=v.world.units.find(it->first);
        const bool own=playerId && it->first==OnlineUnitKey{0,*playerId};
        if(owner==v.world.units.end() || !owner->second.position || (it->first.type==1?onlineMonsterCorpse(owner->second):playerDead(owner->second)) ||
            (channel.until && time>*channel.until) ||
            (owner->second.actionRevision!=channel.revision && owner->second.actionSkill!=skills.number(channel.skillRow,"Id")) ||
            (own && v.world.movementRequest && v.world.combatRequest && v.world.movementRequest->revision>v.world.combatRequest->revision)) {
            it=channels.erase(it);continue;
        }
        channel.revision=owner->second.actionRevision;
        if(own && v.world.combatRequest && v.world.combatRequest->command.action==OnlineCombatCommand::Action::Cast &&
            v.world.combatRequest->command.skill==skills.number(channel.skillRow,"Id")) {
            if(v.world.combatRequest->command.point) channel.destination=*v.world.combatRequest->command.point;
            channel.target=v.world.combatRequest->command.target;
        }
        if(channel.target) {
            const auto target=v.world.units.find(*channel.target);
            if(target==v.world.units.end() || !target->second.position) {it=channels.erase(it);continue;}
            channel.destination=*target->second.position;
        }
        const Vec start=Vec{float(owner->second.position->x)+.5f,float(owner->second.position->y)+.5f};
        const Vec end{float(channel.destination.x)+.5f,float(channel.destination.y)+.5f};
        const int channelFunction=skills.number(channel.skillRow,"cltdofunc").value_or(0);
        const bool imp=channelFunction==71,diablo=channelFunction==56,arctic=channelFunction==54;
        const auto beamRange=skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"calc1",it->first).value_or(0);
        const int frames=std::max(1,(diablo || arctic)?std::clamp(beamRange>0?beamRange:missiles.number(channel.missileRow,"Param2").value_or(0)+channel.level-1,1,255):
            imp?missiles.number(channel.missileRow,"Param2").value_or(0)+
            skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"cltcalc2",it->first).value_or(0):
            skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"calc1",it->first).value_or(1));
        const bool fury=skills.number(channel.skillRow,"cltdofunc")==6 && skills.value(channel.skillRow,"charclass")=="ass";
        int emitted=0;
        while(channel.next<=time && (diablo || arctic || emitted++<8)) {
            for(int beam=0;beam<(arctic?2:1);++beam) {
                auto variant=channel.missileRow;
                const auto alternate=missileNames.find(skills.value(channel.skillRow,"cltmissileb"));
                if(!imp && !diablo && alternate!=missileNames.end() && (rollRandom(channel.random)&1)) variant=alternate->second;
                int velocity=0;ClientMissileInitialization init;
                if(diablo || arctic) {
                    // 58370 draws light suppression before velocity. Clt54's
                    // D5C78 has -2..2; Clt56 uses the D5BE0 table below.
                    init.suppressLight=(rollRandom(channel.random)&3)!=0;
                    constexpr std::array<int,5> offsets{-1,-3,-2,-1,0};
                    init.velocityRaw=nativeShiftLeft(missiles.number(variant,"Vel").value_or(0)+(arctic?int(limitedRandom(channel.random,5))-2:offsets[limitedRandom(channel.random,5)]),8);
                    init.totalFrames=frames;init.animationRate=(missiles.number(variant,"AnimLen").value_or(0)<<8)/frames;
                }
                if(imp) {
                    variant=channel.missileRow;velocity=int(limitedRandom(channel.random,5))-2;
                    int height=skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"cltcalc1",it->first).value_or(0);
                    if(const auto pose=motion.find(it->first);pose!=motion.end()) height-=int(pose->second.riderOffset.y);
                    ClientMissileGraphics graphics;graphics.position[2]=nativeShiftLeft(height,11);
                    graphics.velocity[2]=nativeShiftLeft(-2*height/frames,11);init.graphics=graphics;init.totalFrames=frames;
                }
                launch(variant,start,end,channel.level,channel.next-time,fury?std::optional<float>{}:std::optional<float>{float(frames)/25.f},it->first,-1,0,ClientMissileSource::Cast,{},velocity,init);
            }
            channel.next+=float(diablo?std::max(1,skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"cltcalc1",it->first).value_or(0)):
                fury?std::max(1,skills.number(channel.skillRow,"Param4").value_or(0)):1)/25.f;
        }
        if(!diablo && !arctic && channel.next<time) channel.next=time; // Clt54/56 retain every native callback.
        ++it;
    }
    if(localCast && channelSkill(localCast->command.skill) && localCast->started>=0 && playerId && !channels.contains({0,*playerId})) localCast.reset();
    if (localCast && localCast->started >= 0 && time >= localCast->started + localCast->duration)
        localCast.reset(); // Released missiles have their own lifetime.
    std::erase_if(overlayVisuals, [&](const auto &effect) {
        return time >= effect.born + effect.duration || !v.world.units.contains(effect.unit);
    });
    while (overlayVisuals.size() > 256) overlayVisuals.pop_front();

}
} // namespace d2x
