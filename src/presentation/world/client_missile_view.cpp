#include "presentation/scene_view.hpp"
#include "presentation/world/client_missile_program.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include "world/navigation.hpp"
#include "world/map.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>

namespace d2x {
namespace {
bool matchesCreation(const ClientMissileVisual &effect, int id, EntityId owner, Vec position, Vec direction,
                     uint8_t source, float age, float window,std::optional<int> skill) {
    return effect.creationSources && !(effect.creationSources & source) && effect.missileId == id &&
        (!skill || !effect.sourceSkill || skill==effect.sourceSkill) &&
        effect.owner == owner && (effect.creationPosition-position).length() <= .001f &&
        (!effect.velocityFixed || (effect.creationDirection-direction.unit()).length() <= .001f) &&
        std::abs(effect.age-age) <= window;
}
}
bool SceneView::launchClientMissile(int id, Vec start, Vec target, int level, float delay,
                                  std::optional<float> remaining, int pathIndex,
                                  EntityId owner, bool hostile, int pierce, ClientMissileSource source,int slowPercent,EntityId guidedTarget,int velocityBonus,ClientMissileInitialization initialization) {
    const auto found = assets_.clientMissilePrograms.find(id);
    if (found == assets_.clientMissilePrograms.end()) return false;
    const auto &program = found->second;
    if (program.function>=69 && program.rules!=ClientMissileProgram::Rules::Pd2Cn) return false;
    if(program.function>=38 && program.function<=41 && program.rules!=ClientMissileProgram::Rules::Original) return false;
    if((program.function==30 || program.function==37 || program.function==46) && program.rules!=ClientMissileProgram::Rules::Original) return false;
    if((program.function==16 || program.function==59 || program.function==62 || program.function==63) && program.rules!=ClientMissileProgram::Rules::Original) return false;
    if((program.function==32 || program.function==45 || program.function==54 || program.function==57 || program.function==68) && program.rules!=ClientMissileProgram::Rules::Original) return false;
    // CN replaces these contact programs. Do not display the retail
    // fallback while their target/path inputs are still unsupported.
    if (program.rules==ClientMissileProgram::Rules::Pd2Cn &&
        (program.hitFunction==5 || (program.hitFunction==66 && program.hitChildren[0]>=0))) return false;
    if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==6 &&
        (program.hitChildren[0]<0 || program.serverHitParameters[0]>256 || !assets_.ensureProjectile(program.hitChildren[0]))) return false;
    if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==67 &&
        (!program.hitHeight || program.hitChildren[0]<0 || !assets_.ensureProjectile(program.hitChildren[0]))) return false;
    const auto *capabilities = findClientMissileCapabilities(program.function);
    if (!capabilities || (program.function == 2 && !program.localBlood)) return false;
    if (program.function==9)
        for (const int child:program.children) if (child>=0 && !assets_.ensureProjectile(child)) return false;
    if(program.function==21 || program.function==22 || program.function==29)
        for(const int child:program.children) if(child>=0 && !assets_.ensureProjectile(child)) return false;
    if ((program.function == 4 || program.function==45) && (program.children[0] < 0 || program.parameters[0] < 0 ||
        program.parameters[1] < 0 || program.parameters[1] > 256 || program.parameters[2] < 0 ||
        program.parameters[2] > 32767 || !assets_.ensureProjectile(program.children[0]))) return false;
    if (program.function==9 && program.hitFunction==18)
        for (const int child:program.hitChildren) if (child<0 || !assets_.ensureProjectile(child)) return false;
    if((program.hitFunction==46 || program.hitFunction==47) && (program.hitChildren[0]<0 || !assets_.ensureProjectile(program.hitChildren[0]))) return false;
    if(program.hitFunction==36 || program.hitFunction==48)
        for(size_t index=0;index<3;++index)
            if(program.hitChildren[index]<0 || !assets_.ensureProjectile(program.hitChildren[index])) return false;
    // Lightning's parent is deliberately invisible; its MPQ child is the art.
    if (!assets_.ensureProjectile(id) &&
        !(capabilities->allowChildArtwork && program.children[0] >= 0 &&
          assets_.ensureProjectile(program.children[0]))) return false;
    level = std::max(0, level);
    const auto nativeVelocity = missileVelocityFixed(program.velocity+velocityBonus,program.velocityPerLevel,level,program.canSlow?slowPercent:0);
    if (!nativeVelocity ||
        (program.acceleration && program.maximumVelocity <= 0)) return false;
    const auto rawVelocity=initialization.velocityRaw.value_or(0);
    const auto explicitVelocity=nativeMultiply(program.canSlow && slowPercent?nativeMultiply(rawVelocity,slowPercent)/100:rawVelocity,75)/100;
    const int velocity = capabilities->stationary ? 0 : initialization.velocityRaw?explicitVelocity:
        program.function==4 && program.poisonVelocity[0]>0?poisonCloudVelocity(program.poisonVelocity[0]):*nativeVelocity;
    const int tableFrames=nativeMissileFrameCount(nativeAdd(program.frames,nativeMultiply(level,program.framesPerLevel)));
    const float tableDuration = float(initialization.totalFrames.value_or(tableFrames)) / 25.f;
    const float fullDuration = tableDuration > 0 ? tableDuration : assets_.projectileVisuals.at(id).lifetime;
    const float duration = remaining.value_or((program.groundThrow || initialization.distanceLimited)?float(groundThrowFrames(start,target,velocity))/25.f:program.function==7 && !guidedTarget?std::max(1.f/25.f,(target-start).length()/std::max(.001f,float(velocity)*25.f/4096.f)):fullDuration);
    if (duration <= 0 || !std::isfinite(duration) || !std::isfinite(delay) ||
        !std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(target.x) || !std::isfinite(target.y)) return false;
    // Stationary 73 sends FirstX/FirstY = 0; these are not an aimed ray.
    if (capabilities->stationary) target = start;
    const int constructorFrames=(source!=ClientMissileSource::Synchronization && (program.groundThrow || initialization.distanceLimited))?int(std::lround(duration*25.f)):tableFrames;
    const std::optional<int> activation=nativeMissileFrameCount(nativeSubtract(initialization.totalFrames.value_or(constructorFrames),
        initialization.activationDelay.value_or(program.activationDelay)));
    const auto sources = uint8_t(source);
    const bool reconstructed = (capabilities->pairCreationSources || program.clientSend) && source!=ClientMissileSource::Death;
    if (sources && reconstructed) {
        // 73 has no missile GUID. Pair the two creation sources for the same
        // owner/class/origin/ray within the cast release and event freshness window.
        // Never merge two predictions or two authoritative synchronizations.
        const float expectedAge = -delay + (remaining ? std::max(0.f, fullDuration - duration) : 0.f);
        for (auto it = clientMissiles_.rbegin(); it != clientMissiles_.rend(); ++it) {
            if (!matchesCreation(*it,id,owner,start,target-start,sources,expectedAge,.25f+std::max(0.f,delay),initialization.sourceSkill)) continue;
            if(initialization.sourceSkill) it->sourceSkill=initialization.sourceSkill;
            it->creationSources |= sources;
            if(initialization.totalFrames) it->nativeTotalFrames=nativeMissileFrameCount(*initialization.totalFrames);
            if(initialization.hiddenUntilCurrentFrame) it->hiddenUntilCurrentFrame=initialization.hiddenUntilCurrentFrame;
            if(initialization.activationDelay || !it->activateAtCurrentFrame) it->activateAtCurrentFrame=activation;
            if(initialization.animationRate) it->animationRate=initialization.animationRate;
            if(initialization.impactCount) it->impactCount=initialization.impactCount;
            if(initialization.chainHits) it->remainingHits=*initialization.chainHits;
            if(initialization.chainRadius) it->chainRadius=*initialization.chainRadius;
            if(initialization.suppressLight) it->suppressLight=*initialization.suppressLight;
            if(initialization.parameters) it->nativeParameters=initialization.parameters;
            if(initialization.velocityRaw) {it->velocityFixed=velocity;it->velocity=(target-start).unit()*(float(velocity)*25.f/4096.f);}
            if(initialization.streamRange) it->streamRange=*initialization.streamRange;
            if(initialization.streamPeriod) it->streamPeriod=*initialization.streamPeriod;
            if(initialization.areaEmission) it->areaEmission=initialization.areaEmission;
            if(initialization.graphics) {
                it->graphics=initialization.graphics;
                for(int frame=0;frame<it->frame;++frame) it->graphics->step();
                it->pixelOffset=it->graphics->offset();
            }
            if (source == ClientMissileSource::Synchronization) {
                it->age = std::max(0.f, it->age);
                it->duration = it->age + duration;
                it->setCurrentFrame(int(std::lround(duration*25.f)));
            }
            return true;
        }
    }
    // Native coordinate-target creation advances both axes for a zero ray.
    if (velocity > 0 && (target - start).length() < .001f) target = start + Vec{1, 1};
    ClientMissile effect{id, start, (target - start).unit() * (float(velocity) * 25.f / 4096.f),
        -delay, duration, target - start};
    effect.flight = true; effect.level = level;
    effect.sourceSkill=initialization.sourceSkill;
    effect.areaEmission=initialization.areaEmission;
    if(!effect.areaEmission && program.blizzard)
        effect.areaEmission=ClientMissileAreaEmission{program.blizzard->radius,program.blizzard->emissionPeriod};
    effect.suppressLight=initialization.suppressLight.value_or(false);
    effect.initialHidden=assets_.projectileVisuals.at(id).initSteps>0;
    effect.deathProgram=source==ClientMissileSource::Death;
    effect.slowPercent=slowPercent;effect.guidance=guidedTarget;
    effect.creationSources = sources;
    effect.creationPosition = start; effect.creationDirection = (target - start).unit();
    if(program.chain) effect.remainingHits=std::max(1,skillRankBonus({program.chain->count,program.chain->countPerLevel},level)/program.chainCountDivisor);
    if(program.chain) effect.chainRadius=program.chain->range;
    if(initialization.chainHits) effect.remainingHits=*initialization.chainHits;
    if(initialization.chainRadius) effect.chainRadius=*initialization.chainRadius;
    effect.velocityFixed = velocity;
    effect.acceleration = program.acceleration; effect.owner = owner; effect.hostile = hostile; effect.pierce = pierce;
    effect.soundEmitter = {(uint64_t{1} << 63) | ++nextClientMissile_};
    effect.random = initialRandom(uint32_t(nextClientMissile_));
    // Retail packet 73 (D2Client RVA AEEE0) constructs the full table range,
    // then calls SetCurrentFrame(packet.frame), leaving TotalFrames untouched.
    // The packet never supplies an animation frame or a callback-age prefix.
    effect.nativeTotalFrames=nativeMissileFrameCount(initialization.totalFrames.value_or(constructorFrames));
    effect.hiddenUntilCurrentFrame=initialization.hiddenUntilCurrentFrame;
    effect.activateAtCurrentFrame=activation;
    effect.setCurrentFrame(int(std::lround(duration*25.f)));
    if(source==ClientMissileSource::Synchronization) {
        const auto &visual=assets_.projectileVisuals.at(id);
        const int animationFrame=int(limitedRandom(effect.random,uint32_t(visual.frames)));
        effect.nativeSequenceFrame=nativeShiftLeft(animationFrame,8);
    }
    effect.turnTarget = target - start;
    effect.pathTarget = target;
    effect.nativeParameters=initialization.parameters;effect.animationRate=initialization.animationRate;
    effect.impactCount=initialization.impactCount;
    if(initialization.pixelHeight) effect.pixelOffset.y=-float(*initialization.pixelHeight);
    effect.graphics=initialization.graphics;
    effect.roomAttachment=initialization.roomAttachment;
    if(initialization.ballisticHeight) {
        effect.graphics=ClientMissileGraphics::arc(effect.nativeCurrentFrames,*initialization.ballisticHeight);
        effect.graphics->step(); // Native creation flag0x100 performs the first Gfx update.
    }
    if(initialization.bounce) {
        const auto &bounce=*initialization.bounce;
        effect.graphics=ClientMissileGraphics::arc(bounce[2],0);effect.graphics->flags=4;
        effect.graphics->bounces=bounce[0];effect.graphics->restitution=bounce[1];
    }
    if(effect.graphics) effect.pixelOffset=effect.graphics->offset();
    effect.streamMissile=initialization.streamMissile.value_or(-1);effect.streamRange=initialization.streamRange.value_or(0);effect.streamPeriod=initialization.streamPeriod.value_or(0);
    if(initialization.seed) effect.random=initialRandom(*initialization.seed);
    if(program.function==21) effect.emissionCountdown=50; // Retail BloodRaven callback RVA 52320.
    if(program.deathFlame) {
        effect.pixelVelocity.y=float(program.parameters[0]); effect.pixelAcceleration.y=float(program.parameters[2]);
    }
    if (pathIndex >= 0) {
        if(effect.nativeTotalFrames>=78) {
            effect.nativeTotalFrames=77;
            if(!remaining) {effect.setCurrentFrame(77);effect.duration=77.f/25.f;}
        }
        const auto path = chargedBoltPath(start, target, pathIndex, int(effect.duration * 25.f + .5f));
        effect.path.assign(path.begin(), path.end());
    }
    if(program.blessedHammer || initialization.blessedHammerPath) {
        const auto path=blessedHammerPath(start);effect.path.assign(path.begin(),path.end());
    }
    if(initialization.path!=ClientMissileInitialization::Path::None) {
        int frames=initialization.path==ClientMissileInitialization::Path::Charged?int(uint8_t(effect.nativeTotalFrames)):effect.nativeTotalFrames;
        if(frames>=78) {frames=77;effect.nativeTotalFrames=77;effect.setCurrentFrame(77);effect.duration=77.f/25.f;}
        const bool wall=initialization.path==ClientMissileInitialization::Path::DiabWall;
        if(!wall || rollRandom(effect.random)%100>=20) {
            const auto path=initialization.path==ClientMissileInitialization::Path::Charged || wall?chargedBoltPath(start,target,frames,effect.random):assassinProgressiveSpiral(start,target,frames,effect.random);
            effect.path.assign(path.begin(),path.end());
            // Packet73 carries the initialized path's first point. Keep that
            // ray for pairing its synchronization with this DiabWall cast.
            if(wall && !effect.path.empty()) effect.creationDirection=(effect.path.front()-start).unit();
        }
    }
    if(const auto fall=assets_.blizzardFalls.find(id);fall!=assets_.blizzardFalls.end()) {
        effect.flight=false;effect.initialHidden=false;effect.duration=float((fall->second.fallDistance+fall->second.fallRate-1)/fall->second.fallRate)/25.f;
    }
    clientMissiles_.push_back(std::move(effect)); return true;
}
Vec SceneView::clientMissilePosition(const ClientMissile &effect, const Grid &grid, Vec origin) const {
    if (!effect.flight || effect.age < 0) return effect.pos;
    const float remainder = std::clamp(effect.age - float(effect.frame) / 25.f, 0.f, 1.f / 25.f);
    Vec next = effect.pos + effect.velocity * remainder;
    if (!effect.path.empty()) {
        next = effect.pos;
        float travel = float(effect.velocityFixed) * 25.f / 4096.f * remainder;
        for (const auto point : effect.path) {
            const auto delta = point - next;
            if (delta.length() <= travel) { next = point; travel -= delta.length(); }
            else { next = next + delta.unit() * travel; break; }
        }
    }
    const auto &collision = assets_.clientMissilePrograms.at(effect.missileId).collision;
    if ((!effect.activateAtCurrentFrame || effect.nativeCurrentFrames<=*effect.activateAtCurrentFrame) &&
        (collision.mask & 0x0005) && !grid.missileSegment(effect.pos - origin, next - origin, collision)) {
        float low = 0, high = 1;
        for (int i = 0; i < 12; ++i) {
            const float middle = (low + high) * .5f;
            if (grid.missileSegment(effect.pos - origin, effect.pos + (next - effect.pos) * middle - origin, collision)) low = middle;
            else high = middle;
        }
        next = effect.pos + (next - effect.pos) * low;
    }
    return next;
}
void SceneView::advanceClientMissiles(float dt, const Map &map, Vec origin,
                                    std::span<const ClientMissileTarget> targets,std::span<const ClientMissileAnchor> anchors) {
    if (dt < 0 || !std::isfinite(dt)) return;
    const auto &grid = map.grid;
    const auto townAt = [&](Vec position) -> std::optional<bool> {
        const Vec local = position - origin;
        for (const auto &room : map.terrain.rooms)
            if (local.x >= room.x * 5 && local.y >= room.y * 5 &&
                local.x < (room.x + room.width) * 5 && local.y < (room.y + room.height) * 5)
                return assets_.worldLevel(room.level).town;
        return {};
    };
    // Move the batch aside: client programs may enqueue children while updating.
    auto pending = std::deque<ClientMissile>(clientMissiles_.begin(), clientMissiles_.end());
    clientMissiles_.clear();
    for (auto &effect : pending) effect.age += dt;
    size_t processed = 0;
    const auto relatedOwner=[&](const ClientMissile &effect) {
        return std::any_of(anchors.begin(),anchors.end(),[&](const auto &anchor){return anchor.id==effect.owner;});
    };
    const auto forgeObject=[&](const ClientMissile &effect) -> const ClientMissileAnchor * {
        if(!effect.nativeParameters) return nullptr;
        const auto found=std::find_if(anchors.begin(),anchors.end(),[&](const auto &anchor) {
            return anchor.type==2 && anchor.nativeId==uint32_t((*effect.nativeParameters)[0]);
        });
        return found==anchors.end()?nullptr:&*found;
    };
    auto emit = [&](int id, Vec position, Vec direction, const ClientMissile &parent, float age,int poisonVelocity=-1,int loops=0,int frames=-1,ClientMissileInitialization initialization={}) -> ClientMissile * {
        const auto source = parent.deathProgram ? ClientMissileSource::Death : parent.creationSources ? ClientMissileSource::Cast : ClientMissileSource::Program;
        if (source == ClientMissileSource::Cast)
            for (auto &effect : pending)
                if (matchesCreation(effect,id,parent.owner,position,direction,uint8_t(source),age,.25f,initialization.sourceSkill)) {
                    if(initialization.sourceSkill) effect.sourceSkill=initialization.sourceSkill;
                    effect.creationSources |= uint8_t(source); return &effect;
                }
        const size_t before = clientMissiles_.size();
        if (id < 0 || !launchClientMissile(id, position, position + direction, parent.level,
                0, {}, -1, parent.owner, parent.hostile, 0, source,parent.slowPercent,{},0,initialization)) return nullptr;
        // A synchronized counterpart may have consumed this creation already.
        if (clientMissiles_.size() == before) return nullptr;
        auto child = std::move(clientMissiles_.back()); clientMissiles_.resize(before);
        if(poisonVelocity>=0) {child.velocityFixed=poisonCloudVelocity(poisonVelocity);child.velocity=direction.unit()*(float(child.velocityFixed)*25.f/4096.f);child.duration+=float(loops*assets_.clientMissilePrograms.at(id).loopFrames)/25.f;}
        if(frames>0) child.duration=float(frames)/25.f;
        if(poisonVelocity>=0 || frames>0) {
            child.nativeTotalFrames=nativeMissileFrameCount(int(std::lround(child.duration*25.f)));
            child.setCurrentFrame(child.nativeTotalFrames);
        }
        child.age = std::max(0.f, age); pending.push_back(std::move(child));
        return &pending.back();
    };
    auto impact = [&](ClientMissile &effect, const ClientMissileProgram &program, float overshoot,bool unitContact=false,bool largeMonster=false,std::optional<Vec> unitPosition={}) {
        if(program.rules==ClientMissileProgram::Rules::Original && program.hitFunction==37) {
            if(unitContact) return false;
            // B49F0 reinitializes the hand's seed from the parent's saved
            // CurrentFrame, then emits TargetX holy bolts. A zero ray retries.
            const auto parameters=effect.nativeParameters.value_or(std::array<int32_t,2>{});
            effect.random=initialRandom(uint32_t(parameters[1]));
            const Vec at{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
            for(int index=0;index<parameters[0];) {
                const int x=int(rollRandom(effect.random)%20)-10,y=int(rollRandom(effect.random)%20)-10;
                if(!x && !y) continue;
                ++index;
                ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                initialization.path=ClientMissileInitialization::Path::Charged;
                initialization.seed=uint32_t(int(std::floor(effect.pos.x))+x);
                emit(349,at,{float(x),float(y)},effect,overshoot,-1,0,-1,initialization);
            }
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Original && (program.hitFunction==42 || program.hitFunction==43)) {
            // Retail B5300 / B51C0: the first shard becomes class 375; the
            // second becomes class 376 with hfcontrol's Range, not its own.
            if(relatedOwner(effect)) {
                const int childId=program.hitFunction==42?375:376;
                ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                initialization.parameters=std::array<int32_t,2>{effect.nativeParameters?(*effect.nativeParameters)[0]:0,0};
                if(program.hitFunction==43) {
                    const auto control=assets_.clientMissilePrograms.find(373);
                    if(control==assets_.clientMissilePrograms.end()) return true;
                    initialization.totalFrames=control->second.frames;
                    initialization.velocityRaw=0;
                }
                if(auto *child=emit(childId,effect.pos,{},effect,overshoot,-1,0,-1,initialization))
                    if(program.hitFunction==43) (*child->nativeParameters)[1]=int(rollRandom(effect.random)%35)+10;
            }
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Original && program.hitFunction==60) {
            // B5770 inherits the source skill/rank and puts only child two
            // at cHitPar1's Q11 height. Child identities remain MPQ references.
            ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
            emit(program.hitChildren[0],effect.pos,{},effect,overshoot,-1,0,-1,initialization);
            ClientMissileGraphics graphics;graphics.position[2]=nativeShiftLeft(program.hitParameters[0],11);
            initialization.graphics=graphics;
            emit(program.hitChildren[1],effect.pos,{},effect,overshoot,-1,0,-1,initialization);
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Original && program.hitFunction==51) {
            const int count=program.hitParameters[0];
            if(program.hitChildren[0]<0 || count<=0 || (count-1)/5>7) return true;
            // BA9A0 uses the same A25C0 direction ordering as the existing
            // charged-bolt burst. cHitPar2 alone installs the charged-path init.
            for(const auto direction:missileChargedBurstDirections(count)) {
                ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                if(program.hitParameters[1]) {
                    initialization.path=ClientMissileInitialization::Path::Charged;
                    initialization.seed=uint32_t(int(std::floor(effect.pos.x))+int(direction.x));
                }
                emit(program.hitChildren[0],effect.pos,direction,effect,overshoot,-1,0,-1,initialization);
            }
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==66) {
            // CN 3A2250 returns success without searching when the child is
            // absent (current Holy Light BIN stores FFFF). Valid-child paths
            // remain rejected at launch until their area callback is restored.
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==6) {
            // CN 3A1DC0 reflects only terrain contacts. TargetX counts prior
            // reflections; current/activate/total frames retain distinct roles.
            const int reflections=effect.nativeParameters?(*effect.nativeParameters)[0]:0;
            const int current=effect.nativeCurrentFrames;
            if(unitContact || reflections>=program.hitParameters[2] || current<=1 ||
                current>=effect.nativeTotalFrames || current>effect.activateAtCurrentFrame.value_or(effect.nativeTotalFrames)) return true;
            const auto owner=std::find_if(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;});
            if(owner==targets.end()) return true;
            const Vec cell{std::floor(effect.pos.x-.5f),std::floor(effect.pos.y-.5f)};
            const int size=program.collision.size+1;
            if(grid.missileSegment(cell+Vec{.5f,.5f}-origin,cell+Vec{.5f,.5f}-origin,{0x806,size})) return true;
            const Vec ownerCell{std::floor(owner->position.x-.5f),std::floor(owner->position.y-.5f)};
            const Vec incident=cell-ownerCell;
            if(incident.length()<=0) return true;
            const Vec heading=incident.unit();
            const int count=program.serverHitParameters[0]?program.serverHitParameters[0]:1;
            const int step=program.serverHitParameters[1]?program.serverHitParameters[1]:1;
            const int loss=program.serverHitParameters[2]?program.serverHitParameters[2]:1;
            int offset=nativeMultiply(-(nativeSubtract(count,1)/2),step);
            for(int index=0;index<count;++index,offset=nativeAdd(offset,step)) for(int side=0;side<2;++side) {
                const Vec reflected=side?Vec{heading.x,-heading.y}:Vec{-heading.x,heading.y};
                float angle=float(double(std::atan2(reflected.y,reflected.x))*10.185916357881302);
                if(angle<0) angle+=64.f;
                const Vec direction=missileRingDirection(nativeAdd(int(angle),offset)&63);
                const Vec probe=cell+direction.unit()*float(size);
                const Vec at{float(int(probe.x))+.5f,float(int(probe.y))+.5f};
                // Native 37CDF0 tests the footprint one radius ahead, not
                // the entire outgoing ray.
                if(!grid.missileSegment(at-origin,at-origin,{0x806,size})) continue;
                ClientMissileInitialization init;init.parameters=std::array<int32_t,2>{reflections+1,0};
                if(auto *child=emit(program.hitChildren[0],cell+Vec{.5f,.5f},direction,effect,overshoot,-1,0,-1,init)) {
                    int remaining=std::max(0,nativeSubtract(current,loss));
                    if(!reflections) remaining=std::max(remaining,program.hitParameters[0]);
                    if(remaining<program.hitParameters[1]) remaining=0;
                    child->setCurrentFrame(remaining);child->duration=float(remaining)/25.f;
                }
                break;
            }
            effect.setCurrentFrame(0);
            return true;
        }
        if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==67) {
            // CN 3A2330 creates CltHitSubMissile1 at the struck unit, then
            // C7E30 writes CHitCalc1 as Z (11 fractional bits). cHitPar1-3
            // are raw Gfx velocities, not acceleration or missile lifetime.
            if(unitPosition && program.hitHeight) {
                ClientMissileGraphics graphics;
                graphics.position[2]=nativeShiftLeft(*program.hitHeight,11);
                graphics.velocity=program.hitParameters;
                ClientMissileInitialization init;init.graphics=graphics;
                init.sourceSkill=program.linkedSkill;
                const Vec at{std::floor(unitPosition->x-.5f)+.5f,std::floor(unitPosition->y-.5f)+.5f};
                emit(program.hitChildren[0],at,{},effect,overshoot,-1,0,-1,init);
            }
            return true;
        }
        if (program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==65) {
            if (!effect.owner) return true;
            ClientMissileInitialization init;init.sourceSkill=program.linkedSkill;
            emit(program.hitChildren[0],effect.pos,{},effect,overshoot,-1,0,-1,init);
            if (program.hitChildren[1]>=0) {
                const int count=program.hitParameters[0];
                const int rays=count<=0 || count>64?64:count;
                for (int direction=0;direction<64;direction+=64/rays)
                    emit(program.hitChildren[1],effect.pos,missileRingDirection(direction),effect,overshoot,-1,0,-1,init);
            }
            return true;
        }
        if(program.hitFunction==48) {
            if(!effect.owner) return true;
            emit(program.hitChildren[0],effect.pos,{},effect,overshoot);
            constexpr std::array<Vec,8> offsets{Vec{0,1},Vec{1,1},Vec{1,0},Vec{1,-1},Vec{0,-1},Vec{-1,-1},Vec{-1,0},Vec{-1,1}};
            for(const auto offset:offsets) {
                emit(program.hitChildren[1],effect.pos,offset,effect,overshoot);
                if(auto *child=emit(program.hitChildren[2],effect.pos,offset,effect,overshoot)) {
                    // Hit48 changes TotalFrames only; CurrentFrame keeps the
                    // constructor value. Its second draw changes path velocity.
                    const int frames=nativeSubtract(child->nativeTotalFrames,int(limitedRandom(effect.random,uint32_t(std::max(0,child->nativeTotalFrames/2)))));
                    child->nativeTotalFrames=nativeMissileFrameCount(frames);
                    child->velocityFixed=int(limitedRandom(effect.random,uint32_t(std::max(0,child->velocityFixed))));
                    child->velocity=offset.unit()*(float(child->velocityFixed)*25.f/4096.f);
                    child->graphics=ClientMissileGraphics::arc(frames,30);child->pixelOffset=child->graphics->offset();
                }
            }
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.hitFunction==47) {
            if(!effect.owner) return true;
            int count=nativeAdd(program.hitParameters[0],nativeMultiply(program.hitParameters[1],nativeSubtract(effect.level,1)));
            if(count<=0) {
                if(!effect.impactCount) return true;
                count=std::max(1,*effect.impactCount);
            }
            const Vec center{float(int(effect.pos.x+effect.turnTarget.x))+.5f,float(int(effect.pos.y+effect.turnTarget.y))+.5f};
            effect.random=(effect.random&0xffffffff00000000ULL)|uint32_t(int(center.x));
            const int range=count/4;
            for(int index=0;index<count;++index) {
                const int x=nativeSubtract(int(limitedRandom(effect.random,uint32_t(nativeMultiply(2,range)))),range);
                const int y=nativeSubtract(int(limitedRandom(effect.random,uint32_t(nativeMultiply(2,range)))),range);
                const auto direction=center+Vec{float(x),float(y)}-effect.pos;
                if(direction.x*direction.x+direction.y*direction.y<4) continue;
                ClientMissileInitialization initialization;initialization.distanceLimited=true;initialization.ballisticHeight=30;
                emit(program.hitChildren[0],effect.pos,direction,effect,overshoot,-1,0,-1,initialization);
            }
            emit(program.hitChildren[1],effect.pos,{},effect,overshoot);
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.hitFunction==46) {
            if(!effect.owner) return true;
            int count=nativeAdd(program.hitParameters[0],nativeMultiply(program.hitParameters[1],nativeSubtract(effect.level,1)));
            if(count<=0) {
                if(!effect.impactCount) return true;
                count=std::max(1,*effect.impactCount);
            }
            // The native ring helper asserts past its seven direction groups.
            if((count-1)/5>7) return true;
            for(const auto direction:missileChargedBurstDirections(count)) {
                ClientMissileInitialization initialization;initialization.path=ClientMissileInitialization::Path::Charged;
                initialization.seed=uint32_t(int(effect.pos.x+direction.x));
                emit(program.hitChildren[0],effect.pos,direction,effect,overshoot,-1,0,-1,initialization);
            }
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.hitFunction==36) {
            if(unitContact) return false;
            const Vec center{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
            if(!townAt(center).value_or(false) && grid.missileSegment(center-origin,center-origin,{1,3}))
                for(const auto offset:missileDiskOffsets(program.hellMeteorRadius))
                    if(grid.missileSegment(center+offset-origin,center+offset*2.f-origin,{4,1})) {
                        // Native B9740 draws duration, class, then chance;
                        // rejected chance gates still consume all three draws.
                        const int frames=13+int(rollRandom(effect.random)%25);
                        const auto variant=size_t(limitedRandom(effect.random,3));
                        if(rollRandom(effect.random)%100<75)
                            emit(program.hitChildren[variant],center+offset,{},effect,overshoot,-1,0,frames);
                    }
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.hitFunction==52 && unitContact && !largeMonster) return false;
        if(program.hitFunction==53) {
            const size_t before=clientMissiles_.size();ClientMissileInitialization init;
            init.bounce=std::array{program.hitParameters[0],program.hitParameters[1],program.hitParameters[2]};
            init.sourceSkill=effect.sourceSkill;
            if(launchClientMissile(program.hitChildren[0],effect.pos,effect.pathTarget,effect.level,0,{},-1,effect.owner,effect.hostile,0,ClientMissileSource::Program,effect.slowPercent,{},0,init) && clientMissiles_.size()>before) {
                auto child=std::move(clientMissiles_.back());clientMissiles_.resize(before);child.age=overshoot;pending.push_back(std::move(child));
            }
        }
        if(program.hitFunction==56) emit(program.hitChildren[0],effect.pos,effect.turnTarget,effect,overshoot);
        if(program.hitFunction==62 && effect.owner && program.hitChildren[0]>=0) {
            // CltHit62 preserves the seed high word and reseeds low from X.
            effect.random=(effect.random&0xffffffff00000000ULL)|uint32_t(int32_t(std::floor(effect.pos.x)));
            const int extent=std::max(program.hitParameters[0],1);
            const auto span=uint32_t(nativeAdd(nativeMultiply(2,extent),1));
            const int x=nativeSubtract(int32_t(limitedRandom(effect.random,span)),extent);
            const int y=nativeSubtract(int32_t(limitedRandom(effect.random,span)),extent);
            emit(program.hitChildren[0],effect.pos,Vec{float(x),float(y)},effect,overshoot);
        }
        if(program.hitFunction==63 && effect.owner && program.hitChildren[0]>=0) {
            const auto &cloud=assets_.clientMissilePrograms.at(program.hitChildren[0]);
            for(const auto heading:poisonCloudDirections(2,std::max(program.hitParameters[0],1)))
                emit(program.hitChildren[0],effect.pos,heading.direction,effect,overshoot,cloud.poisonVelocity[heading.secondary?1:0]);
        }
        if(program.hitFunction==50 || program.hitFunction==52) {
            if(!effect.owner) return true;
            const int rockGuard=program.hitChildren[program.hitFunction==50?0:1];
            const int fireMissile=program.hitChildren[program.hitFunction==50?1:2];
            if(program.hitFunction==52) emit(program.hitChildren[0],effect.pos,{},effect,overshoot);
            const int count=program.hitParameters[0],radius=nativeMultiply(count,2),span=nativeAdd(nativeMultiply(radius,2),1);
            if(rockGuard>=0 && count>0) for(int index=0;index<count;++index) {
                const int x=int(limitedRandom(effect.random,uint32_t(span)))-radius;
                const int y=int(limitedRandom(effect.random,uint32_t(span)))-radius;
                const auto at=effect.pos+Vec{float(x),float(y)};
                if(unitCoordinateDistance(int(effect.pos.x),int(effect.pos.y),int(at.x),int(at.y))<=3) continue;
                ClientMissileInitialization init;init.distanceLimited=true;init.ballisticHeight=0;
                init.velocityRaw=nativeShiftLeft(program.hitParameters[1],8);
                emit(program.boulderRocks,effect.pos,at-effect.pos,effect,overshoot,-1,0,-1,init);
            }
            constexpr std::array<Vec,18> fire{Vec{2,-2},Vec{-2,-2},Vec{0,2},Vec{0,5},Vec{-3,3},Vec{0,3},Vec{3,3},Vec{-1,2},Vec{1,1},Vec{-1,-1},Vec{2,-1},Vec{-4,-2},Vec{-3,-2},Vec{-1,-3},Vec{0,-4},Vec{1,-3},Vec{3,-3},Vec{4,-2}};
            for(const auto offset:fire) emit(fireMissile,effect.pos+offset,{},effect,overshoot);
            assets_.sceneAudio.playRegistered("missile-hit:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
            return true;
        }
        if(program.hitFunction==54) emit(program.hitChildren[0],effect.pos,{},effect,overshoot);
        if(program.hitFunction==55)
            for(int index=0;index<3;++index) emit(program.hitChildren[size_t(index)],effect.pos,{},effect,overshoot);
        if(program.hitFunction==44 && program.hitChildren[0]>=0 && !program.childServerSent)
            emit(program.hitChildren[0],effect.pos,{},effect,overshoot);
        if(program.hitFunction==28 && program.hitChildren[0]>=0) {
            const auto &cloud=assets_.clientMissilePrograms.at(program.hitChildren[0]);
            constexpr std::array<Vec,8> directions{Vec{0,2},Vec{2,2},Vec{2,0},Vec{2,-2},Vec{0,-2},Vec{-2,-2},Vec{-2,0},Vec{-2,2}};
            for(Vec direction:directions) emit(program.hitChildren[0],effect.pos,direction,effect,overshoot,cloud.poisonVelocity[0],std::max(0,effect.level-1));
        }
        if(program.hitFunction==2 && program.hitChildren[0]>=0) {
            const auto &cloud=assets_.clientMissilePrograms.at(program.hitChildren[0]);
            for(const auto &heading:poisonCloudDirections(program.hitParameters[1],program.hitParameters[0]))
                emit(program.hitChildren[0],effect.pos,heading.direction,effect,overshoot,cloud.poisonVelocity[heading.secondary?1:0],program.hitParameters[2]);
        }
        if(program.hitFunction==12 && program.hitChildren[0]>=0) {
            const int radius=program.hitParameters[0]>0?program.hitParameters[0]:std::max(1,program.immolationRadius);
            const Vec center{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
            const int duration=program.hitDuration && *program.hitDuration>0?*program.hitDuration:
                program.frames+(effect.level-1)*program.framesPerLevel;
            int variants=1;
            if(program.rules==ClientMissileProgram::Rules::Pd2Cn)
                while(variants<3 && program.hitChildren[size_t(variants)]>=0) ++variants;
            for(const auto offset:missileDiskOffsets(radius)) if(grid.missileSegment(center+offset-origin,center+offset*2.f-origin,{4,1}) &&
                int(limitedRandom(effect.random,100))<program.hitParameters[1])
                emit(program.hitChildren[variants>1?limitedRandom(effect.random,uint32_t(variants)):0],center+offset,{},effect,overshoot,-1,0,duration);
        }
        if((program.hitFunction==25 || program.hitFunction==26) && program.targetBurst && program.hitChildren[0]>=0) {
            const auto &burst=*program.targetBurst;
            std::vector<MissileBurstTarget> candidates;
            for(const auto &target:targets) if(target.id!=effect.owner && target.hostile!=effect.hostile &&
                grid.missileSegment(effect.pos-origin,target.position-origin,{4,1})) candidates.push_back({target.id,target.position});
            const int radius=program.hitParameters[0]>0?program.hitParameters[0]:std::max(1,burst.radius);
            const int count=program.hitParameters[1]>0?program.hitParameters[1]:std::max(1,skillRankBonus({burst.count,burst.countPerLevel},effect.level));
            for(const auto &target:missileBurstTargets(effect.pos,radius,count,candidates))
                emit(program.hitChildren[0],effect.pos,target.position-effect.pos,effect,overshoot);
        }
        // These are client contact images, not confirmation of damage or debuffs.
        const int main = program.hitFunction == 14 || program.hitFunction == 3 || program.hitFunction == 1 || program.hitFunction == 32
            ? program.hitChildren[0] : program.explosion;
        if (!assets_.meteorVisuals.contains(effect.missileId)) emit(main, effect.pos, {}, effect, overshoot);
        const auto first = clientMissiles_.size();
        createMissileImpactVisuals(effect.missileId, effect.pos);
        for (size_t index = first; index < clientMissiles_.size(); ++index) {
            auto child = std::move(clientMissiles_[index]);
            child.deathProgram=effect.deathProgram;
            child.owner=effect.owner;
            child.age = std::max(0.f, overshoot); pending.push_back(std::move(child));
        }
        clientMissiles_.resize(first);
        assets_.sceneAudio.playRegistered("missile-hit:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
        return true;
    };
    while (!pending.empty() && processed++ < 8192) {
        auto effect = std::move(pending.front()); pending.pop_front();
        const auto sourceOwner = std::find_if(targets.begin(),targets.end(),[&](const auto &target) { return target.id == effect.owner; });
        const bool ownerInTown = effect.owner && sourceOwner != targets.end() && townAt(sourceOwner->position).value_or(false);
        if (!effect.flight) {
            // Impact-only images keep the same MPQ owner/town lifetime rule
            // as native missiles, independently of the observer's location.
            if (ownerInTown)
                if (const auto program = assets_.clientMissilePrograms.find(effect.missileId);
                    program != assets_.clientMissilePrograms.end() && program->second.sourceTownFrames > 0)
                    effect.duration = std::min(effect.duration,effect.age + float(program->second.sourceTownFrames) / 25.f);
            // The same lifecycle used by offline impact, ice melt and Blizzard images.
            effect.pos = effect.pos + effect.velocity * dt;
            if (effect.age + .00001f >= effect.duration) {
                auto land = [&](int id, float duration) {
                    if (id < 0 || !assets_.ensureProjectile(id)) return;
                    const float age = std::max(0.f, effect.age - effect.duration);
                    if (age < duration) {
                        clientMissiles_.push_back({id, effect.pos, {}, age, duration, effect.direction});
                        clientMissiles_.back().deathProgram=effect.deathProgram;
                    }
                };
                if (const auto melt = assets_.iceShatterMelts.find(effect.missileId); melt != assets_.iceShatterMelts.end())
                    land(melt->second, assets_.projectileVisuals.at(melt->second).lifetime);
                if (const auto fall = assets_.blizzardFalls.find(effect.missileId); fall != assets_.blizzardFalls.end())
                    land(fall->second.impactId, float(fall->second.impactFrames) / 25.f);
            } else clientMissiles_.push_back(std::move(effect));
            continue;
        }
        if (effect.age < 0) { clientMissiles_.push_back(std::move(effect)); continue; }
        const auto &program = assets_.clientMissilePrograms.at(effect.missileId);
        if(program.function==43) {
            // Retail B2890 follows the owner's precise path coordinates and
            // calls UNITS_UpdateFrame, without the common lifetime/collision step.
            // This target list contains living players/monsters only; a missing
            // or dead owner removes the current MPQ's hit-less attached effects.
            const auto owner=std::find_if(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;});
            if(owner==targets.end()) continue;
            const int lastFrame=int(effect.age*25.f+.00001f);
            auto animation=program.animation;animation.loop=true;animation.subLoop=false;
            if(effect.animationRate) animation.rate=nativeMultiply(*effect.animationRate,4);
            while(effect.frame<lastFrame) {
                if(!effect.frame) assets_.sceneAudio.playRegistered("missile-release:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
                effect.nativeSequenceFrame=advanceMissileAnimation(effect.nativeSequenceFrame,effect.nativeCurrentFrames,animation);
                ++effect.frame;
            }
            effect.pos=owner->position;
            effect.duration=effect.age+float(std::max(1,effect.nativeCurrentFrames))/25.f;
            clientMissiles_.push_back(std::move(effect));continue;
        }
        if (program.function == 2 && program.localBlood) {
            // Retail CltDo2 (RVA B3060): freeze at animation end, reset the
            // remaining counter to 128, and discard blood outside the view.
            // These are local particles, with no contacts or damage events.
            const auto &visual = assets_.projectileVisuals.at(effect.missileId);
            const auto at = screen(effect.pos - origin);
            if (at.x < 0 || at.x > W || at.y < -8 || at.y > H - HUD + 64) continue;
            const int lastFrame = int(effect.age * 25.f + .00001f);
            const int movingFrames = visual.fps > 0 ? int(std::ceil(float(visual.frames) * 25.f / visual.fps)) : 0;
            while (effect.frame < std::min(lastFrame,movingFrames)) {
                effect.pos = effect.pos + effect.velocity * (1.f / 25.f);
                if(effect.animationRate) {
                auto animation=program.animation;animation.rate=nativeMultiply(*effect.animationRate,4);
                effect.nativeSequenceFrame=advanceMissileAnimation(effect.nativeSequenceFrame,effect.nativeCurrentFrames,animation);
            } else effect.nativeSequenceFrame=advanceMissileAnimation(effect.nativeSequenceFrame,effect.nativeCurrentFrames,program.animation);
                ++effect.frame;
            }
            effect.frame = lastFrame;
            if (lastFrame >= movingFrames) effect.velocity = {};
            effect.duration = effect.age + 128.f / 25.f;
            effect.setCurrentFrame(128);
            clientMissiles_.push_back(std::move(effect));
            continue;
        }
        int lastFrame = int(std::min(effect.age, effect.duration) * 25.f + .00001f);
        bool finished = false;
        auto poisonSmoke = [&](float age) {
            const int radius = program.parameters[2];
            auto offset = [&] {
                const int value = int(limitedRandom(effect.random, uint32_t(2 * radius))) - radius;
                return value + (value < 0 ? -radius : radius);
            };
            const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
            for (int i = 0; i < program.parameters[1]; ++i) {
                ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                emit(program.children[0], anchor, {float(offset()), float(offset())}, effect, age,-1,0,-1,initialization);
            }
        };
        while (effect.frame < lastFrame && !finished) {
            const int frame = effect.frame;
            const int elapsedFrames=effect.elapsedFrames();
            const float childAge = std::max(0.f, effect.age - float(frame) / 25.f);
            if(program.function==59) {
                // B1CC0: signed Q11 height, inclusive [-TargetX,TargetY].
                const int height=effect.graphics?effect.graphics->position[2]>>11:0;
                const auto limits=effect.nativeParameters.value_or(std::array<int32_t,2>{});
                if(height<nativeSubtract(0,limits[0]) || height>limits[1]) {finished=true;break;}
            }
            if(program.function==16 && (program.children[0]<0 || !relatedOwner(effect))) {finished=true;break;}
            if(program.function==30 && program.children[0]<0) {finished=true;break;}
            if(program.function==46 && program.children[0]<0) {finished=true;break;}
            if(program.function==57 && (program.children[0]<0 ||
                std::none_of(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;}))) {finished=true;break;}
            // B77A0 tests zero, unlike callbacks which reject negative ids.
            // A blank (-1) submissile still attempts creation and keeps Do1.
            if(program.function==68 && program.children[0]==0) {finished=true;break;}
            bool lightningTrail=true;
            if (program.rules==ClientMissileProgram::Rules::Pd2Cn && program.function==8) {
                const int base=program.parameters[1]<1?100:std::min(program.parameters[1],100);
                const auto chance=uint32_t(nativeAdd(base,program.parameters[2]>0?nativeMultiply(program.parameters[2],effect.nativeCurrentFrames)/4:0));
                lightningTrail=rollRandom(effect.random)%100<chance;
            }
            if (!frame) assets_.sceneAudio.playRegistered("missile-release:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
            switch (program.function) {
            case 57: {
                // B6F60 first follows the live owner (B2890), then A1340
                // reads that owner's target unit, path target, or facing fallback.
                const auto owner=std::find_if(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;});
                effect.pos=owner->position;
                auto aim=owner->targetPoint;
                if(owner->target) if(const auto target=std::find_if(anchors.begin(),anchors.end(),[&](const auto &anchor){return anchor.id==owner->target;});target!=anchors.end())
                    aim=target->position;
                if(!aim || !int(std::floor(aim->x)) || !int(std::floor(aim->y))) {
                    if(!owner->direction) break; // Unobserved facing is not a guessed native input.
                    constexpr std::array<int,8> x{0,-1,-2,-1,0,1,2,1},y{2,1,0,-1,-2,-1,0,1};
                    const size_t direction=(*owner->direction&63)>>3;
                    aim=Vec{std::floor(owner->position.x)+float(x[direction])+.5f,std::floor(owner->position.y)+float(y[direction])+.5f};
                }
                if(!effect.nativeParameters) effect.nativeParameters=std::array<int32_t,2>{};
                auto &parameters=*effect.nativeParameters;
                if(parameters[1]<parameters[0]) parameters[1]=nativeAdd(parameters[1],1);
                ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;initialization.blessedHammerPath=true;
                emit(program.children[0],effect.pos,*aim-effect.pos,effect,childAge,-1,0,-1,initialization);
                parameters[1]=0;
                break;
            }
            case 32: {
                // B2340 / D3688: descending CurrentFrame bands, not elapsed.
                constexpr std::array<std::array<int,3>,6> bands{{
                    {300,25,1},{250,10,3},{100,8,4},{75,6,4},{30,4,3},{0,100,0}}};
                size_t band=0;
                while(band+1<bands.size() && effect.nativeCurrentFrames<=bands[band][0]) ++band;
                if(effect.nativeCurrentFrames%bands[band][1]==0 && relatedOwner(effect)) {
                    ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                    initialization.parameters=std::array<int32_t,2>{bands[band][2],effect.nativeCurrentFrames};
                    emit(348,{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f},{},effect,childAge,-1,0,-1,initialization);
                }
                break;
            }
            case 68:
                // B77A0: old path changed-cell bit; flag9 copies the origin's
                // precise position and supplies CltParam1 as the child loop count.
                if(effect.movedCell) {
                    ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                    if(const auto child=assets_.clientMissilePrograms.find(program.children[0]);child!=assets_.clientMissilePrograms.end())
                        initialization.totalFrames=nativeAdd(nativeAdd(child->second.frames,nativeMultiply(effect.level,child->second.framesPerLevel)),
                            nativeMultiply(child->second.loopFrames,program.parameters[0]));
                    emit(program.children[0],effect.pos,{},effect,childAge,-1,0,-1,initialization);
                }
                break;
            case 54: {
                // B9C10: the invisible Anya controller has no table children.
                const Vec at{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
                if(elapsedFrames<110 && relatedOwner(effect)) {
                    ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                    if(!elapsedFrames) emit(532,at,{},effect,childAge,-1,0,-1,initialization);
                    const int count=elapsedFrames<50?8:elapsedFrames<80?5:1;
                    for(int index=0;index<count;++index) {
                        if(auto *child=emit(597,at+Vec{2,2},{},effect,childAge,-1,0,-1,initialization)) {
                            // Only successful allocation consumes these three rolls.
                            const int x=50-int(rollRandom(effect.random)%100),y=50-int(rollRandom(effect.random)%100);
                            const int z=int(rollRandom(effect.random)&7)+2;
                            // Every native constructor allocates a Gfx record
                            // and sets bit8 (C7B00/C7BF0), even for zero motion.
                            if(!child->graphics) {child->graphics.emplace();child->graphics->flags=8;}
                            child->graphics->velocity={nativeShiftLeft(x,11),nativeShiftLeft(y,11),0};
                            child->graphics->acceleration={0,0,nativeShiftLeft(z,11)};
                        }
                    }
                }
                if(elapsedFrames==5) shakeScreen(3,15,100,75,-childAge);
                if(elapsedFrames<200 && elapsedFrames%7==0 && relatedOwner(effect)) {
                    // B39E0 makes at most four probes. Preserve the original's
                    // asymmetric rejection (no upper comparison on Y).
                    for(int attempt=0;attempt<4;++attempt) {
                        const int x=17-int(rollRandom(effect.random)%34),y=17-int(rollRandom(effect.random)%34);
                        if(x>=-1 && x<=1 && y>=-1) continue;
                        const Vec point=at+Vec{float(x),float(y)},local=point-origin;
                        if(grid.nativeMovementMask(int(std::floor(local.x)),int(std::floor(local.y)))&4) continue;
                        ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                        emit(615,point,{},effect,childAge,-1,0,-1,initialization);
                        emit(616,point,{},effect,childAge,-1,0,-1,initialization);
                        break;
                    }
                }
                break;
            }
            case 46: {
                // B7880 caches the perpendicular to the first path point
                // during elapsed 0/1, before testing the old changed-cell bit.
                if(!effect.nativeParameters) effect.nativeParameters=std::array<int32_t,2>{};
                if(elapsedFrames<2) {
                    const auto first=effect.pathTarget; // Common getters read tTargetCoord, not the queued path front.
                    (*effect.nativeParameters)[0]=nativeSubtract(int(std::floor(effect.pos.y)),int(std::floor(first.y)));
                    (*effect.nativeParameters)[1]=nativeSubtract(int(std::floor(first.x)),int(std::floor(effect.pos.x)));
                }
                if(effect.movedCell) {
                    const auto child=assets_.clientMissilePrograms.find(program.children[0]);
                    if(child!=assets_.clientMissilePrograms.end()) {
                        const auto &row=child->second;
                        int frames=nativeAdd(row.frames,nativeMultiply(effect.level,row.framesPerLevel));
                        if(row.animation.subLoop) frames=nativeAdd(frames,nativeMultiply(nativeSubtract(row.animation.subStop,row.animation.subStart),program.parameters[0]));
                        const Vec direction{float((*effect.nativeParameters)[0]),float((*effect.nativeParameters)[1])};
                        const Vec at{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
                        ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;initialization.totalFrames=frames;
                        emit(program.children[0],at,direction,effect,childAge,-1,0,-1,initialization);
                        emit(program.children[0],at,direction*-1.f,effect,childAge,-1,0,-1,initialization);
                    }
                }
                break;
            }
            case 30: {
                // B79F0: latch exactly at CltParam1, then emit on elapsed
                // modulo CltParam2. A late 73 must not invent the missed latch.
                if(!effect.nativeParameters) effect.nativeParameters=std::array<int32_t,2>{};
                if(elapsedFrames==program.parameters[0]) (*effect.nativeParameters)[1]=1;
                if((*effect.nativeParameters)[1] && elapsedFrames%std::max(program.parameters[1],1)==0) {
                    const int radius=program.parameters[2],span=nativeAdd(nativeMultiply(radius,2),1);
                    const auto offset=[&] {return nativeSubtract(span>0?int(limitedRandom(effect.random,uint32_t(span))):0,radius);};
                    const int x=offset(),y=offset();
                    const Vec at{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
                    ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                    emit(program.children[0],at,{float(x),float(y)},effect,childAge,-1,0,-1,initialization);
                }
                break;
            }
            case 37:
                // B2500 reads CurrentFrame, not elapsed, and its sound call
                // has no unit source. Both operations run before common Do1.
                if(effect.nativeCurrentFrames==150) shakeScreen(25,0,100,0,-childAge);
                else if(effect.nativeCurrentFrames==50)
                    assets_.sceneAudio.playNativeSound(4638,uint64_t(view_.animationTime*25.f));
                break;
            case 62:
                if(relatedOwner(effect)) {
                    // BA4A0 -> B3D60 twice per callback. Every attempt draws
                    // height, cardinal offset, then variant, before allocation.
                    constexpr std::array<Vec,4> cross{Vec{1,0},Vec{0,1},Vec{-1,0},Vec{0,-1}};
                    const Vec center{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
                    for(int index=0;index<2;++index) {
                        const int height=int(rollRandom(effect.random)%125);
                        const auto offset=cross[rollRandom(effect.random)&3];
                        const auto variant=rollRandom(effect.random)%30;
                        const int id=variant<=3?608+int(variant):607;
                        ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                        initialization.parameters=std::array<int32_t,2>{int32_t(std::floor(effect.pos.x)),int32_t(std::floor(effect.pos.y))};
                        ClientMissileGraphics graphics;graphics.position[2]=nativeShiftLeft(height,11);initialization.graphics=graphics;
                        emit(id,center+offset,{offset.y,-offset.x},effect,childAge,-1,0,-1,initialization);
                    }
                    if(elapsedFrames>60 && elapsedFrames%20==0) {
                        // B3C40: both zero offsets alone consume a third roll.
                        int x=20-int(rollRandom(effect.random)%40),y=20-int(rollRandom(effect.random)%40);
                        if(!x && !y) x=int(rollRandom(effect.random)%19)+1;
                        ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                        initialization.parameters=std::array<int32_t,2>{x,y};
                        ClientMissileGraphics graphics;graphics.position[2]=-20480;initialization.graphics=graphics;
                        emit(285,center,{float(x),float(y)},effect,childAge,-1,0,-1,initialization);
                    }
                    if(elapsedFrames==25) {
                        ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                        emit(618,center+Vec{2,2},{},effect,childAge,-1,0,-1,initialization);
                        emit(617,center+Vec{2,2},{},effect,childAge,-1,0,-1,initialization);
                    }
                }
                break;
            case 16:
                // B8AA0 reads the preceding path update's changed-cell bit.
                // Three of four emitted walls suppress their own light.
                if(effect.movedCell) {
                    ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                    initialization.suppressLight=limitedRandom(effect.random,4)!=0;
                    const Vec at{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f};
                    emit(program.children[0],at,{},effect,childAge,-1,0,-1,initialization);
                }
                break;
            case 63: {
                // B1F20 rotates the first path point around TargetX/Y and
                // repaths every callback, unlike Clt60's even-frame gate.
                const auto center=effect.nativeParameters.value_or(std::array<int32_t,2>{});
                const int x=int(std::floor(effect.pos.x)),y=int(std::floor(effect.pos.y));
                effect.turnTarget={float(nativeSubtract(y,center[1])),float(nativeSubtract(center[0],x))};
                effect.pathTarget=Vec{float(x)+.5f,float(y)+.5f}+effect.turnTarget;
                effect.path.clear();
                effect.velocity=effect.turnTarget.unit()*(float(effect.velocityFixed)*25.f/4096.f);
                break;
            }
            case 38: {
                // Retail BA7E0 -> B4FB0: fourteen fixed directions, once at
                // elapsed 10. These are engine offsets, not a replacement MPQ.
                if(elapsedFrames!=10 || !forgeObject(effect) || !relatedOwner(effect)) break;
                constexpr std::array<int,14> x{-4,2,4,-3,-2,3,5,-3,-5,5,-6,-1,0,6};
                constexpr std::array<int,14> y{6,4,-4,-2,1,7,0,3,-4,-3,4,-5,5,-3};
                const auto shard=assets_.clientMissilePrograms.find(374);if(shard==assets_.clientMissilePrograms.end()) break;
                for(size_t index=0;index<x.size();++index) {
                    const int distance=(x[index]>=y[index]?std::abs(x[index])+2*std::abs(y[index]):std::abs(y[index])+2*std::abs(x[index]))>>1;
                    const int frames=nativeMultiply(shard->second.frames,distance)/6;
                    ClientMissileInitialization initialization;initialization.totalFrames=frames;initialization.sourceSkill=effect.sourceSkill;
                    initialization.parameters=std::array<int32_t,2>{(*effect.nativeParameters)[0],0};
                    auto graphics=ClientMissileGraphics::arc(frames,0);graphics.step();initialization.graphics=graphics;
                    if(auto *child=emit(374,effect.pos,{float(x[index]),float(y[index])},effect,childAge,-1,0,-1,initialization))
                        (*child->nativeParameters)[1]=int(rollRandom(effect.random)%35)+10;
                }
                shakeScreen(4,2,2,10,-childAge);
                break;
            }
            case 39:
                // B2480 freezes the precise frame before the common update.
                if(relatedOwner(effect)) effect.nativeSequenceFrame=0;
                break;
            case 40:
                if(relatedOwner(effect)) {
                    if(!effect.nativeParameters) effect.nativeParameters=std::array<int32_t,2>{};
                    auto &countdown=(*effect.nativeParameters)[1];
                    if(!countdown) {
                        if(const auto *object=forgeObject(effect)) {
                            ClientMissileInitialization initialization;initialization.sourceSkill=effect.sourceSkill;
                            initialization.parameters=std::array<int32_t,2>{int32_t(std::floor(effect.pos.x)-std::floor(object->position.x)),int32_t(std::floor(effect.pos.y)-std::floor(object->position.y))};
                            ClientMissileGraphics graphics;graphics.velocity[2]=4096;initialization.graphics=graphics;
                            emit(377,effect.pos,{},effect,childAge,-1,0,-1,initialization);
                        }
                        countdown=int(rollRandom(effect.random)%35)+10;
                    }
                    countdown=nativeSubtract(countdown,1);
                }
                break;
            case 41:
                if(elapsedFrames==8) {
                    if(effect.graphics) effect.graphics->acceleration={0,0,256};
                    effect.animationRate=int16_t(uint16_t(effect.animationRate.value_or(program.animation.rate/4)))>>1;
                } else if(elapsedFrames==35)
                    effect.animationRate=int16_t(uint16_t(nativeShiftLeft(int16_t(uint16_t(effect.animationRate.value_or(program.animation.rate/4))),1)));
                break;
            case 23:
                // Retail B2980 / Common #10737, #10970: keep Den light
                // alive by resetting the remaining counter, not its age or RNG.
                if(effect.nativeCurrentFrames<100) {
                    effect.setCurrentFrame(500);
                    effect.duration=float(effect.frame+500)/25.f;
                    lastFrame=int(std::min(effect.age,effect.duration)*25.f+.00001f);
                }
                break;
            case 10: {
                // Retail BBD40 -> B8DF0: choose the class before the period
                // gate, then initialize the parent's SEED only for emission.
                int child=program.children[0];
                if(child<0) {finished=true;break;}
                if(child<program.children[1]) child=nativeAdd(child,int(limitedRandom(effect.random,
                    uint32_t(nativeAdd(nativeSubtract(program.children[1],child),1)))));
                const int scale=effect.level/std::max(program.parameters[2],1);
                const int radius=nativeAdd(program.parameters[0],std::max(scale,2));
                const int period=std::max(nativeSubtract(program.parameters[1],scale),3);
                if(elapsedFrames%period || !effect.owner ||
                   std::none_of(targets.begin(),targets.end(),[&](const auto &unit){return unit.id==effect.owner;})) break;
                const auto definition=assets_.clientMissilePrograms.find(child);
                if(definition==assets_.clientMissilePrograms.end()) break;
                effect.random=initialRandom(uint32_t(nativeAdd(int32_t(std::floor(effect.pos.x)),elapsedFrames)));
                const int extent=nativeSubtract(radius,1),width=nativeMultiply(extent,2);
                const int x=nativeSubtract(extent,int(limitedRandom(effect.random,uint32_t(width))));
                const int y=nativeSubtract(extent,int(limitedRandom(effect.random,uint32_t(width))));
                const Vec at{std::floor(effect.pos.x)+float(x)+.5f,std::floor(effect.pos.y)+float(y)+.5f};
                if(!grid.missileSegment(at-origin,at-origin,{4,1})) break;
                const int fall=std::max(definition->second.parameters[1],1);
                const int height=std::max(definition->second.parameters[0],fall);
                ClientMissileGraphics graphics;graphics.position[2]=nativeShiftLeft(height,11);
                graphics.velocity[2]=nativeSubtract(0,nativeShiftLeft(fall,11));
                ClientMissileInitialization init;init.graphics=graphics;init.totalFrames=height/fall;
                init.sourceSkill=effect.sourceSkill;
                emit(child,at,{},effect,childAge,-1,0,-1,init);
                break;
            }
            case 12:
                // B2D00 starts a seconds-based shake only on elapsed frame 0.
                if(!elapsedFrames && program.parameters[0]>0 && program.parameters[1]>0) {
                    const int third=program.parameters[1]/3;
                    shakeScreen(program.parameters[0],nativeMultiply(third,25),
                        nativeMultiply(nativeSubtract(program.parameters[1],nativeMultiply(third,2)),25),nativeMultiply(third,25),-childAge);
                }
                break;
            case 15: {
                // B8BC0: TargetX is a signed status counter, independently of
                // CurrentFrame. The zero/negative child fades without emitting.
                const auto owner=std::find_if(targets.begin(),targets.end(),[&](const auto &unit){return unit.id==effect.owner;});
                if(owner==targets.end()) {finished=true;break;}
                if(!effect.nativeParameters) effect.nativeParameters=std::array<int32_t,2>{};
                const int status=(*effect.nativeParameters)[0];
                if(status<=nativeSubtract(0,program.parameters[1])) {finished=true;break;}
                if(status<1) (*effect.nativeParameters)[0]=nativeSubtract(status,1);
                if(effect.velocityFixed<=0) break;
                auto target=std::find_if(targets.begin(),targets.end(),[&](const auto &unit){return unit.id==effect.guidance;});
                if(target==targets.end()) target=std::find_if(targets.begin(),targets.end(),[&](const auto &unit){return unit.id==owner->target;});
                const int period=program.parameters[2]>0?program.parameters[2]:5;
                if(target!=targets.end() && status>0 && effect.nativeCurrentFrames%period==0) {
                    const int reduction=program.collision.size/2+target->size/2;
                    const int x=std::max(int(std::abs(std::floor(target->position.x)-std::floor(effect.pos.x)))-reduction,0);
                    const int y=std::max(int(std::abs(std::floor(target->position.y)-std::floor(effect.pos.y)))-reduction,0);
                    if(2*std::max(x,y)+std::min(x,y)<program.parameters[3]) {
                        const int step=std::max(program.parameters[4],1);
                        const auto sign=[](float delta){return (delta>0)-(delta<0);};
                        const Vec direction{float(sign(std::floor(target->position.x)-std::floor(effect.pos.x))*step),
                            float(sign(std::floor(target->position.y)-std::floor(effect.pos.y))*step)};
                        effect.turnTarget=direction;effect.velocity=direction.unit()*(float(effect.velocityFixed)*25.f/4096.f);
                    }
                }
                if(program.children[0]>0 && status>0 && effect.nativeCurrentFrames%std::max(program.parameters[0],1)==0) {
                    ClientMissileInitialization init;init.parameters=std::array<int32_t,2>{};
                    // Native flags5 enable the explicit zero velocity, but
                    // not 8000: its written Range field is ignored by B30F0.
                    init.sourceSkill=effect.sourceSkill;init.velocityRaw=0;
                    emit(program.children[0],{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f},{},effect,childAge,-1,0,-1,init);
                }
                break;
            }
            case 25:
                // B83D0: direction X/Y draws precede the two origin draws.
                if(program.children[0]<0) {finished=true;break;}
                if(elapsedFrames%std::max(program.parameters[2],1)==0) {
                    const int spread=program.parameters[0],radius=program.parameters[1];
                    const auto offset=[&](int extent) {return nativeSubtract(int(limitedRandom(effect.random,
                        uint32_t(nativeAdd(nativeMultiply(extent,2),1)))),extent);};
                    const int dx=offset(spread),dy=offset(spread);
                    const int x=offset(radius),y=offset(radius);
                    const Vec at{std::floor(effect.pos.x)+float(x)+.5f,std::floor(effect.pos.y)+float(y)+.5f};
                    ClientMissileInitialization init;init.sourceSkill=effect.sourceSkill;
                    emit(program.children[0],at,{float(dx),float(dy)},effect,childAge,-1,0,-1,init);
                }
                break;
            case 6: {
                // Retail B92B0 samples the previous path update before the
                // common move/collision step. Variants retain child A's Range.
                if(!effect.movedCell) break;
                const auto base=assets_.clientMissilePrograms.find(program.children[0]);
                if(base==assets_.clientMissilePrograms.end()) {finished=true;continue;}
                int child=program.children[0];
                if(program.children[1]>=0) {
                    if(program.children[2]>=0) {
                        const auto choice=limitedRandom(effect.random,3);
                        if(choice<2) child=program.children[size_t(choice)+1];
                    } else if(!limitedRandom(effect.random,2)) child=program.children[1];
                }
                ClientMissileInitialization initialization;
                if(base->second.frames) initialization.totalFrames=base->second.frames;
                initialization.suppressLight=!program.parameters[0] ||
                    limitedRandom(effect.random,uint32_t(program.parameters[0]))!=0;
                // The native callback restores the parent's precise path
                // coordinates after construction, without snapping to a cell.
                emit(child,effect.pos,{},effect,childAge,-1,0,-1,initialization);
                break;
            }
            case 70: {
                if (program.rules!=ClientMissileProgram::Rules::Pd2Cn) break;
                const auto owner=std::find_if(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;});
                if (owner==targets.end()) break;
                if (unitCoordinateDistance(int(effect.pos.x),int(effect.pos.y),int(owner->position.x),int(owner->position.y))<2) {
                    finished=true; break;
                }
                effect.velocity=(owner->position-effect.pos).unit()*effect.velocity.length();
                break;
            }
            case 71:
                if (program.rules==ClientMissileProgram::Rules::Pd2Cn && program.parameters[0]>0 &&
                    effect.nativeCurrentFrames%program.parameters[0]==0 && effect.owner &&
                    std::any_of(targets.begin(),targets.end(),[&](const auto &target){return target.id==effect.owner;}))
                    for (const auto child:program.children) emit(child,effect.pos,{},effect,childAge);
                break;
            case 56: {
                size_t count{};
                while(count<program.children.size() && program.children[count]>=0 && program.parameters[count+1]>0) ++count;
                if(!count) {finished=true;break;}
                if(effect.nativeCurrentFrames>=program.parameters[0]) {
                    effect.random=(effect.random&0xffffffff00000000ULL)|uint32_t(int32_t(std::floor(effect.pos.x)));
                    const auto index=limitedRandom(effect.random,uint32_t(count));
                    if(effect.nativeCurrentFrames%program.parameters[index+1]==0) {
                        const auto before=pending.size();
                        emit(program.children[index],effect.pos,{},effect,childAge);
                        if(pending.size()>before) {
                            auto child=std::move(pending.back());pending.pop_back();
                            impact(child,assets_.clientMissilePrograms.at(child.missileId),childAge);
                        }
                    }
                }
                break;
            }
            case 55:
                // Clt55's four parent-seeded draws precede the cloud's own
                // constructor. Its cadence uses CurrentFrame, not elapsed.
                if(effect.nativeCurrentFrames%3==0) {
                    const int targetX=int(limitedRandom(effect.random,20))-10;
                    const int targetY=int(limitedRandom(effect.random,20))-10;
                    const int x=int(std::floor(effect.pos.x))+int(rollRandom(effect.random)&1)-1;
                    const int y=int(std::floor(effect.pos.y))+int(rollRandom(effect.random)&1)-1;
                    emit(program.children[0],Vec{float(x)+.5f,float(y)+.5f},Vec{float(targetX),float(targetY)},effect,childAge);
                }
                break;
            case 17: {
                // 1.13c CltDo17 (6FB6BB40 -> 6FB66D40): the constructor
                // supplies the skill's range, not a missile Param radius.
                if(effect.streamRange<=0) {finished=true;break;}
                const int child=effect.streamMissile>0?effect.streamMissile:program.children[0];
                if(elapsedFrames<program.parameters[0] && elapsedFrames%3==0) {
                    const int radius=effect.streamRange;
                    const int stride=radius>4?2:1, chance=radius>4?radius/2:radius;
                    for(int y=-radius;y<=radius;y+=stride)
                        for(int x=-radius;x<=radius;x+=stride)
                            if(x*x+y*y<=radius*radius && limitedRandom(effect.random,uint32_t(chance))==0)
                                emit(child,effect.pos+Vec{float(x),float(y)},{},effect,childAge);
                }
                break;
            }
            case 48: {
                const int period=std::max(effect.streamPeriod,1),radius=nativeSubtract(effect.streamRange,1);
                if(effect.nativeCurrentFrames%period==0) {
                    effect.random=initialRandom(uint32_t(nativeAdd(int32_t(effect.pos.x),effect.nativeCurrentFrames)));
                    const int x=int(limitedRandom(effect.random,uint32_t(nativeMultiply(radius,2))))-radius;
                    const int y=int(limitedRandom(effect.random,uint32_t(nativeMultiply(radius,2))))-radius;
                    const auto at=effect.pos+Vec{float(x),float(y)};
                    if(grid.missileSegment(at-origin,at-origin,{0x45,1})) {
                        emit(program.children[0],at,{},effect,childAge);emit(program.children[1],at,{},effect,childAge);
                    }
                }
                break;
            }
            case 49:
                if(effect.nativeCurrentFrames%std::max(1,program.parameters[0])==0)
                    emit(program.children[0],effect.pos,{},effect,childAge);
                break;
            case 51:
                if(effect.nativeCurrentFrames==program.parameters[0]) {
                    emit(program.children[0],effect.pos,{},effect,childAge);
                    for(int index=0;index<program.parameters[2];++index) {
                        const int radius=program.parameters[3];
                        const int x=int(limitedRandom(effect.random,uint32_t(2*radius+1)))-radius;
                        const int y=int(limitedRandom(effect.random,uint32_t(2*radius+1)))-radius;
                        emit(program.children[0],effect.pos+Vec{float(x),float(y)},{},effect,childAge);
                    }
                }
                if(effect.nativeCurrentFrames==program.parameters[1]) emit(program.children[1],effect.pos,{},effect,childAge);
                break;
            case 50: {
                // 1.13c CltDo50 (6FB6B2A0): native countdown gates and
                // TargetX-seeded x/y draws, followed by the next TargetX roll.
                const int radius=std::max(1,program.parameters[1]>0?program.parameters[1]:effect.streamRange);
                const int period=std::max(1,program.parameters[0]>0?program.parameters[0]:effect.streamPeriod);
                if(effect.nativeCurrentFrames>program.parameters[2] && effect.nativeCurrentFrames<program.parameters[3] && effect.nativeCurrentFrames%period==0) {
                    effect.random=initialRandom(uint32_t(effect.nativeParameters?(*effect.nativeParameters)[0]:0));
                    const int x=int(limitedRandom(effect.random,uint32_t(nativeAdd(nativeMultiply(radius,2),1))))-radius;
                    const int y=int(limitedRandom(effect.random,uint32_t(nativeAdd(nativeMultiply(radius,2),1))))-radius;
                    effect.nativeParameters=std::array<int32_t,2>{nativeNarrow(rollRandom(effect.random)),0};
                    const size_t before=clientMissiles_.size();ClientMissileInitialization init;
                    init.distanceLimited=true;init.ballisticHeight=program.parameters[4]?program.parameters[4]:30;
                    if(launchClientMissile(program.children[0],effect.pos,effect.pos+Vec{float(x),float(y)},effect.level,0,{},-1,effect.owner,effect.hostile,0,ClientMissileSource::Program,effect.slowPercent,{},0,init) && clientMissiles_.size()>before) {
                        auto child=std::move(clientMissiles_.back());clientMissiles_.resize(before);child.age=childAge;pending.push_back(std::move(child));
                    }
                }
                break;
            }
            case 60:
                // Retail CltDo60 / RVA B1C00: retarget tangentially to the
                // fixed birth center on even native countdowns.
                if(effect.nativeCurrentFrames%2==0 && effect.nativeParameters) {
                    const int x=int(effect.pos.x)-(*effect.nativeParameters)[0],y=int(effect.pos.y)-(*effect.nativeParameters)[1];
                    effect.turnTarget={float(y),float(-x)};
                    effect.velocity=effect.turnTarget.unit()*(float(effect.velocityFixed)*25.f/4096.f);
                }
                break;
            case 58:
                if(elapsedFrames%std::max(1,program.serverTurnPeriod)==0) {
                    if(effect.nativeParameters) effect.random=initialRandom(uint32_t((*effect.nativeParameters)[0]));
                    const int packed=effect.nativeParameters?(*effect.nativeParameters)[1]:0;
                    const int x=effect.nativeParameters?int16_t(packed):int(effect.turnTarget.x),y=effect.nativeParameters?int16_t(uint32_t(packed)>>16):int(effect.turnTarget.y);
                    const bool clockwise=(rollRandom(effect.random)&1)!=0;
                    int nextX=(4*x+(clockwise?-y:y))/4,nextY=(4*y+(clockwise?x:-x))/4;
                    if(!nextX) nextX=1;
                    if(!nextY) nextY=1;
                    effect.turnTarget={float(nextX),float(nextY)};effect.velocity=effect.turnTarget.unit()*(float(effect.velocityFixed)*25.f/4096.f);
                    effect.nativeParameters=std::array<int32_t,2>{nativeNarrow(uint32_t(effect.random)),nativeNarrow(uint32_t(uint16_t(nextX))|(uint32_t(uint16_t(nextY))<<16))};
                }
                break;
            case 5:
                effect.nativeSequenceFrame=firewallMissileSequence(effect.nativeSequenceFrame,effect.nativeCurrentFrames,
                    program.animation.subStart,program.animation.subStop,effect.random);
                break;
            case 21:
                // Retail B86F0 / B0050: initial 50 ticks, then an elapsed-frame interval.
                if(effect.emissionCountdown<=0) {
                    const int radius=std::max(0,program.parameters[0]);
                    auto offset=[&]{return int(limitedRandom(effect.random,uint32_t(radius*2+1)))-radius;};
                    Vec heading{float(offset()),float(offset())};
                    if(heading.length()==0) heading.x=1;
                    emit(program.children[0],effect.pos,heading,effect,childAge);
                    effect.emissionCountdown=elapsedFrames<125?25:elapsedFrames<175?10:elapsedFrames<300?3:15;
                }
                --effect.emissionCountdown;
                break;
            case 22:
                // Retail B8550: emit on cell entry, then rotate the integer ray.
                if(elapsedFrames%std::max(1,program.parameters[0])==0) {
                    const int x=int(effect.turnTarget.x),y=int(effect.turnTarget.y);
                    const bool clockwise=(rollRandom(effect.random)&1)!=0;
                    effect.turnTarget={float((clockwise?x-y:x+y)*3/4),float((clockwise?x+y:y-x)*3/4)};
                    if(effect.turnTarget.length()>0) effect.velocity=effect.turnTarget.unit()*effect.velocity.length();
                }
                break;
            case 27:
                // Retail B8130: the ground flame is emitted on an entered cell.
                if(elapsedFrames%std::max(1,program.parameters[0])==0) {
                    const int x=int(effect.turnTarget.x),y=int(effect.turnTarget.y);
                    switch(rollRandom(effect.random)&3) {
                    case 0:effect.turnTarget={float((x-y)*3/4),float((x+y)*3/4)};break;
                    case 2:effect.turnTarget={float((x+y)*3/4),float((y-x)*3/4)};break;
                    default:break;
                    }
                    if(effect.turnTarget.length()>0) effect.velocity=effect.turnTarget.unit()*effect.velocity.length();
                }
                break;
            case 29: {
                // Retail BC210 / B0000 / BB5B0: fire grows and recedes with elapsed frames.
                int amount=1;
                if(elapsedFrames>=30 && elapsedFrames<85) amount=(elapsedFrames-15)/4;
                else if(elapsedFrames>=85 && elapsedFrames<program.frames-50) amount=12;
                else if(elapsedFrames>=program.frames-50 && elapsedFrames<program.frames) amount=12-(elapsedFrames-(program.frames-50))/4;
                if(elapsedFrames>10 && program.children[0]>=0) {
                    const auto &base=assets_.clientMissilePrograms.at(program.children[0]);
                    const int count=std::min(8,std::max(0,amount*7)),span=std::max(1,amount*7);
                    for(int i=0;i<count;++i) {
                        const auto before=pending.size();
                        emit(base.children[0],effect.pos,{},effect,childAge);
                        if(pending.size()>before) {
                            auto &child=pending.back();
                            child.pixelOffset={float(int(limitedRandom(effect.random,uint32_t(span)))-span/2),
                                float(int(limitedRandom(effect.random,uint32_t(span)))-span/2)};
                        }
                    }
                    if(elapsedFrames%2==0 && (amount>=3 || elapsedFrames%16==0)) {
                        const int spread=amount<3?1:amount<6?2:amount<9?3:4;
                        Vec at=effect.pos;
                        if(spread>1) at=at+Vec{float(int(limitedRandom(effect.random,uint32_t(spread)))-spread/2),
                            float(int(limitedRandom(effect.random,uint32_t(spread)))-spread/2)};
                        emit(program.children[0],at,{},effect,childAge);
                    }
                }
                auto wandering=[&](int child,int radius) {
                    if(child<0) return;
                    auto offset=[&]{return int(limitedRandom(effect.random,uint32_t(radius*2+1)))-radius;};
                    Vec direction{float(offset()),float(offset())}; if(direction.length()==0) direction.x=1;
                    emit(child,effect.pos,direction,effect,childAge);
                };
                if(elapsedFrames%std::max(1,program.parameters[0])==0) wandering(program.children[1],20);
                if(elapsedFrames>115 && elapsedFrames<315 &&
                   ((elapsedFrames>200 && elapsedFrames<225) || elapsedFrames%4==0)) {
                    wandering(program.children[2],20);
                    if(elapsedFrames>200 && elapsedFrames<225) wandering(program.children[2],6);
                }
                break;
            }
            case 4: case 45:
                if ((!program.childServerSent || program.function==45) &&
                    (!program.parameters[0] || limitedRandom(effect.random, uint32_t(program.parameters[0])) == 0)) {
                // Retail CltDo04 / RVA BBE00 -> B9470: chance per native tick,
                // MPQ count and radius; smoke starts at the parent and drifts
                // toward an integer offset. No poison or damage is applied here.
                    poisonSmoke(childAge);
                }
                break;
            // Retail CltDo03 RVA BBE60 creates CltSubMissile1 at the parent
            // each native tick (Poison Javelin's verified CltCalc1 is zero).
            case 3:
                if(program.children[0]>=0) emit(program.children[0],effect.pos,{},effect,childAge);
                break;
            case 13: {
                if (!effect.areaEmission) break;
                const auto &b=*effect.areaEmission;
                // Retail RVA BBC10 / B8DF0: variant is chosen before the
                // remaining-frame gate; placement reseeds from global X.
                const int first=program.children[0],last=program.children[1];
                const int variant=first+int(limitedRandom(effect.random,uint32_t(std::max(1,last-first+1))));
                if(missileEmissionDue(elapsedFrames,b.period)) {
                    const Vec at=effect.pos+blizzardOffset(uint32_t(std::floor(effect.pos.x)),elapsedFrames,b.radius,true,effect.random);
                    if(grid.missileSegment(at-origin,at-origin,{4,1})) emit(variant,at,{},effect,childAge);
                }
                break;
            }
            case 19:
                if (const auto emission = missileRingEmission(elapsedFrames, program.parameters[0], effect.directionIndex, program.parameters[1])) {
                    const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
                    emit(program.children[0], anchor, emission->direction, effect, childAge);
                    effect.directionIndex = emission->nextIndex;
                }
                break;
            case 20:
                if (const auto turn=missileOrbTurn(effect.turnTarget,elapsedFrames,program.parameters[0],program.parameters[1])) {
                    effect.turnTarget = *turn;
                    effect.velocity = effect.turnTarget.unit() * (float(effect.velocityFixed) * 25.f / 4096.f);
                }
                break;
            case 7: {
                if (!effect.guidance) break;
                const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.id==effect.guidance;});
                if(target!=targets.end()) if(const auto heading=missileGuidedDirection(effect.pos,target->position,elapsedFrames,std::max(1,program.parameters[0]))) effect.velocity=*heading*effect.velocity.length();
                break;
            }
            default: break;
            }
            // PathMisc::sub_6FD5CEB0 adjusts native fixed velocity every five ticks.
            if (const auto step=advanceMissileVelocity(effect.velocityFixed,effect.acceleration,program.maximumVelocity,frame+1)) {
                effect.velocityFixed=step->speed; effect.acceleration=step->acceleration;
                effect.velocity = effect.velocity.unit() * (float(effect.velocityFixed) * 25.f / 4096.f);
            }
            Vec next = effect.pos;
            float travel = effect.path.empty() ? effect.velocity.length() / 25.f : float(effect.velocityFixed) / 4096.f;
            if (!effect.path.empty()) {
                while (!effect.path.empty() && travel > 0) {
                    const auto delta = effect.path.front() - next;
                    if (delta.length() <= travel) { next = effect.path.front(); travel -= delta.length(); effect.path.pop_front(); }
                    else { next = next + delta.unit() * travel; travel = 0; }
                }
                effect.velocity = (next - effect.pos) * 25.f;
            } else next = next + effect.velocity * (1.f / 25.f);
            float fraction = 1.f;
            bool wall = false;
            if(effect.hiddenUntilCurrentFrame && effect.nativeCurrentFrames<=*effect.hiddenUntilCurrentFrame) effect.hiddenUntilCurrentFrame.reset();
            const bool activated = !effect.activateAtCurrentFrame || effect.nativeCurrentFrames <= *effect.activateAtCurrentFrame;
            // Retail Client B1862 clears initial invisibility strictly after
            // InitSteps elapsed callbacks, and only once the missile is active.
            if(activated && effect.elapsedFrames()>assets_.projectileVisuals.at(effect.missileId).initSteps)
                effect.initialHidden=false;
            if (activated && (program.collision.mask & 0x0005)) {
                if (const auto contact = missileTerrainContact(grid, effect.pos, next, program.collision, origin)) {
                    fraction = *contact; wall = true;
                }
            }
            const Vec start = effect.pos;
            if(program.function==52 && !wall && missileChangedCell(start,next)) {
                const Vec direction=effect.nativeParameters?Vec{float((*effect.nativeParameters)[0]),float((*effect.nativeParameters)[1])}:Vec{-effect.turnTarget.y,effect.turnTarget.x};
                const Vec position{float(int(next.x))+.5f,float(int(next.y))+.5f};
                emit(program.children[0],position,direction,effect,childAge);
                emit(program.children[0],position,direction*-1.f,effect,childAge);
            }
            if(program.function==47 && !wall && missileChangedCell(start,next)) {
                int child=program.children[0];
                if(program.children[1]>=0 && program.children[2]>=0) {
                    const int choice=int(limitedRandom(effect.random,3));if(choice<2) child=program.children[size_t(choice+1)];
                } else if(program.children[1]>=0 && !limitedRandom(effect.random,2)) child=program.children[1];
                if(program.parameters[0]) limitedRandom(effect.random,uint32_t(program.parameters[0]));
                const auto definition=assets_.clientMissilePrograms.find(child);
                if(definition!=assets_.clientMissilePrograms.end()) emit(child,{float(int(next.x))+.5f,float(int(next.y))+.5f},{},effect,childAge,-1,0,assets_.clientMissilePrograms.at(program.children[0]).frames);
            }
            if ((program.function==22 || program.function==27) && !wall && missileChangedCell(start,next))
                emit(program.children[0],{float(int(next.x))+.5f,float(int(next.y))+.5f},{},effect,childAge);
            std::vector<std::pair<float, const ClientMissileTarget *>> contacts;
            if (activated && program.collide && program.function != 19)
                for (const auto &target : targets) {
                    if ((program.holyBolt && !target.undead) || (program.function==7 && ((!effect.guidance && !effect.guidanceSearched) || (effect.guidance && target.id!=effect.guidance))) || target.id == effect.owner || target.hostile == effect.hostile ||
                        std::find(effect.contacts.begin(), effect.contacts.end(), target.id) != effect.contacts.end()) continue;
                    if (const auto at = missileUnitIntersection(start, next, program.collision.size, target.position, target.size);
                        at && *at <= fraction) contacts.emplace_back(*at, &target);
                }
            std::sort(contacts.begin(), contacts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[at, target] : contacts) {
                effect.pos = start + (next - start) * at;
                const bool hitStops=impact(effect, program, std::max(0.f, childAge - 1.f / 25.f),true,target->monster && target->large,target->position);
                if(program.returnFire && target->retaliation>=0) {
                    const auto source=std::find_if(targets.begin(),targets.end(),[&](const auto &candidate){return candidate.id==effect.owner;});
                    if(source!=targets.end()) {
                        auto retaliation=effect;retaliation.owner=target->id;retaliation.hostile=target->hostile;retaliation.level=target->retaliationRank;
                        emit(target->retaliation,target->position,source->position-target->position,retaliation,std::max(0.f,childAge-1.f/25.f));
                    }
                }
                effect.contacts.push_back(target->id);
                if(program.hitFunction==16 && effect.chainRadius>0 && effect.remainingHits>1) {
                    std::vector<uint64_t> eligible;
                    for(const auto &candidate:targets) {
                        const Vec d{std::floor(candidate.position.x)-std::floor(effect.pos.x),std::floor(candidate.position.y)-std::floor(effect.pos.y)};
                        if(candidate.id==effect.owner || candidate.hostile==effect.hostile || candidate.id==target->id ||
                            d.x*d.x+d.y*d.y>float(effect.chainRadius*effect.chainRadius) ||
                            !grid.missileSegment(effect.pos-origin,candidate.position-origin,{4,1})) continue;
                        eligible.push_back(candidate.id.value);
                    }
                    const EntityId nextId{missileChainSuccessor(target->id.value,eligible)};
                    const auto nextTarget=std::find_if(targets.begin(),targets.end(),[&](const auto &t) {return t.id==nextId;});
                    if(nextTarget!=targets.end()) {
                        const size_t queued=pending.size();
                        ClientMissileInitialization init;init.chainHits=effect.remainingHits-1;init.chainRadius=effect.chainRadius;
                        emit(effect.missileId,{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f},nextTarget->position-effect.pos,effect,std::max(0.f,childAge-1.f/25.f),-1,0,-1,init);
                        if(pending.size()>queued) {pending.back().remainingHits=effect.remainingHits-1;pending.back().contacts.push_back(target->id);}
                    }
                }
                if (hitStops && program.killOnContact && effect.pierce-- <= 0) {
                    fraction = at; finished = true; wall = false; break;
                }
            }
            if ((program.function == 8 && lightningTrail) || program.function == 18) {
                const int loops = std::clamp(program.parameters[0], 1, 32);
                for (int i = 0; i < loops; ++i)
                    emit(program.children[0], start + (next - start) * (fraction * float(i) / loops), {}, effect,
                        std::max(0.f, childAge - float(i) / (25.f * loops)));
            }
            effect.pos = start + (next - start) * fraction;
            effect.movedCell=missileChangedCell(start,effect.pos);
            effect.pixelOffset=effect.pixelOffset+effect.pixelVelocity;
            effect.pixelVelocity=effect.pixelVelocity+effect.pixelAcceleration;
            if(effect.graphics) {
                if(effect.graphics->step()) assets_.sceneAudio.playRegistered("missile-progress:"+std::to_string(effect.missileId),uint64_t(view_.animationTime*25.f));
                effect.pixelOffset=effect.graphics->offset();
            }
            ++effect.frame;
            if(effect.animationRate) {
                auto animation=program.animation;animation.rate=nativeMultiply(*effect.animationRate,4);
                effect.nativeSequenceFrame=advanceMissileAnimation(effect.nativeSequenceFrame,effect.nativeCurrentFrames,animation);
            } else effect.nativeSequenceFrame=advanceMissileAnimation(effect.nativeSequenceFrame,effect.nativeCurrentFrames,program.animation);
            effect.setCurrentFrame(nativeSubtract(effect.nativeCurrentFrames,1));
            // Retail D2Client B18F7-B1959 caps CurrentFrame after its normal
            // decrement, using the missile owner's room and CltSrcTown.
            if (ownerInTown && program.sourceTownFrames > 0 && effect.nativeCurrentFrames >= program.sourceTownFrames) {
                effect.setCurrentFrame(program.sourceTownFrames);
                const int end = effect.frame + program.sourceTownFrames;
                effect.duration = std::min(effect.duration,float(end) / 25.f);
                lastFrame = std::min(lastFrame,end);
                const int animationFrame = program.animation.frames - program.sourceTownFrames;
                if (!program.animation.loop && animationFrame > 0)
                    effect.nativeSequenceFrame = nativeShiftLeft(animationFrame,8);
            }
            if (program.rules==ClientMissileProgram::Rules::Pd2Cn && program.function==69 &&
                effect.nativeCurrentFrames==effect.nativeTotalFrames-1 && effect.owner)
                emit(program.children[0],effect.pos,{},effect,childAge);
            if (wall) {
                impact(effect, program, std::max(0.f, childAge - 1.f / 25.f));
                finished = true;
            }
        }
        if(!finished && program.function==7 && !effect.guidance && !effect.guidanceSearched && effect.age+.00001f>=effect.duration) {
            effect.guidanceSearched=true;
            for(const auto &target:targets) if(target.id!=effect.owner && target.hostile!=effect.hostile &&
                missileDistance(effect.pos,target.position)<=program.guidedRadius && grid.missileSegment(effect.pos-origin,target.position-origin,{4,1}) && (!effect.guidance || target.id<effect.guidance)) effect.guidance=target.id;
            const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.id==effect.guidance;});
            effect.velocity=(target!=targets.end()?target->position-effect.pos:effect.turnTarget).unit()*effect.velocity.length();
            effect.setCurrentFrame(nativeAdd(program.frames,nativeMultiply(effect.level-1,program.framesPerLevel)));
            effect.duration=float(effect.frame+effect.nativeCurrentFrames)/25.f;
            if(effect.frame<int(std::min(effect.age,effect.duration)*25.f+.00001f)) {
                // A presentation update may cross both guided phases. Keep
                // its chronological remainder and process the renewed clock.
                pending.push_front(std::move(effect));continue;
            }
        }
        if (!finished && effect.age + .00001f >= effect.duration) {
            // CltDo04 bypasses its chance gate when native CurrentFrame is zero.
            if ((program.function == 4 && !program.childServerSent) || program.function==45)
                poisonSmoke(std::max(0.f, effect.age - effect.duration));
            if (program.function == 19 && program.hitParameters[0] > 0) {
                const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
                for (const auto direction : missileRingBurst(program.hitParameters[0]))
                    emit(program.hitChildren[0], anchor, direction, effect,
                        std::max(0.f, effect.age - effect.duration));
            } else if (program.explodeOnExpiry || (program.rules==ClientMissileProgram::Rules::Original && program.hitFunction==37) || program.hitFunction==52 || program.hitFunction==53 || program.hitFunction==56 || program.hitFunction==54 || program.hitFunction==55 || program.hitFunction==25 || program.hitFunction==26 || (program.function==9 && program.hitFunction==18))
                impact(effect, program, std::max(0.f, effect.age - effect.duration));
            finished = true;
        }
        if (!finished) clientMissiles_.push_back(std::move(effect));
    }
    // The work budget bounds one presentation update, not missile lifetime.
    // Retain unvisited parents and descendants with their elapsed age.
    while(!pending.empty()) {
        clientMissiles_.push_back(std::move(pending.front()));pending.pop_front();
    }
    {
        std::vector<SoundEmitter> sounds;
        for (const auto &effect : clientMissiles_) {
            if (effect.age < 0 || !effect.soundEmitter) continue;
            const auto key = "missile-release:" + std::to_string(effect.missileId);
            if (assets_.audio.hasEmitterSound(key)) sounds.push_back({effect.soundEmitter, key});
        }
        assets_.audio.syncEmitters(sounds, uint64_t(view_.animationTime * 25.f));
    }
}
} // namespace d2x
