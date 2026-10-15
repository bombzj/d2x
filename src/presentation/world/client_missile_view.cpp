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
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>

namespace d2x {
namespace {
bool matchesCreation(const ClientMissileVisual &effect, int id, EntityId owner, Vec position, Vec direction,
                     uint8_t source, float age, float window) {
    return effect.creationSources && !(effect.creationSources & source) && effect.missileId == id &&
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
    const auto *capabilities = findClientMissileCapabilities(program.function);
    if (!capabilities || (program.function == 2 && !program.localBlood)) return false;
    if (program.function==9)
        for (const int child:program.children) if (child>=0 && !assets_.ensureProjectile(child)) return false;
    if(program.function==21 || program.function==22 || program.function==29)
        for(const int child:program.children) if(child>=0 && !assets_.ensureProjectile(child)) return false;
    if (program.function == 4 && (program.children[0] < 0 || program.parameters[0] < 0 ||
        program.parameters[1] < 0 || program.parameters[1] > 256 || program.parameters[2] < 0 ||
        program.parameters[2] > 32767 || !assets_.ensureProjectile(program.children[0]))) return false;
    if (program.function==9 && program.hitFunction==18)
        for (const int child:program.hitChildren) if (child<0 || !assets_.ensureProjectile(child)) return false;
    // Lightning's parent is deliberately invisible; its MPQ child is the art.
    if (!assets_.ensureProjectile(id) &&
        !(capabilities->allowChildArtwork && program.children[0] >= 0 &&
          assets_.ensureProjectile(program.children[0]))) return false;
    level = std::max(0, level);
    const auto nativeVelocity = missileVelocityFixed(program.velocity+velocityBonus,program.velocityPerLevel,level,program.canSlow?slowPercent:0);
    if (!nativeVelocity ||
        (program.acceleration && program.maximumVelocity <= 0)) return false;
    const int velocity = capabilities->stationary ? 0 : program.function==4 && program.poisonVelocity[0]>0?poisonCloudVelocity(program.poisonVelocity[0]):*nativeVelocity;
    const int tableFrames=nativeMissileFrameCount(nativeAdd(program.frames,nativeMultiply(level,program.framesPerLevel)));
    const float tableDuration = float(tableFrames) / 25.f;
    const float fullDuration = tableDuration > 0 ? tableDuration : assets_.projectileVisuals.at(id).lifetime;
    const float duration = remaining.value_or((program.groundThrow || initialization.distanceLimited)?float(groundThrowFrames(start,target,velocity))/25.f:program.function==7 && !guidedTarget?std::max(1.f/25.f,(target-start).length()/std::max(.001f,float(velocity)*25.f/4096.f)):fullDuration);
    if (duration <= 0 || !std::isfinite(duration) || !std::isfinite(delay) ||
        !std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(target.x) || !std::isfinite(target.y)) return false;
    // Stationary 73 sends FirstX/FirstY = 0; these are not an aimed ray.
    if (capabilities->stationary) target = start;
    const auto sources = uint8_t(source);
    const bool reconstructed = (capabilities->pairCreationSources || program.clientSend) && source!=ClientMissileSource::Death;
    if (sources && reconstructed) {
        // 73 has no missile GUID. Pair the two creation sources for the same
        // owner/class/origin/ray within the cast release and event freshness window.
        // Never merge two predictions or two authoritative synchronizations.
        const float expectedAge = -delay + (remaining ? std::max(0.f, fullDuration - duration) : 0.f);
        for (auto it = clientMissiles_.rbegin(); it != clientMissiles_.rend(); ++it) {
            if (!matchesCreation(*it,id,owner,start,target-start,sources,expectedAge,.25f+std::max(0.f,delay))) continue;
            it->creationSources |= sources;
            if(initialization.parameters) it->nativeParameters=initialization.parameters;
            if(initialization.streamRange) it->streamRange=*initialization.streamRange;
            if(initialization.streamPeriod) it->streamPeriod=*initialization.streamPeriod;
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
    effect.deathProgram=source==ClientMissileSource::Death;
    effect.slowPercent=slowPercent;effect.guidance=guidedTarget;
    effect.creationSources = sources;
    effect.creationPosition = start; effect.creationDirection = (target - start).unit();
    if(program.chain) effect.remainingHits=std::max(1,skillRankBonus({program.chain->count,program.chain->countPerLevel},level)/program.chainCountDivisor);
    effect.velocityFixed = velocity;
    effect.acceleration = program.acceleration; effect.owner = owner; effect.hostile = hostile; effect.pierce = pierce;
    effect.soundEmitter = {(uint64_t{1} << 63) | ++nextClientMissile_};
    effect.random = initialRandom(uint32_t(nextClientMissile_));
    // Retail packet 73 (D2Client RVA AEEE0) constructs the full table range,
    // then calls SetCurrentFrame(packet.frame), leaving TotalFrames untouched.
    // The packet never supplies an animation frame or a callback-age prefix.
    effect.nativeTotalFrames=tableFrames;
    effect.setCurrentFrame(int(std::lround(duration*25.f)));
    if(source==ClientMissileSource::Synchronization) {
        const auto &visual=assets_.projectileVisuals.at(id);
        const int animationFrame=int(limitedRandom(effect.random,uint32_t(visual.frames)));
        effect.nativeSequenceFrame=nativeShiftLeft(animationFrame,8);
    }
    effect.turnTarget = target - start;
    effect.nativeParameters=initialization.parameters;effect.animationRate=initialization.animationRate;
    if(initialization.pixelHeight) effect.pixelOffset.y=-float(*initialization.pixelHeight);
    effect.graphics=initialization.graphics;
    if(initialization.ballisticHeight) effect.graphics=ClientMissileGraphics::arc(effect.nativeCurrentFrames,*initialization.ballisticHeight);
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
    if(program.blessedHammer) {
        const auto path=blessedHammerPath(start);effect.path.assign(path.begin(),path.end());
    }
    if(initialization.path!=ClientMissileInitialization::Path::None) {
        int frames=initialization.path==ClientMissileInitialization::Path::Charged?int(uint8_t(effect.nativeTotalFrames)):effect.nativeTotalFrames;
        if(frames>=78) {frames=77;effect.nativeTotalFrames=77;effect.setCurrentFrame(77);effect.duration=77.f/25.f;}
        const auto path=initialization.path==ClientMissileInitialization::Path::Charged?chargedBoltPath(start,target,frames,effect.random):assassinProgressiveSpiral(start,target,frames,effect.random);
        effect.path.assign(path.begin(),path.end());
    }
    if(const auto fall=assets_.blizzardFalls.find(id);fall!=assets_.blizzardFalls.end()) {
        effect.flight=false;effect.duration=float((fall->second.fallDistance+fall->second.fallRate-1)/fall->second.fallRate)/25.f;
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
    if ((collision.mask & 0x0005) && !grid.missileSegment(effect.pos - origin, next - origin, collision)) {
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
void SceneView::advanceClientMissiles(float dt, const Grid &grid, Vec origin,
                                    std::span<const ClientMissileTarget> targets) {
    if (dt < 0 || !std::isfinite(dt)) return;
    // Move the batch aside: client programs may enqueue children while updating.
    auto pending = std::deque<ClientMissile>(clientMissiles_.begin(), clientMissiles_.end());
    clientMissiles_.clear();
    for (auto &effect : pending) effect.age += dt;
    size_t processed = 0;
    auto emit = [&](int id, Vec position, Vec direction, const ClientMissile &parent, float age,int poisonVelocity=-1,int loops=0,int frames=-1) {
        const auto source = parent.deathProgram ? ClientMissileSource::Death : parent.creationSources ? ClientMissileSource::Cast : ClientMissileSource::Program;
        if (source == ClientMissileSource::Cast)
            for (auto &effect : pending)
                if (matchesCreation(effect,id,parent.owner,position,direction,uint8_t(source),age,.25f)) {
                    effect.creationSources |= uint8_t(source); return;
                }
        const size_t before = clientMissiles_.size();
        if (id < 0 || !launchClientMissile(id, position, position + direction, parent.level,
                0, {}, -1, parent.owner, parent.hostile, 0, source,parent.slowPercent)) return;
        // A synchronized counterpart may have consumed this creation already.
        if (clientMissiles_.size() == before) return;
        auto child = std::move(clientMissiles_.back()); clientMissiles_.resize(before);
        if(poisonVelocity>=0) {child.velocityFixed=poisonCloudVelocity(poisonVelocity);child.velocity=direction.unit()*(float(child.velocityFixed)*25.f/4096.f);child.duration+=float(loops*assets_.clientMissilePrograms.at(id).loopFrames)/25.f;}
        if(frames>0) child.duration=float(frames)/25.f;
        if(poisonVelocity>=0 || frames>0) {
            child.nativeTotalFrames=nativeMissileFrameCount(int(std::lround(child.duration*25.f)));
            child.setCurrentFrame(child.nativeTotalFrames);
        }
        child.age = std::max(0.f, age); pending.push_back(std::move(child));
    };
    auto impact = [&](ClientMissile &effect, const ClientMissileProgram &program, float overshoot,bool unitContact=false,bool largeMonster=false) {
        if(program.hitFunction==52 && unitContact && !largeMonster) return false;
        if(program.hitFunction==53) {
            const size_t before=clientMissiles_.size();ClientMissileInitialization init;
            init.bounce=std::array{program.hitParameters[0],program.hitParameters[1],program.hitParameters[2]};
            if(launchClientMissile(program.hitChildren[0],effect.pos,effect.pos+effect.turnTarget,effect.level,0,{},-1,effect.owner,effect.hostile,0,ClientMissileSource::Program,effect.slowPercent,{},0,init) && clientMissiles_.size()>before) {
                auto child=std::move(clientMissiles_.back());clientMissiles_.resize(before);child.age=overshoot;pending.push_back(std::move(child));
            }
        }
        if(program.hitFunction==56) emit(program.hitChildren[0],effect.pos,effect.turnTarget,effect,overshoot);
        if(program.hitFunction==52) {
            emit(program.hitChildren[0],effect.pos,{},effect,overshoot);
            const int radius=nativeMultiply(program.hitParameters[0],2),span=nativeAdd(nativeMultiply(radius,2),1);
            for(int index=0;index<radius;++index) {
                const int x=int(limitedRandom(effect.random,uint32_t(span)))-radius;
                const int y=int(limitedRandom(effect.random,uint32_t(span)))-radius;
                const auto at=effect.pos+Vec{float(x),float(y)};
                if(unitCoordinateDistance(int(effect.pos.x),int(effect.pos.y),int(at.x),int(at.y))<=3) continue;
                const size_t before=clientMissiles_.size();ClientMissileInitialization init;
                init.distanceLimited=true;init.ballisticHeight=program.hitParameters[1];
                if(launchClientMissile(program.hitChildren[1],effect.pos,at,effect.level,0,{},-1,effect.owner,effect.hostile,0,ClientMissileSource::Program,effect.slowPercent,{},0,init) && clientMissiles_.size()>before) {
                    auto child=std::move(clientMissiles_.back());clientMissiles_.resize(before);child.age=overshoot;pending.push_back(std::move(child));
                }
            }
            constexpr std::array<Vec,18> fire{Vec{2,-2},Vec{-2,-2},Vec{0,2},Vec{0,5},Vec{-3,3},Vec{0,3},Vec{3,3},Vec{-1,2},Vec{1,1},Vec{-1,-1},Vec{2,-1},Vec{-4,-2},Vec{-3,-2},Vec{-1,-3},Vec{0,-4},Vec{1,-3},Vec{3,-3},Vec{4,-2}};
            for(const auto offset:fire) emit(program.hitChildren[2],effect.pos+offset,{},effect,overshoot);
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
            // Retail BB070/B9740: Clt duration uses Range + (rank-1)*LevRange;
            // cHitPar2 is the creation chance, not the server's SHitCalc1 duration.
            for(const auto offset:missileDiskOffsets(radius)) if(grid.missileSegment(center+offset-origin,center+offset*2.f-origin,{4,1}) &&
                int(limitedRandom(effect.random,100))<program.hitParameters[1])
                emit(program.hitChildren[0],center+offset,{},effect,overshoot,-1,0,program.frames+(effect.level-1)*program.framesPerLevel);
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
            child.age = std::max(0.f, overshoot); pending.push_back(std::move(child));
        }
        clientMissiles_.resize(first);
        assets_.sceneAudio.playRegistered("missile-hit:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
        return true;
    };
    while (!pending.empty() && processed++ < 8192) {
        auto effect = std::move(pending.front()); pending.pop_front();
        if (!effect.flight) {
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
        const int lastFrame = int(std::min(effect.age, effect.duration) * 25.f + .00001f);
        bool finished = false;
        auto poisonSmoke = [&](float age) {
            const int radius = program.parameters[2];
            auto offset = [&] {
                const int value = int(limitedRandom(effect.random, uint32_t(2 * radius))) - radius;
                return value + (value < 0 ? -radius : radius);
            };
            const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
            for (int i = 0; i < program.parameters[1]; ++i)
                emit(program.children[0], anchor, {float(offset()), float(offset())}, effect, age);
        };
        while (effect.frame < lastFrame && !finished) {
            const int frame = effect.frame;
            const int elapsedFrames=effect.elapsedFrames();
            const float childAge = std::max(0.f, effect.age - float(frame) / 25.f);
            if (!frame) assets_.sceneAudio.playRegistered("missile-release:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
            switch (program.function) {
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
                    if(!nextX) nextX=1;if(!nextY) nextY=1;
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
            case 4:
                if (!program.childServerSent &&
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
                if (!program.blizzard) break;
                const auto &b=*program.blizzard;
                // Retail RVA BBC10 / B8DF0: variant is chosen before the
                // remaining-frame gate; placement reseeds from global X.
                const int first=program.children[0],last=program.children[1];
                const int variant=first+int(limitedRandom(effect.random,uint32_t(std::max(1,last-first+1))));
                if(missileEmissionDue(elapsedFrames,b.emissionPeriod)) {
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
            if (program.collision.mask & 0x0005) {
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
            if ((program.function==6 || program.function==22 || program.function==27) && !wall && missileChangedCell(start,next))
                emit(program.children[0],{float(int(next.x))+.5f,float(int(next.y))+.5f},{},effect,childAge);
            std::vector<std::pair<float, const ClientMissileTarget *>> contacts;
            if (program.collide && program.function != 19)
                for (const auto &target : targets) {
                    if ((program.holyBolt && !target.undead) || (program.function==7 && ((!effect.guidance && !effect.guidanceSearched) || (effect.guidance && target.id!=effect.guidance))) || target.id == effect.owner || target.hostile == effect.hostile ||
                        std::find(effect.contacts.begin(), effect.contacts.end(), target.id) != effect.contacts.end()) continue;
                    if (const auto at = missileUnitIntersection(start, next, program.collision.size, target.position, target.size);
                        at && *at <= fraction) contacts.emplace_back(*at, &target);
                }
            std::sort(contacts.begin(), contacts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[at, target] : contacts) {
                effect.pos = start + (next - start) * at;
                const bool hitStops=impact(effect, program, std::max(0.f, childAge - 1.f / 25.f),true,target->monster && target->large);
                if(program.returnFire && target->retaliation>=0) {
                    const auto source=std::find_if(targets.begin(),targets.end(),[&](const auto &candidate){return candidate.id==effect.owner;});
                    if(source!=targets.end()) {
                        auto retaliation=effect;retaliation.owner=target->id;retaliation.hostile=target->hostile;retaliation.level=target->retaliationRank;
                        emit(target->retaliation,target->position,source->position-target->position,retaliation,std::max(0.f,childAge-1.f/25.f));
                    }
                }
                effect.contacts.push_back(target->id);
                if(program.hitFunction==16 && program.chain && effect.remainingHits>1) {
                    std::vector<uint64_t> eligible;
                    for(const auto &candidate:targets) {
                        const Vec d{std::floor(candidate.position.x)-std::floor(effect.pos.x),std::floor(candidate.position.y)-std::floor(effect.pos.y)};
                        if(candidate.id==effect.owner || candidate.hostile==effect.hostile || candidate.id==target->id ||
                            d.x*d.x+d.y*d.y>float(program.chain->range*program.chain->range) ||
                            !grid.missileSegment(effect.pos-origin,candidate.position-origin,{4,1})) continue;
                        eligible.push_back(candidate.id.value);
                    }
                    const EntityId nextId{missileChainSuccessor(target->id.value,eligible)};
                    const auto nextTarget=std::find_if(targets.begin(),targets.end(),[&](const auto &t) {return t.id==nextId;});
                    if(nextTarget!=targets.end()) {
                        const size_t queued=pending.size();
                        emit(effect.missileId,{std::floor(effect.pos.x)+.5f,std::floor(effect.pos.y)+.5f},nextTarget->position-effect.pos,effect,std::max(0.f,childAge-1.f/25.f));
                        if(pending.size()>queued) {pending.back().remainingHits=effect.remainingHits-1;pending.back().contacts.push_back(target->id);}
                    }
                }
                if (hitStops && program.killOnContact && effect.pierce-- <= 0) {
                    fraction = at; finished = true; wall = false; break;
                }
            }
            if (program.function == 8 || program.function == 18) {
                const int loops = std::clamp(program.parameters[0], 1, 32);
                for (int i = 0; i < loops; ++i)
                    emit(program.children[0], start + (next - start) * (fraction * float(i) / loops), {}, effect,
                        std::max(0.f, childAge - float(i) / (25.f * loops)));
            }
            effect.pos = start + (next - start) * fraction;
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
            if (program.function == 4 && !program.childServerSent)
                poisonSmoke(std::max(0.f, effect.age - effect.duration));
            if (program.function == 19 && program.hitParameters[0] > 0) {
                const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
                for (const auto direction : missileRingBurst(program.hitParameters[0]))
                    emit(program.hitChildren[0], anchor, direction, effect,
                        std::max(0.f, effect.age - effect.duration));
            } else if (program.explodeOnExpiry || program.hitFunction==52 || program.hitFunction==53 || program.hitFunction==56 || program.hitFunction==54 || program.hitFunction==55 || program.hitFunction==25 || program.hitFunction==26 || (program.function==9 && program.hitFunction==18))
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
