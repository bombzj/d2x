#include "remote_scene_impl.hpp"

namespace d2x {
namespace {
bool corpseSpriteHit(const Sprite &image,Vec position,Vec mouse) {
    // Retail D2Client A6730/A6C90: select each original component frame
    // with twelve pixels on each side and a minimum extent of 32.
    for(auto bounds:image.corpseComponentBounds) {
        bounds.x-=12;bounds.y-=12;bounds.width+=24;bounds.height+=24;
        if(bounds.width<32) {const int grow=(32-int(bounds.width))/2;bounds.x-=grow;bounds.width+=2*grow;}
        if(bounds.height<32) {const int grow=(32-int(bounds.height))/2;bounds.y-=grow;bounds.height+=2*grow;}
        bounds.x+=position.x;bounds.y+=position.y;
        if(mouse.x>=bounds.x && mouse.x<=bounds.x+bounds.width &&
           mouse.y>=bounds.y && mouse.y<=bounds.y+bounds.height) return true;
    }
    return false;
}
}

RemoteSceneFrame RemoteScene::Impl::draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding,
                       SceneView &shared, const RemoteCombat &combat, const RemoteUiClients &clients, bool uiConsumed, Vec mouse, bool rightHand, bool paused) {
    RemoteSceneFrame intent;
    players.clear();
    monsters.clear();
    rendered = unavailable = 0;
    playerDisplayed = false;
    soundEvents.clear();
    intent.input.gameGeneration = v.gameGeneration;
    intent.input.areaGeneration = v.world.areaGeneration;
    worldView = &v.world;
    sceneBinding = &binding;
    combatBinding = &combat;
    playerId = v.load.playerUnitId;
    worldDifficulty=v.load.difficulty.value_or(0);
    pd2=v.protocolProfile!="lod113c";
    uiClients=&clients;
    artPalette = binding.palette.value_or(0);
    if(gameGeneration!=v.gameGeneration) pd2VisualSeeds.clear();
    if (gameGeneration != v.gameGeneration || areaGeneration != v.world.areaGeneration) {
        gameGeneration = v.gameGeneration;
        areaGeneration = v.world.areaGeneration;
        motion.clear();
        monsterIdentities.clear();
        monsterTransforms.clear();
        deathCallbacks.clear(); preparedDeaths.clear();
        monsterEffects.clear();
        shared.clearClientMissiles(); channels.clear(); ownerMissiles.clear(); blazeTrails.clear(); effectLimitations.clear(); missileCastRevisions.clear(); overlayVisuals.clear(); stateTimes.clear(); shattered.clear();
        soundEvents.clear();
        combatSequence = v.world.combatSequence;
        localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
        localCast.reset();
        time = 0;
        nextWoundBloodFrame = 0; woundBloodRandom.clear();druidParticles.clear();
        localObjectKickRevision = 0;
    }
    const auto now = std::chrono::steady_clock::now();
    // Refresh the wall-clock anchor while paused and on the resume frame.
    // Menu time must never become world animation/prediction catch-up.
    const float elapsed = paused || wasPaused || lastFrame == std::chrono::steady_clock::time_point{} ? 0.f
        : std::max(0.f, std::chrono::duration<float>(now - lastFrame).count());
    lastFrame = now;
    wasPaused = paused;
    shared.pauseWorldPresentation(paused);
    time += elapsed;
    // Long window/loading waits skip stale queued casts and audio.
    // Assigned actors and active missiles retain their own lifecycles.
    const bool suspended = elapsed > .25f;
    if (suspended) {
        // Monster point paths (67) carry endpoints, not current positions.
        // Clearing motion here would restart them at a stale 68/6D sample
        // every time a boundary room loads. Assignment, removal and native
        // position updates below still own invalidation and reconciliation.
        for(auto &[key,phase]:motion) {(void)key;phase.pd2PlayerLeap.reset();phase.pd2PlayerCharge.reset();}
        monsterEffects.clear();
        for (const auto &[key, revision] : missileCastRevisions) {
            (void)revision;
            shared.cancelPendingClientMissiles(effectOwner(key));
        }
        missileCastRevisions.clear(); channels.clear(); ownerMissiles.clear(); blazeTrails.clear(); overlayVisuals.clear(); localCast.reset();
        soundEvents.clear();
        const auto beforeGap=combatSequence;
        combatSequence = v.world.combatSequence;
        // Skip stale history, but a death just received after the frame gap
        // still owns a fresh callback. Assigned corpses have no such event.
        const auto received=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        for(const auto &event:v.world.combatEvents)
            if(event.sequence>beforeGap && event.source.type==1 && event.kind==OnlineCombatEvent::Kind::Action &&
               event.action==8 && (!event.receivedMilliseconds || (received>=event.receivedMilliseconds && received-event.receivedMilliseconds<=250)))
                combatSequence=std::min(combatSequence,event.sequence-1);
        localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
    }
    if (!binding.origin || !v.world.playerPosition)
        return intent;
    shared.advanceWorldPresentation(suspended ? 0.f : elapsed);
    std::erase_if(deathCallbacks,[&](const auto &key) {
        const auto unit=v.world.units.find(key);
        return unit==v.world.units.end() || !onlineMonsterCorpse(unit->second);
    });
    observeEffects(v, shared, combat, binding.town,map.grid,{float(binding.origin->x),float(binding.origin->y)});
    intent.input.castInputBusy = localCast && !channelSkill(localCast->command.skill) &&
        (localCast->started < 0 || time < localCast->started + localCast->duration);
    // Decode the real death art when a boss is first visible, before its
    // timed callback. A cold child DCC must not interrupt the native program.
    for(const auto &[key,unit]:v.world.units) {
        if(key.type!=1 || !unitClass(unit) || onlineMonsterCorpse(unit) || preparedDeaths.contains(key)) continue;
        const auto row=monsterRows.find(*unitClass(unit));
        if(row==monsterRows.end()) continue;
        const auto code=monstats.value(row->second,"Id");
        if(code!="andariel" && code!="bloodraven") continue;
        preparedDeaths.insert(key);
        auto dead=unit; dead.mode=0; dead.actionSkill.reset(); monster(dead,false,shared);
        std::vector<size_t> pending;
        for(const auto *name:{"andycontrol0","brdeathcontrol","brdeathspirits1","brdeathsmokes1"})
            if((code=="andariel")==std::string_view(name).starts_with("andy"))
                if(const auto missile=missileNames.find(name);missile!=missileNames.end()) pending.push_back(missile->second);
        std::set<size_t> loaded;
        while(!pending.empty()) {
            const auto missile=pending.back();pending.pop_back();
            if(!loaded.insert(missile).second) continue;
            if(const auto id=missiles.number(missile,"Id")) shared.projectileVisual(*id);
            for(const auto *field:{"CltSubMissile1","CltSubMissile2","CltSubMissile3","CltHitSubMissile1","CltHitSubMissile2","CltHitSubMissile3","CltHitSubMissile4","ExplosionMissile"})
                if(const auto child=missileNames.find(missiles.value(missile,field));child!=missileNames.end()) pending.push_back(child->second);
        }
    }
    std::erase_if(blazeTrails,[&](const auto &entry) { return !v.world.units.contains(entry.first); });
    std::erase_if(pd2VisualSeeds,[&](const auto &entry) { return !v.world.units.contains(entry.first); });
    const auto origin = *binding.origin;
    std::vector<ClientMissileTarget> missileTargets;
    for (const auto &[key, unit] : v.world.units) {
        if (!unit.position || (key.type != 0 && key.type != 1) ||
            onlineMonsterCorpse(unit)) continue;
        const bool enemy = combat.hostile(unit);
        if (!enemy && ((key.type==0 && (world().corpseOwners.contains(key.id) || playerDead(unit))) || (key.type!=0 && key.type!=1))) continue;
        missileTargets.push_back({effectOwner(key), displayPosition(unit, map, origin,clients) + Vec{.5f, .5f},
            movementRule(unit).size, enemy});
        missileTargets.back().monster=key.type==1;
        if(key.type==1 && monsterText->nativeMonsterTypes) if(const auto size=monsterText->nativeMonsterTypes->dimensions.find(unitClass(unit).value_or(UINT16_MAX));size!=monsterText->nativeMonsterTypes->dimensions.end()) missileTargets.back().large=size->second.second;
        if(key.type==1) if(const auto row=monsterRows.find(unitClass(unit).value_or(UINT16_MAX));row!=monsterRows.end())
            missileTargets.back().undead=monstats.number(row->second,"lUndead").value_or(0)!=0 || monstats.number(row->second,"hUndead").value_or(0)!=0;
        if(const auto effects=combat.states().find(key);effects!=combat.states().end() && effects->second.decoded)
            for(const auto &[state,record]:effects->second.states) {
                (void)record;const auto stateRow=stateRows.find(state);if(stateRow==stateRows.end()) continue;
                for(const auto &[skill,row]:skillRows) if(skills.value(row,"aurastate")==states.value(stateRow->second,"state") &&
                    skills.number(row,"auraeventfunc1")==1) {
                    const auto missile=missileNames.find(skills.value(row,"srvmissilea"));
                    if(missile!=missileNames.end()) missileTargets.back().retaliation=missiles.number(missile->second,"Id").value_or(-1);
                    if(key.type==0 && key.id==playerId && v.world.playerSkills.contains(uint16_t(skill))) missileTargets.back().retaliationRank=v.world.playerSkills.at(uint16_t(skill));
                    else if(unit.actionSkill==skill) missileTargets.back().retaliationRank=unit.actionSkillLevel.value_or(1);
                }
            }
    }
    if(elapsed>0 && !suspended) emitDruidStates(v,shared,combat,binding.town);
    if (elapsed > 0 && !suspended)
        emitWoundBlood(v,shared,combat,map.grid,{float(origin.x),float(origin.y)});
    shared.advanceClientMissiles(suspended ? 0.f : elapsed, map,
        {float(origin.x), float(origin.y)}, missileTargets);
    clientMissiles.clear();
    for (const auto &missile : shared.clientMissiles())
        clientMissiles.push_back({missile.missileId, missile.pos, missile.age, missile.duration - missile.age});
    const bool waypointOpen = v.world.waypointSource.has_value();
    auto local = [&](OnlinePoint p) {
        return Vec{float(int(p.x) - origin.x), float(int(p.y) - origin.y)};
    };
    Vec observer{float(v.world.playerPosition->x), float(v.world.playerPosition->y)};
    if (playerId) {
        const auto player = v.world.units.find({0, *playerId});
        if (player != v.world.units.end() && player->second.position) observer = displayPosition(player->second, map, origin,clients);
    }
    const Vec camera = observer - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
    shared.ui().camera = project(camera);
    const auto viewport = shared.worldViewport();
    const bool surface = shared.ui().blocksInput() || shared.ui().skillPicker || shared.ui().inventory.drag ||
        shared.ui().inventory.goldDialog || shared.ui().inventory.identify ||
        shared.characterView().dead || hudSurface(mouse) || !CheckCollisionPointRec(rv(mouse),viewport) || uiConsumed;
    const bool menu = shared.ui().gameMenuOpen;
    auto screen = [&](Vec p) { return shared.screen(p); };
    const auto groundTarget = surface ? std::optional<ItemHandle>{} : shared.lootAt(mouse);
    std::optional<size_t> selectedExit;
    OnlineUnitKey selectedTarget{};
    bool hasSelectedTarget=false;
    std::optional<OnlineUnitKey> combatTarget, hoveredPlayer, hoveredCorpse;
    float monsterDistance = std::numeric_limits<float>::max();
    float playerDistance = std::numeric_limits<float>::max();
    float corpseDistance = std::numeric_limits<float>::max();
    if (!menu && !waypointOpen && !hudSurface(mouse)) for (size_t i = 0; i < map.terrain.exits.size(); ++i) {
        const auto &exit = map.terrain.exits[i];
        const bool assigned = std::any_of(v.world.units.begin(), v.world.units.end(), [&](const auto &entry) {
            const auto &[key, unit] = entry;
            return key.type == 5 && unitClass(unit) == exit.selection.id && unit.position &&
                local(*unit.position).x == exit.position.x &&
                local(*unit.position).y == exit.position.y;
        });
        if (!assigned) continue;
        const auto at = screen(exit.position);
        const auto &r = exit.selection;
        if (CheckCollisionPointRec(rv(mouse), {at.x + r.selectX, at.y + r.selectY,
            float(r.selectWidth), float(r.selectHeight)})) {
            selectedExit = i;
            for (const auto &target : binding.mapTargets)
                if (target.unit.type == 5 && local(target.position).x == exit.position.x &&
                    local(target.position).y == exit.position.y) { selectedTarget = target.unit; hasSelectedTarget=true; break; }
            break;
        }
    }
    WorldDrawView worldScene;
    worldScene.map = &map; worldScene.region = shared.mapView().region;
    worldScene.level = binding.area.value(); worldScene.palette = binding.palette.value_or(0);
    worldScene.gameGeneration = v.gameGeneration; worldScene.areaGeneration = v.world.areaGeneration;
    worldScene.observer = camera;
    const auto lightStat = [&](const auto &attributes, const char *name) -> int32_t {
        if (!monsterText->nativeStats) return 0;
        const auto stat = monsterText->nativeStats->names.find(name);
        if (stat == monsterText->nativeStats->names.end() || stat->second > 255) return 0;
        const auto value = attributes.find(uint8_t(stat->second));
        return value == attributes.end() ? 0 : int32_t(value->second);
    };
    worldScene.observerLightColor = uint32_t(lightStat(v.world.playerAttributes, "item_lightcolor"));
    if (playerId) if (const auto light=clients.light({0,*playerId})) worldScene.observerLightColor=light->color;
    worldScene.roomObserver = observer - Vec{float(origin.x), float(origin.y)};
    worldScene.terrainOrigin = {float(origin.x), float(origin.y)};
    worldScene.time = time; worldScene.elapsed = suspended ? 0.f : elapsed;
    worldScene.eclipse=v.world.eclipse;
    worldScene.selectedExit = selectedExit; worldScene.groundHighlight = groundTarget ? groundTarget->id : EntityId{};
    auto &draw = worldScene.items;
    rendered = unavailable = 0;
    playerDisplayed = false;
    float targetDistance = std::numeric_limits<float>::max(), combatDistance = std::numeric_limits<float>::max();
    std::erase_if(motion, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
    std::erase_if(monsterIdentities, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
    std::erase_if(monsterTransforms, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
    std::erase_if(preparedDeaths, [&](const auto &key) { return !v.world.units.contains(key); });
    std::erase_if(shattered, [&](const auto &key) { return !v.world.units.contains(key); });
    std::vector<SoundActorView> soundActors;
    std::map<OnlineUnitKey, size_t> soundActorsByKey;
    for (const auto &[key, unit] : v.world.units) {
        if (key.type > 2 || !unit.position || !unitClass(unit)) continue;
        SoundActorView source;
        source.id = effectOwner(key); source.identity = *unitClass(unit);
        source.kind = key.type == 0 ? SoundActorKind::Player :
            key.type == 1 ? SoundActorKind::Monster : SoundActorKind::Object;
        source.actionRevision = unit.actionRevision;
        source.position = {float(unit.position->x) + .5f, float(unit.position->y) + .5f};
        source.alive = key.type == 2 || (key.type == 1 ? !onlineMonsterCorpse(unit) : !playerDead(unit));
        source.neutral = key.type == 1 && unit.mode == 1 && !unit.actionSkill;
        source.audible = CheckCollisionPointRec(rv(screen(local(*unit.position) + Vec{.5f,.5f})),shared.worldViewport());
        soundActorsByKey.emplace(key,soundActors.size()); soundActors.push_back(source);
    }
    for (const auto &[key, u] : v.world.units) {
        if (!u.position || key.type > 2)
            continue;
        OnlinePlayerDisplay *player = nullptr;
        if (key.type == 0 && unitClass(u)) {
            players.push_back({key.id, u.name, "Outside current map", *unitClass(u),
                {float(u.position->x), float(u.position->y)}, key.id == playerId, false, false, playerDead(u)});
            player = &players.back();
        }
        if (key.type == 0 && onlinePlayerDead(v.world)) {
            const auto corpse = v.world.corpseOwners.find(key.id);
            const auto owner = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
            if (corpse != v.world.corpseOwners.end() && corpse->second == playerId &&
                owner != v.world.units.end() && owner->second.position == u.position) continue;
        }
        Vec feet = key.type == 2 ? local(*u.position)
            : displayPosition(u, map, origin,clients) - Vec{float(origin.x), float(origin.y)};
        if (player) player->position = feet + Vec{float(origin.x), float(origin.y)};
        if (feet.x < 0 || feet.y < 0 || feet.x >= binding.width || feet.y >= binding.height)
            continue;
        if (!unitClass(u)) {
            ++unavailable;
            continue;
        }
        auto &m = motion[key];
        if (key.type == 2 && m.assignmentRevision != u.assignmentRevision) {
            m = {}; m.assignmentRevision = u.assignmentRevision;
        }
        bool frozen=false, hiddenCorpse=false, shatter=false;
        stateColorShifts.erase(key);int colorPriority=0;
        if (key.type<=1) {
            const bool dead = key.type == 0 ? playerDead(u) : onlineMonsterCorpse(u);
            if (!dead) shattered.erase(key);
            if (const auto snapshot=combat.states().find(key); snapshot!=combat.states().end() && snapshot->second.decoded)
                for (const auto &[id,state]:snapshot->second.states) {
                    const auto row=stateRows.find(id);
                    if (row==stateRows.end()) continue;
                    frozen |= !dead && state.name=="freeze";
                    const int priority=states.number(row->second,"colorpri").value_or(0);
                    const auto shift=states.number(row->second,"colorshift");
                    if(!dead && shift && *shift>=0 && *shift<=128 && priority>colorPriority) {colorPriority=priority;stateColorShifts[key]=*shift;}
                    hiddenCorpse |= key.type == 1 && ((dead && states.number(row->second,"hide").value_or(0)) || states.number(row->second,"setfunc")==11);
                    shatter |= key.type == 1 && dead && states.number(row->second,"shatter").value_or(0);
                }
            if (frozen) {
                // Freeze stops prediction as well as animation. The world snapshot
                // retains the original native position/path throughout.
                feet=local(*u.position);
                m.position={float(u.position->x),float(u.position->y)};
                m.correction={}; m.correctionLeft=0; m.route.clear(); m.goal.reset(); m.updatedAt=time;
            }
            if (shatter && shattered.insert(key).second)
                shared.drawNativeIceShatter(feet+Vec{float(origin.x)+.5f,float(origin.y)+.5f},movementRule(u).size);
            if (hiddenCorpse) continue;
        }
        bool attached=false;
        if(key.type==1) if(const auto snapshot=combat.states().find(key);snapshot!=combat.states().end() && snapshot->second.decoded)
            for(const auto &[state,record]:snapshot->second.states) {(void)state;attached|=record.name=="attached";}
        if(attached) {
            if(const auto owner=combat.storedOwner(u)) if(const auto mount=v.world.units.find(*owner);mount!=v.world.units.end() && mount->second.position) {
                const auto pose=motion.find(*owner);
                feet=pose!=motion.end()?pose->second.position+pose->second.correction-Vec{float(origin.x),float(origin.y)}:local(*mount->second.position);
                if(!onlineMonsterCorpse(mount->second)) if(const auto *visual=monster(mount->second,walking(mount->second),shared);visual && pose!=motion.end()) {
                    const auto [clock,elapsed]=pose->second.animationTime(time);
                    const auto anchor=visual->riderAnchor(clock,elapsed,pose->second.look,pose->second.animationFrame());
                    m.riderOffset=anchor.value_or(Vec{})+visual->offset;
                }
            }
        } else m.riderOffset={};
        if (const auto source = soundActorsByKey.find(key); source != soundActorsByKey.end()) {
            auto &audio = soundActors[source->second];
            audio.frozen = frozen;
            audio.position = feet + Vec{float(origin.x) + .5f,float(origin.y) + .5f};
            audio.audible = CheckCollisionPointRec(rv(screen(feet + Vec{.5f,.5f})),shared.worldViewport());
        }
        if (unitClass(u) && key.type == 1 && !onlineMonsterCorpse(u)) worldScene.monsters.push_back({*unitClass(u), feet + Vec{.5f, .5f}});
        if (key.type == 0 && key.id != playerId) {
            // 1.13c 192CE-192F5: other players own unshadowed type-1 lights.
            // Public base stats plus the observed 9D equipment/state lists
            // follow the same callback semantics as the controlled actor.
            uint32_t color=uint32_t(lightStat(u.attributes,"item_lightcolor"));
            if (const auto light=clients.light(key)) {
                const int64_t requested=13+int64_t(light->radiusModifier);
                if (requested>0) m.lightRadius=int(std::min<int64_t>(18,requested));
                color=light->color;
            }
            worldScene.playerLights.push_back({feet + Vec{.5f, .5f}, m.lightRadius,
                color});
        }
        const auto receivedNow = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const float age = u.actionReceivedMilliseconds && receivedNow >= u.actionReceivedMilliseconds
            ? float(receivedNow - u.actionReceivedMilliseconds) / 1000.f : 0.f;
        m.animation.observe(u.mode, u.actionRevision, time, age, frozen);
        const bool localLeap=key.type==0 && key.id==playerId && localCast && localCast->started>=0 &&
            (localCast->command.skill==132 || localCast->command.skill==143);
        if(localLeap || (key.type==0 && (u.actionSkill==132 || u.actionSkill==143)) || (key.type==1 && (u.actionSkill==107 || u.actionSkill==352))) {
            const auto revision=localLeap?localCast->revision:u.actionRevision;
            if(m.leapRevision!=revision) {m.leapRevision=revision;m.leapFlying=false;m.leapLanding=-1;}
            bool flying=false;
            bool known=false;
            if(const auto snapshot=combat.states().find(key);snapshot!=combat.states().end() && snapshot->second.decoded && snapshot->second.sequence==u.stateSequence) {
                known=true;
                for(const auto &[state,record]:snapshot->second.states) {(void)record;if(const auto row=stateRows.find(state);row!=stateRows.end() && states.value(row->second,"state")=="skill_move") flying=true;}
            }
            if(known && flying) m.leapFlying=true;
            else if(known && m.leapFlying && m.leapLanding<0 && !m.pd2PlayerLeap) m.leapLanding=time;
            if(m.leapLanding>=0) m.animation.startedAt=m.leapLanding;
        } else {m.leapFlying=false;m.leapLanding=-1;}
        if(const auto channel=channels.find(key);channel!=channels.end() && u.actionSkill==41 && u.mode==21)
            m.animation.startedAt=channel->second.started;
        if (m.route.empty() && !walking(u) && u.destination) {
            const auto look=local(*u.destination)-feet;
            if(look.length()>.001f) m.look=look;
        }
        if (m.route.empty() && !walking(u) && u.destinationUnit) {
            if (const auto target = localUnitPosition(*u.destinationUnit)) {
                const auto look=*target-Vec{float(origin.x),float(origin.y)}-feet;
                if(look.length()>.001f) m.look=look;
            }
        }
        const bool moving = !frozen && m.pathMoving;
        if (key.type == 0) {
            std::optional<MovementFire> blaze;
            int blazeLevel=1;
            if (!binding.town && !playerDead(u))
                if (const auto snapshot=combat.states().find(key);snapshot!=combat.states().end() && snapshot->second.decoded)
                    for (const auto &[state,record]:snapshot->second.states) {
                        if (const auto missile=movementFireMissiles.find(state);missile!=movementFireMissiles.end()) {
                            if(missile->second.pd2Only && !pd2) continue;
                            blaze=missile->second;
                            const auto rank=monsterText->nativeStats->names.find("modifierlist_level");
                            if(rank!=monsterText->nativeStats->names.end())
                                for(const auto &stat:record.stats) if(stat.id==rank->second && !stat.parameter && stat.value>0)
                                    blazeLevel=int(std::min<int64_t>(stat.value,INT32_MAX));
                        }
                    }
            if (!blaze) blazeTrails.erase(key);
            else {
                const Vec at=feet+Vec{float(origin.x),float(origin.y)};
                const OnlinePoint cell{uint16_t(std::floor(at.x)),uint16_t(std::floor(at.y))};
                const auto [trail,inserted]=blazeTrails.try_emplace(key,BlazeTrail{cell,u.positionDiscontinuity});
                if (!inserted && moving && trail->second.discontinuity==u.positionDiscontinuity && trail->second.cell!=cell) {
                    const Vec center{float(cell.x)+.5f,float(cell.y)+.5f};
                    if (blaze->missile<0 || !shared.launchClientMissile(blaze->missile,center,center,blazeLevel,0,{},-1,effectOwner(key),false,0,ClientMissileSource::Cast))
                        effectLimitations.insert("Blaze client fire unavailable");
                }
                trail->second={cell,u.positionDiscontinuity};
            }
        }
        if (player) { player->moving = moving; player->position = feet + Vec{float(origin.x), float(origin.y)}; }
        if (u.direction && !walking(u)) {
            // Path/Step.cpp measures clockwise from +Y, with its eight-bin offset.
            const float angle = (float(*u.direction & 63) - 7.5f) * 2.f * pi / 64.f;
            m.look = {-std::sin(angle), std::cos(angle)};
        }
        auto displayed = u;
        float objectElapsed = m.animation.elapsed(time);
        if (key.type == 2) {
            const auto presentation = shared.objectPresentation(*unitClass(u), u.mode.value_or(0), objectElapsed);
            displayed.mode = uint8_t(presentation.mode);
            objectElapsed = presentation.elapsed;
            if (m.objectSoundMode != presentation.mode && objectElapsed <= .25f &&
                u.actionRevision != u.assignmentRevision)
                emitSound(PresentationSoundEvent::Kind::ObjectMode,u,objectElapsed,-1,-1,{},presentation.mode);
            m.objectSoundMode = presentation.mode;
            worldScene.objects.push_back({*unitClass(u), displayed.mode.value_or(0), feet});
            // Invisible trap controllers are intentional MPQ Draw=0 units,
            // not failed resources and not substitutes for visible actors.
            if (!presentation.draw) continue;
        }
        if (playerId && key == OnlineUnitKey{0, *playerId} && localCast && localCast->started >= 0 &&
            time < localCast->started + localCast->duration) {
            displayed.actionSkill = localCast->command.skill;
            m.animation.startedAt = localCast->started;
            if(m.pd2PlayerCharge && m.pd2PlayerCharge->attackAt>=0) m.animation.startedAt=m.pd2PlayerCharge->attackAt;
            if(m.leapLanding>=0 && (displayed.actionSkill==132 || displayed.actionSkill==143)) m.animation.startedAt=m.leapLanding;
            auto target = localCast->command.point;
            if (localCast->command.target) target=targetPosition(*localCast->command.target);
            if (target) m.look = local(*target) - feet;
        }
        if (key.type == 0 && v.world.corpseOwners.contains(key.id)) {
            displayed.mode = 17; displayed.nativeMode = true; displayed.actionSkill.reset();
        }
        auto movementMode=locomotionMode(displayed,frozen?walking(displayed):moving,binding.town);
        if(movementMode) {
            displayed.mode=*movementMode;
            displayed.nativeMode=key.type==0;
        }
        const Art *visual = key.type == 0   ? character(displayed, moving, binding.town, shared)
                      : key.type == 1 ? monster(displayed, moving, shared)
                                      : object(displayed, shared);
        if (!visual || visual->animation.frames.empty()) {
            if (player) player->reason = "Original character components or animation unavailable";
            effectLimitations.insert("Original unit animation unavailable: type="+std::to_string(key.type)+
                " class="+std::to_string(unitClass(u).value_or(UINT16_MAX))+" mode="+
                (u.mode?std::to_string(*u.mode):"skill")+" skill="+(u.actionSkill?std::to_string(*u.actionSkill):"none"));
            ++unavailable;
            continue;
        }
        // Native action notification starts an animation; its end is a display transition,
        // not a change to server life, position or mode. Never freeze a completed cast.
        const bool oneShot = u.actionSkill || (key.type == 0 && (u.nativeMode ?
            (u.mode == 4 || (u.mode && *u.mode >= 7 && *u.mode <= 15) || u.mode == 19) :
            (u.mode == 6 || u.mode == 18 || u.mode == 20))) ||
            (key.type == 1 && u.mode && *u.mode >= 3 && *u.mode <= 11);
        const bool completedAction = m.scriptedSequence?m.scriptedSequence->ended:oneShot && visual->finished(m.animation.elapsed(time));
        if (completedAction) {
            auto idle = u; idle.actionSkill.reset(); idle.nativeMode = false;
            idle.mode = key.type == 0 ? 7 : 1;
            visual = key.type == 0 ? character(idle, moving, binding.town, shared) : monster(idle, moving, shared);
            movementMode=locomotionMode(idle,moving,binding.town);
            if (!visual || visual->animation.frames.empty()) {
                if (player) player->reason = "Original neutral animation unavailable";
                ++unavailable; continue;
            }
        }
        if (key.type != 2)
            feet = feet + Vec{.5f, .5f};
        if(movementMode) {
            // UNITS_ChangeAnimMode resets the unit's animation only when
            // its mode changes. Retargeting the same walk/run preserves
            // phase; walk/run/neutral transitions start their own clock.
            const bool changed=m.locomotionAnimation.mode!=movementMode;
            m.locomotionAnimation.observe(movementMode,0,time,0,frozen);
            if(changed) {m.locomotionFrame=0;m.locomotionTick=0;}
            const auto tick=uint64_t(std::floor(double(m.locomotionAnimation.elapsed(time))*25.0+1e-5));
            // Accumulate native fractional frames. A speed/stat change
            // affects future ticks without rescaling the elapsed phase.
            if(tick>=m.locomotionTick)
                m.locomotionFrame=std::fmod(m.locomotionFrame+double(tick-m.locomotionTick)*visual->fps/25.0,double(visual->frameCount));
            m.locomotionTick=tick;
        } else m.locomotionAnimation.mode.reset();
        const auto [animationClock,animationElapsed]=m.animationTime(time);
        if (const auto source = soundActorsByKey.find(key); source != soundActorsByKey.end()) {
            auto &audio = soundActors[source->second];
            audio.position = feet + Vec{float(origin.x),float(origin.y)};
            audio.frozen = frozen;
            audio.moving = moving && visual->cycle && !displayed.actionSkill;
            audio.neutral = !moving && !frozen && ((movementMode && (*movementMode==1 || *movementMode==5)) || completedAction);
            audio.movementCycle = audio.moving ? visual->duration() : 0.f;
            audio.audible = CheckCollisionPointRec(rv(screen(feet)),shared.worldViewport());
        }
        const float runClock=m.scriptedSequence?float(m.scriptedSequence->animation.frame)/256.f/25.f:0;
        const auto *image = key.type == 2 ? visual->sample(objectElapsed, objectElapsed, m.look)
            : m.scriptedSequence && !completedAction?visual->sample(runClock,runClock,m.look)
            : visual->sample(animationClock, animationElapsed, m.look,m.animationFrame());
          const auto p = screen(feet) + visual->offset+m.riderOffset+
              (m.scriptedSequence && m.scriptedSequence->leapGraphics?m.scriptedSequence->leapGraphics->offset():Vec{})+
              (m.pd2PlayerLeap?m.pd2PlayerLeap->graphics.offset():Vec{});
        if (player) player->reason = "Outside viewport";
        if (!image || !visible(*image, p))
            continue;
        if (player) { player->visible = true; player->reason.clear(); }
        if (player && !player->local && !player->dead && !surface && !waypointOpen &&
            !binding.npcConversation && spriteHit(image, p, mouse)) {
            const float distance = (p - mouse).length();
            if (distance < playerDistance) { playerDistance = distance; hoveredPlayer = key; }
        }
        if (!selectedExit && !surface && !waypointOpen && !binding.npcConversation &&
            (key.type == 0 || key.type == 1 || key.type == 2) && spriteHit(image, p, mouse) &&
            std::any_of(binding.mapTargets.begin(), binding.mapTargets.end(),
                [&](const auto &target) { return target.unit == key; })) {
            const float distance = (p - mouse).length();
            if (distance < targetDistance) { targetDistance = distance; selectedTarget = key; hasSelectedTarget=true; }
        }
        const bool corpse=key.type==1 && onlineMonsterCorpse(u);
        const bool combatHit=corpse?corpseSpriteHit(*image,p,mouse):spriteHit(image,p,mouse);
        if (!surface && (key.type == 0 || key.type == 1 || key.type == 2) && combatHit) {
            const float distance = (p - mouse).length();
            if (combat.monsterTargetEligible(u, false) && distance < monsterDistance) {
                monsterDistance = distance;
                intent.hoveredMonster = effectOwner(key);
            }
            const auto selected = rightHand ? v.world.rightSkill : v.world.leftSkill;
            if (selected && combat.skillTargetEligible(u,selected->skill)) {
                if (distance<combatDistance) { combatDistance=distance; combatTarget=key; }
            }
            // Native SelectProc checks both selected hands for hover. Only
            // the active hand supplies the actual cast target above.
            const bool selectedCorpse=corpse &&
                ((v.world.rightSkill && combat.skillTargetEligible(u,v.world.rightSkill->skill)) ||
                 (v.world.leftSkill && combat.skillTargetEligible(u,v.world.leftSkill->skill)));
            if (selectedCorpse && distance<corpseDistance) {
                corpseDistance=distance; hoveredCorpse=key;
            }
        }
        ++rendered;
        if (key.type == 0 && v.load.playerUnitId == key.id)
            playerDisplayed = true;
        WorldDrawItem actorEntry;
        actorEntry.image = image; actorEntry.position = feet; actorEntry.pixelOffset = visual->offset+m.riderOffset;
        actorEntry.shadow = visual->shadow; actorEntry.orderFlag = visual->order; actorEntry.unit = effectOwner(key);
        draw.push_back(actorEntry);
    }
    for (const auto &decoration : map.terrain.clientObjects) {
        if (decoration.type != 2) continue;
        OnlineUnit unit;
        unit.classId = uint16_t(decoration.id);
        unit.mode = 0;
        const auto *visual = object(unit, shared);
        if (!visual || visual->animation.frames.empty()) continue;
        const auto *image = visual->sampleFacing(time, time, 0);
        const Vec feet{float(decoration.x), float(decoration.y)};
        const auto position = screen(feet) + visual->offset;
        worldScene.objects.push_back({decoration.id, 0, feet});
        if (image && visible(*image, position)) {
            WorldDrawItem entry; entry.image = image; entry.position = feet; entry.pixelOffset = visual->offset;
            entry.shadow = visual->shadow; entry.orderFlag = visual->order; draw.push_back(entry);
        }
    }
    auto addOverlay = [&](int id, OnlineUnitKey key, float born, bool loop) {
        const float age = time - born;
        if(age<0) return;
        const auto actor = v.world.units.find(key);
        const auto *visual = shared.overlayVisual(id);
        if (!visual || actor == v.world.units.end() || !actor->second.position) return;
        const bool objectUnit = key.type == 2;
        const Vec feet = objectUnit ? local(*actor->second.position) :
            displayPosition(actor->second, map, origin,clients) - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
        WorldDrawItem entry; entry.position = feet;
        entry.overlay = id; entry.age = age; entry.loop = loop;
        entry.lightOwner = effectOwner(key); entry.lightStartedAt = born;
        entry.height = objectUnit ? 0 : 1;
        if (objectUnit)
            if (const auto owner = std::find_if(draw.begin(), draw.end(),
                    [&](const auto &item) { return item.image && item.unit == effectOwner(key); }); owner != draw.end())
                entry.orderFlag = owner->orderFlag;
        if (key.type == 1 && unitClass(actor->second)) {
            const auto row = monsterRows.find(*unitClass(actor->second));
            const auto extra = row == monsterRows.end() ? monsterExtra.end() : monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
            if (extra != monsterExtra.end()) entry.height = monstats2.number(extra->second,"Height").value_or(1);
        }
        draw.push_back(entry);
    };
    for (const auto &effect : overlayVisuals) addOverlay(effect.id, effect.unit, effect.born, false);
    for (const auto &[key, unit] : v.world.units) {
        // A neutral shrine owns its icon/shimmer. Once the server starts
        // operating it, only the recipient's actual state owns those layers.
        if (key.type != 2 || !unitClass(unit) || unit.mode != 0 || !unit.objectInteractType) continue;
        for (const int id : shared.objectShrineOverlays(*unitClass(unit), *unit.objectInteractType))
            if (id >= 0) addOverlay(id, key, 0, true);
    }
    std::set<std::pair<OnlineUnitKey, uint8_t>> activeStates;
    for (const auto &[key, snapshot] : combat.states()) {
        if (!snapshot.decoded || !v.world.units.contains(key)) continue;
        for (const auto &[id, state] : snapshot.states) {
            const auto row = stateRows.find(id);
            if (row == stateRows.end()) continue;
            const auto identity = std::pair{key,id}; activeStates.insert(identity);
            const auto [epoch,inserted]=stateTimes.try_emplace(identity,time);
            const float born=epoch->second;
            if(inserted) {
                if(states.number(row->second,"setfunc")==2) {
                    int skill=-1,rank=0;
                    const auto skillStat=monsterText->nativeStats->names.at("modifierlist_skill"),rankStat=monsterText->nativeStats->names.at("modifierlist_level");
                    for(const auto &value:state.stats) if(!value.parameter) {
                        if(value.id==skillStat) skill=int(value.value);
                        if(value.id==rankStat) rank=int(value.value);
                    }
                    if(skill>=0 && rank>0) worldScene.cloakDuration=skillCalculation(skill,rank,"auralencalc",key);
                }
                const auto cast=overlayNames.find(states.value(row->second,"castoverlay"));
                if(cast!=overlayNames.end()) {
                    if(const auto *visual=shared.overlayVisual(int(cast->second)))
                        overlayVisuals.push_back({int(cast->second),key,time,visual->frames/visual->fps});
                }
            }
            if(states.number(row->second,"setfunc")==16) {
                const auto stat=[&](std::string_view name) {const int id=monsterText->nativeStats->names.at(std::string(name));
                    for(const auto &value:state.stats) if(value.id==id && !value.parameter) return int(value.value);
                    return 0;};
                const int count=stat("skill_frenzy"),skill=stat("modifierlist_skill"),rank=stat("modifierlist_level");
                const int limit=skillCalculation(skill,rank,"calc2",key).value_or(0);
                if(count>0 && limit>0) for(const auto fields:std::array{std::array{"overlay1","overlay2"},std::array{"overlay3","overlay4"}}) {
                    const auto first=overlayNames.find(states.value(row->second,fields[0])),last=overlayNames.find(states.value(row->second,fields[1]));
                    if(first==overlayNames.end() || last==overlayNames.end() || last->second<first->second) continue;
                    const int slots=int(last->second-first->second)+1,cap=std::min(limit,slots);
                    const int index=nativeMultiply(std::min(count,slots)-1,slots)/cap;
                    if(index>=0 && index<slots) addOverlay(int(first->second)+index,key,born,true);
                }
                continue;
            }
            for (const auto field : {"overlay2","overlay1"}) {
                const auto overlay = overlayNames.find(states.value(row->second,field));
                if (overlay != overlayNames.end()) addOverlay(int(overlay->second),key,born,true);
            }
            const auto progressive=overlayNames.find(states.value(row->second,"pgsvoverlay"));
            const auto statName=states.value(row->second,"stat");
            if(progressive!=overlayNames.end() && !statName.empty()) {
                const auto cost=monsterText->nativeStats->names.find(statName);int count{};
                if(cost!=monsterText->nativeStats->names.end()) for(const auto &value:state.stats) if(value.id==cost->second && !value.parameter) count=int(value.value);
                if(count>0) addOverlay(int(progressive->second)+std::min(count,3)-1,key,born,true);
            }
        }
    }
    std::erase_if(stateTimes,[&](const auto &entry) { return !activeStates.contains(entry.first); });
    // An interactable under the pointer owns both hover and click; an
    // earlier overlapping monster candidate must not turn an NPC click
    // into an attack. Resolve before drawing any selection effects.
    if (combatTarget) hasSelectedTarget=false;
    if (hasSelectedTarget || (combatTarget && !onlineMonsterCorpse(v.world.units.at(*combatTarget)))) hoveredCorpse.reset();
    for (auto &item : draw)
        item.highlighted = !surface && !groundTarget && item.unit &&
            ((hasSelectedTarget && item.unit == effectOwner(selectedTarget)) ||
             (hoveredPlayer && item.unit == effectOwner(*hoveredPlayer)) ||
             (hoveredCorpse && item.unit == effectOwner(*hoveredCorpse)) ||
             (combatTarget && item.unit == effectOwner(*combatTarget)));
    for (const auto &key : v.world.questAlerts) {
        const auto found = motion.find(key);
        if (found != motion.end()) worldScene.alerts.push_back({EntityId{(uint64_t{1} << 32) + key.id + 1},
            found->second.position - worldScene.terrainOrigin});
    }
    intent.visibleMapTiles = shared.drawWorld(worldScene);
    if (hasSelectedTarget && !surface && !groundTarget)
        for (const auto &target : binding.mapTargets) if (target.unit == selectedTarget) {
            const auto drawn = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) { return entry.unit == effectOwner(selectedTarget); });
            const auto at = drawn == draw.end() ? screen(local(target.position)) : screen(drawn->position) + drawn->pixelOffset;
            if (selectedTarget.type == 0 && target.interaction == OnlineMapInteraction::Corpse) shared.drawCorpseLabel(target.name, at);
            else if (selectedTarget.type == 0) {
                const auto unit = v.world.units.find(selectedTarget);
                shared.drawPlayerLabel(target.name, at,
                    unit != v.world.units.end() && combat.hostileSource(unit->second));
            } else {
                const auto localized = strings.find(target.name);
                shared.drawInteractionLabel(localized.empty() ? target.name : std::string(localized), at);
            }
            break;
        }
    if (hoveredCorpse && !surface && !groundTarget && !hasSelectedTarget)
        intent.hoveredMonster=effectOwner(*hoveredCorpse);
    shared.drawGroundLabels(mouse);
    if (hoveredPlayer && !hasSelectedTarget && (!combatTarget || combatTarget->type==0) && !groundTarget) {
        const auto actor = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) {
            return entry.unit == effectOwner(*hoveredPlayer);
        });
        if (actor != draw.end()) shared.drawPlayerLabel(v.world.units.at(*hoveredPlayer).name,
            screen(actor->position) + actor->pixelOffset, combat.hostileSource(v.world.units.at(*hoveredPlayer)));
    }
    auto automap = binding.automap;
    // Native hosts send path verification sparsely. The same collision-
    // checked position drives camera, visibility, effects and automap each
    // frame; packet coordinates remain the authority/acknowledgement state.
    automap.observer = observer;
    if (playerId && shared.ui().bhShowPathLine) {
        // BH path guidance on the automap: lines from the player's own dot
        // to waypoints, next-scene exits and quest points (0x8A hints).
        for (const auto &target : binding.mapTargets)
            if (target.interaction == OnlineMapInteraction::Waypoint ||
                target.interaction == OnlineMapInteraction::Exit)
                automap.pathLines.push_back({{float(target.position.x), float(target.position.y)},
                    target.interaction == OnlineMapInteraction::Waypoint ? uint8_t(0) : uint8_t(1)});
        for (const auto &key : v.world.questAlerts) {
            const auto unit = v.world.units.find(key);
            if (unit == v.world.units.end() || !unit->second.position) continue;
            const Vec position{float(unit->second.position->x), float(unit->second.position->y)};
            automap.pathLines.push_back({position, uint8_t(2)});
            automap.markers.push_back({position, -1, {}, false, false,
                AutomapDrawView::UnitMark::Quest, std::pair{key.type, key.id}, 0, {}, {}});
        }
    }
    if (shared.ui().bhShowChests) {
        // BH Show Chests: unopened selectable containers (IsObjectChest
        // OperateFn list) get a 3x3 palette-white box on the automap.
        constexpr std::array<int, 12> chestOps{{1, 3, 4, 5, 7, 14, 19, 20, 33, 48, 51, 68}};
        for (const auto &[key, unit] : v.world.units) {
            const auto cls = unitClass(unit);
            if (key.type != 2 || !unit.position || !cls || unit.mode != 0) continue;
            const auto row = objectRows.find(int(*cls));
            if (row == objectRows.end()) continue;
            if (!objects.number(row->second, "Selectable0").value_or(0)) continue;
            const auto operate = objects.number(row->second, "OperateFn");
            if (!operate || std::find(chestOps.begin(), chestOps.end(), *operate) == chestOps.end()) continue;
            automap.markers.push_back({{float(unit.position->x), float(unit.position->y)}, -1, {}, false,
                false, AutomapDrawView::UnitMark::Chest, std::pair{key.type, key.id}, 0, {}, {}});
        }
    }
    if (shared.ui().bhShowMonsters) {
        // BH Maphack::OnAutomapDraw draws a Crosshook per live monster of the
        // loaded rooms; IsValidMonster (D2Helpers.cpp) drops corpses, quest
        // triggers and named placeholder units.
        std::set<std::pair<uint8_t, uint32_t>> drawn;
        for (const auto &marker : binding.automap.markers)
            if (marker.unit) drawn.insert(*marker.unit);
        constexpr std::array<uint16_t, 30> hidden{{227, 283, 326, 327, 328, 329, 330, 410, 411, 412,
            413, 414, 415, 416, 366, 406, 351, 352, 353, 266, 408, 516, 517, 518, 519, 522, 523, 543,
            543, 545}};
        for (const auto &[key, unit] : v.world.units) {
            const auto cls = unitClass(unit);
            if (key.type != 1 || !unit.position || !cls || onlineMonsterCorpse(unit) ||
                unit.mode == 0 || unit.mode == 12) continue;
            if (drawn.contains({key.type, key.id})) continue;
            // Own pets keep the RemoteTown pet marks; foreign summons are
            // monsters too and get the BH cross.
            const auto pet = v.world.pets.find(key.id);
            if (pet != v.world.pets.end() && playerId && pet->second.owner == *playerId) continue;
            if (std::find(hidden.begin(), hidden.end(), *cls) != hidden.end()) continue;
            if ((*cls == 608 && unit.mode == 8) ||
                ((*cls == 68 || *cls == 258 || *cls == 261) && unit.mode == 14)) continue;
            if (unit.name == "an evil force" || unit.name == "dummy" || unit.name == "Maggot") continue;
            if (const auto row = monsterRows.find(*cls); row != monsterRows.end() &&
                monstats.number(row->second, "npc").value_or(0)) continue;
            decodeMonsterIdentity(unit);
            // BH "Monster Color" defaults, overridden by rank like Maphack.cpp.
            uint8_t color = 0x5B;
            if (const auto identity = monsterIdentities.find(key); identity != monsterIdentities.end()) {
                if (identity->second.unique || identity->second.superUnique) color = 0x84;
                if (identity->second.champion) color = 0x91;
                if (identity->second.minion) color = 0x60;
            }
            automap.markers.push_back({{float(unit.position->x), float(unit.position->y)}, -1, {}, false,
                false, AutomapDrawView::UnitMark::Monster, std::pair{key.type, key.id}, color,
                shared.ui().bhShowResistances ? monsterAutomapResist(unit) : std::string{},
                shared.ui().bhShowEnchantments ? monsterAutomapEnchant(unit) : std::string{}});
        }
    }
    for (auto &marker : automap.markers) if (marker.unit) {
        const OnlineUnitKey key{marker.unit->first, marker.unit->second};
        const auto unit = v.world.units.find(key);
        const auto moving = motion.find(key);
        if (unit != v.world.units.end() && moving != motion.end() &&
            moving->second.assignmentRevision == unit->second.assignmentRevision)
            marker.position = moving->second.position + moving->second.correction;
    }
    shared.drawAutomap(automap);
    auto &hit = intent.input;
    hit.available = !surface && !waypointOpen && !v.world.npcConversation;
    hit.targetingAvailable = !shared.ui().blocksInput() && !shared.ui().skillPicker &&
        !shared.ui().inventory.drag && !shared.ui().inventory.goldDialog &&
        !shared.ui().inventory.identify && !shared.characterView().dead && !waypointOpen && !v.world.npcConversation;
    hit.movementAvailable = binding.movementAvailable;
    hit.origin = {float(origin.x), float(origin.y)};
    hit.size = {float(binding.width), float(binding.height)};
    hit.observer = observer;
    const auto aim = shared.world(mouse);
    hit.point = {std::floor(aim.x) + origin.x, std::floor(aim.y) + origin.y};
    hit.interaction = hasSelectedTarget ? effectOwner(selectedTarget) : EntityId{};
    hit.combat = combatTarget ? effectOwner(*combatTarget) : EntityId{};
    if(groundTarget) {
        const auto selected=rightHand?v.world.rightSkill:v.world.leftSkill;
        const auto item=v.world.items.find(uint32_t(groundTarget->id.value-1));
        if(selected && item!=v.world.items.end() && combat.skillTargetEligible(item->second,selected->skill))
            hit.combat=effectOwner({4,item->first});
    }
    hit.pickup = groundTarget;
    const std::array selections{v.world.leftSkill, v.world.rightSkill};
    for (size_t hand = 0; hand < selections.size(); ++hand) {
        if (selections[hand]) hit.skills[hand] = InputSkillSelection{selections[hand]->skill,
            selections[hand]->owner == UINT32_MAX ? EntityId{} : EntityId{uint64_t(selections[hand]->owner) + 1}};
        for (const auto &[key, unit] : v.world.units)
            if (selections[hand] && combat.skillTargetEligible(unit,selections[hand]->skill))
                hit.validCombatTargets[hand].push_back(effectOwner(key));
        for (const auto &[id, item] : v.world.items)
            if (selections[hand] && combat.skillTargetEligible(item, selections[hand]->skill))
                hit.validCombatTargets[hand].push_back(effectOwner({4,id}));
    }
    shared.updateWorldAudio(time,soundActors,soundEvents);
    const auto &audioLimitations = shared.soundLimitations();
    effectLimitations.insert(audioLimitations.begin(),audioLimitations.end());
    // The frame's replica belongs to the caller. Diagnostics retain values,
    // never its address after draw() returns or the next network pump runs.
    for(const auto &[key,transform]:monsterTransforms) {
        const auto unit=v.world.units.find(key);
        if(unit==v.world.units.end() || !unitClass(unit->second)) continue;
        auto [name,modifiers]=monsterCaption(unit->second);
        monsters.push_back({key.id,*unitClass(unit->second),std::move(name),std::move(modifiers),transform.first,transform.second});
    }
    return intent;
}
} // namespace d2x
