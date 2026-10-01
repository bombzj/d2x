#include "core/random.hpp"
#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::createIceShatter(Vec position, int size) {
    // Each original DCC already contains a complete burst of ice fragments.
    // Use one burst at the monster anchor, with an independent visual variant.
    const int id = assets_.iceShatterProjectiles[size_t(std::clamp(size - 1, 0, 2))];
    constexpr Vec directions[]{{0,1},{-1,0},{0,-1},{1,0}};
    const auto direction = directions[limitedRandom(projectileVisualRandom_, 4)];
    clientMissiles_.push_back({id, position, {}, 0, assets_.projectileVisuals.at(id).lifetime, direction});
    if ((screen(position) - Vec{W / 2.f, (H - HUD) / 2.f}).length() < W)
        assets_.audio.play("monster-shatter", session_.state().frame);
}
void SceneView::createBlizzardFall(int missileId, Vec position) {
    const auto found = assets_.blizzardFalls.find(missileId);
    if (found == assets_.blizzardFalls.end()) return;
    const auto &program = found->second;
    // MPQ fall distance/rate are client fields, not the server Range=9.
    // Use the damage-bearing shard's original art while CltDo13's variant
    // selection and exact legacy initialization remain unverified.
    const int frames = (program.fallDistance + program.fallRate - 1) / program.fallRate;
    clientMissiles_.push_back({missileId, position, {}, 0, float(frames) / 25.f});
}
void SceneView::createMissileImpactVisuals(int missileId, Vec position) {
    if (const auto ejecta = assets_.projectileFreezingEjecta.find(missileId);
        ejecta != assets_.projectileFreezingEjecta.end()) {
        // CltHit14's original directional ejecta already carries pixel motion
        // in its DCC offsets; the table velocity is zero. Legacy multiplicity
        // and offset randomization remain unverified, so only one is displayed.
        constexpr Vec directions[]{{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1},{1,0},{1,1}};
        const auto direction = directions[limitedRandom(projectileVisualRandom_, 8)];
        clientMissiles_.push_back({ejecta->second, position, {}, 0,
            assets_.projectileVisuals.at(ejecta->second).lifetime, direction});
        return;
    }
    const auto found = assets_.projectileImpactVariants.find(missileId);
    if (found == assets_.projectileImpactVariants.end()) return;
    // Presentation has its own random stream; debris cannot change damage,
    // accuracy, loot or the simulation's entity allocation order.
    rollRandom(projectileVisualRandom_);
    const int id = found->second[uint32_t(projectileVisualRandom_) % found->second.size()];
    if (id < 0) return;
    clientMissiles_.push_back({id, position, {}, 0, assets_.projectileVisuals.at(id).lifetime});
}
void SceneView::advanceMissileVisuals(float dt) {
    std::vector<ClientMissile> landed;
    for (auto &effect : clientMissiles_) {
        effect.age += dt;
        effect.pos = effect.pos + effect.velocity * dt;
        if (effect.age + .00001f >= effect.duration) {
            if (const auto melt = assets_.iceShatterMelts.find(effect.missileId);
                melt != assets_.iceShatterMelts.end())
                landed.push_back({melt->second, effect.pos, {},
                    std::max(0.f, effect.age - effect.duration),
                    assets_.projectileVisuals.at(melt->second).lifetime, effect.direction});
            const auto found = assets_.blizzardFalls.find(effect.missileId);
            if (found != assets_.blizzardFalls.end()) {
                const auto &program = found->second;
                // Keep overshoot so landing does not depend on render frequency.
                landed.push_back({program.impactId, effect.pos, {},
                    std::max(0.f, effect.age - effect.duration), float(program.impactFrames) / 25.f});
            }
        }
    }
    std::erase_if(clientMissiles_, [](const auto &effect) { return effect.age + .00001f >= effect.duration; });
    for (auto &effect : landed)
        if (effect.age + .00001f < effect.duration) clientMissiles_.push_back(std::move(effect));
}
void SceneView::syncMissileAudio() {
    std::vector<SoundEmitter> emitters;
    for (const auto &[id, offset] : session_.sceneRegions())
        for (const auto &missile : session_.areaState(id).missiles) {
            const auto key = "missile-release:" + std::to_string(missile.missileId);
            if (!assets_.audio.hasEmitterSound(key)) continue;
            // Retain the existing scene sound admission range; exact legacy
            // spatial falloff/panning and EAX are separate, unverified rules.
            if ((screen(missile.pos + offset) - Vec{W / 2.f, (H - HUD) / 2.f}).length() < W)
                emitters.push_back({missile.id, key});
        }
    assets_.audio.syncEmitters(emitters, session_.state().frame);
}
} // namespace d2x
