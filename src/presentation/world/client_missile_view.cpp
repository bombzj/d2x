#include "presentation/scene_view.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "world/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace d2x {
bool SceneView::launchClientMissile(int id, Vec start, Vec target, int level, float delay,
                                  std::optional<float> remaining, int pathIndex,
                                  EntityId owner, bool hostile, int pierce) {
    const auto found = assets_.clientMissilePrograms.find(id);
    if (found == assets_.clientMissilePrograms.end()) return false;
    const auto &program = found->second;
    if (program.function != 1 && program.function != 8 && program.function != 18 &&
        program.function != 19 && program.function != 20) return false;
    // Lightning's parent is deliberately invisible; its MPQ child is the art.
    if (!assets_.ensureProjectile(id) &&
        !((program.function == 8 || program.function == 18) && program.children[0] >= 0 &&
          assets_.ensureProjectile(program.children[0]))) return false;
    level = std::max(1, level);
    const auto nativeVelocity = (int64_t(program.velocity) + int64_t(level) * program.velocityPerLevel / 8) * 256 * 75 / 100;
    if (nativeVelocity < 0 || nativeVelocity > std::numeric_limits<int>::max() ||
        (program.acceleration && program.maximumVelocity <= 0)) return false;
    const int velocity = int(nativeVelocity);
    const float tableDuration = float(program.frames + level * program.framesPerLevel) / 25.f;
    const float fullDuration = tableDuration > 0 ? tableDuration : assets_.projectileVisuals.at(id).lifetime;
    const float duration = remaining.value_or(fullDuration);
    if (duration <= 0 || !std::isfinite(duration) || !std::isfinite(delay) ||
        !std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(target.x) || !std::isfinite(target.y)) return false;
    // Native coordinate-target creation advances both axes for a zero ray.
    if (velocity > 0 && (target - start).length() < .001f) target = start + Vec{1, 1};
    ClientMissile effect{id, start, (target - start).unit() * (float(velocity) * 25.f / 4096.f),
        -std::max(0.f, delay), duration, target - start};
    effect.flight = true; effect.level = level; effect.velocityFixed = velocity;
    effect.acceleration = program.acceleration; effect.owner = owner; effect.hostile = hostile; effect.pierce = pierce;
    effect.soundEmitter = {(uint64_t{1} << 63) | ++nextClientMissile_};
    effect.animationOffset = remaining ? std::max(0.f, fullDuration - duration) : 0;
    effect.frame = int(effect.animationOffset * 25.f + .00001f);
    effect.turnTarget = target - start;
    if (pathIndex >= 0) {
        effect.duration = std::min(effect.duration, 77.f / 25.f);
        const auto path = chargedBoltPath(start, target, pathIndex, int(effect.duration * 25.f + .5f));
        effect.path.assign(path.begin(), path.end());
    }
    if (clientMissiles_.size() >= 2048) return false;
    clientMissiles_.push_back(std::move(effect)); return true;
}
Vec SceneView::clientMissilePosition(const ClientMissile &effect, const Grid &grid, Vec origin) const {
    if (!effect.flight || effect.age < 0) return effect.pos;
    const float remainder = std::clamp(effect.age + effect.animationOffset - float(effect.frame) / 25.f, 0.f, 1.f / 25.f);
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
    auto emit = [&](int id, Vec position, Vec direction, const ClientMissile &parent, float age) {
        const size_t before = clientMissiles_.size();
        if (id < 0 || !launchClientMissile(id, position, position + direction, parent.level,
                0, {}, -1, parent.owner, parent.hostile)) return;
        auto child = std::move(clientMissiles_.back()); clientMissiles_.resize(before);
        child.age = std::max(0.f, age); pending.push_back(std::move(child));
    };
    auto impact = [&](const ClientMissile &effect, const ClientMissileProgram &program, float overshoot) {
        // These are client contact images, not confirmation of damage or debuffs.
        const int main = program.hitFunction == 14 || program.hitFunction == 3
            ? program.hitChildren[0] : program.explosion;
        emit(main, effect.pos, {}, effect, overshoot);
        const auto first = clientMissiles_.size();
        createMissileImpactVisuals(effect.missileId, effect.pos);
        for (size_t index = first; index < clientMissiles_.size(); ++index) {
            auto child = std::move(clientMissiles_[index]);
            child.age = std::max(0.f, overshoot); pending.push_back(std::move(child));
        }
        clientMissiles_.resize(first);
        assets_.audio.play("missile-hit:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
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
                    if (age < duration) clientMissiles_.push_back({id, effect.pos, {}, age, duration, effect.direction});
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
        const int lastFrame = int(std::min(effect.age, effect.duration) * 25.f + effect.animationOffset * 25.f + .00001f);
        bool finished = false;
        while (effect.frame < lastFrame && !finished) {
            const int frame = effect.frame;
            const float childAge = std::max(0.f, effect.age + effect.animationOffset - float(frame) / 25.f);
            if (!frame) assets_.audio.play("missile-release:" + std::to_string(effect.missileId), uint64_t(view_.animationTime * 25.f));
            if (program.function == 19 && program.parameters[0] > 0 && frame % program.parameters[0] == 0) {
                const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
                emit(program.children[0], anchor, missileRingDirection(effect.directionIndex), effect, childAge);
                effect.directionIndex = (effect.directionIndex + program.parameters[1]) & 63;
            }
            if (program.function == 20 && frame < program.parameters[0] && program.parameters[1] > 0 &&
                frame % program.parameters[1] == 0) {
                const int x = int(effect.turnTarget.x), y = int(effect.turnTarget.y);
                effect.turnTarget = {float((x - y) / 2), float((x + y) / 2)};
                effect.velocity = effect.turnTarget.unit() * (float(effect.velocityFixed) * 25.f / 4096.f);
            }
            // PathMisc::sub_6FD5CEB0 adjusts native fixed velocity every five ticks.
            if (effect.acceleration && (frame + 1) % 5 == 0) {
                effect.velocityFixed = std::max(0, effect.velocityFixed + effect.acceleration);
                if (effect.velocityFixed >= program.maximumVelocity) {
                    effect.velocityFixed = program.maximumVelocity; effect.acceleration = 0;
                }
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
            if ((program.collision.mask & 0x0005) && !grid.missileSegment(effect.pos - origin, next - origin, program.collision)) {
                // Find the visible contact using the common terrain/object ray.
                float low = 0, high = 1;
                for (int i = 0; i < 12; ++i) {
                    const float middle = (low + high) * .5f;
                    if (grid.missileSegment(effect.pos - origin, effect.pos + (next - effect.pos) * middle - origin, program.collision)) low = middle;
                    else high = middle;
                }
                fraction = low; wall = true;
            }
            const Vec start = effect.pos;
            std::vector<std::pair<float, const ClientMissileTarget *>> contacts;
            if (program.collide && program.function != 19)
                for (const auto &target : targets) {
                    if (target.id == effect.owner || target.hostile == effect.hostile ||
                        std::find(effect.contacts.begin(), effect.contacts.end(), target.id) != effect.contacts.end()) continue;
                    if (const auto at = missileUnitIntersection(start, next, program.collision.size, target.position, target.size);
                        at && *at <= fraction) contacts.emplace_back(*at, &target);
                }
            std::sort(contacts.begin(), contacts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[at, target] : contacts) {
                effect.pos = start + (next - start) * at;
                impact(effect, program, std::max(0.f, childAge - 1.f / 25.f));
                effect.contacts.push_back(target->id);
                if (program.killOnContact && effect.pierce-- <= 0) {
                    fraction = at; finished = true; wall = false; break;
                }
            }
            if (program.function == 8 || program.function == 18) {
                const int loops = std::clamp(program.parameters[0], 1, 32);
                for (int i = 0; i < loops; ++i)
                    emit(program.children[0], start + (next - start) * (fraction * float(i) / loops), {}, effect,
                        std::max(0.f, childAge - float(i) / (25.f * loops)));
            }
            effect.pos = start + (next - start) * fraction; ++effect.frame;
            if (wall) {
                impact(effect, program, std::max(0.f, childAge - 1.f / 25.f));
                finished = true;
            }
        }
        if (!finished && effect.age + .00001f >= effect.duration) {
            if (program.function == 19 && program.hitParameters[0] > 0) {
                const Vec anchor{std::floor(effect.pos.x) + .5f, std::floor(effect.pos.y) + .5f};
                for (int direction = 0; direction < 64; direction += program.hitParameters[0])
                    emit(program.hitChildren[0], anchor, missileRingDirection(direction), effect,
                        std::max(0.f, effect.age - effect.duration));
            } else if (program.explodeOnExpiry) impact(effect, program, std::max(0.f, effect.age - effect.duration));
            finished = true;
        }
        if (!finished && clientMissiles_.size() < 2048) clientMissiles_.push_back(std::move(effect));
    }
    if (multiplayer()) {
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
