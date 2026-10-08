#include "core/random.hpp"
#include "presentation/scene_view.hpp"
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
        assets_.sceneAudio.playRegistered("monster-shatter", uint64_t(view_.animationTime*25.f));
}
void SceneView::createBlizzardFall(int missileId, Vec position) {
    const auto found = assets_.blizzardFalls.find(missileId);
    if (found == assets_.blizzardFalls.end()) return;
    const auto &program = found->second;
    // MPQ fall distance/rate are client fields, not the server Range=9.
    // Variant and placement are performed by the native center program.
    // This helper handles an already selected original falling-shard image.
    const int frames = (program.fallDistance + program.fallRate - 1) / program.fallRate;
    clientMissiles_.push_back({missileId, position, {}, 0, float(frames) / 25.f, {}});
}
void SceneView::createMissileImpactVisuals(int missileId, Vec position) {
    if (const auto meteor = assets_.meteorVisuals.find(missileId); meteor != assets_.meteorVisuals.end()) {
        const auto &program = meteor->second;
        auto ring = [&](int id, int density, float radius) {
            const int count = std::max(1, density);
            for (int index = 0; index < count; ++index) {
                const float angle = 2.f * 3.14159265358979323846f * float(index) / float(count);
                const Vec offset{std::cos(angle) * radius, std::sin(angle) * radius};
                clientMissiles_.push_back({id, position + offset, {}, 0,
                    assets_.projectileVisuals.at(id).lifetime, offset});
            }
        };
        ring(program.explodeId, program.explodeDensity, 2.f);
        ring(program.mediumId, program.mediumDensity, 3.f);
        ring(program.smallId, program.smallDensity, 4.f);
        clientMissiles_.push_back({program.lightId, position, {}, 0, float(program.fireFrames) / 25.f, {}});
        return;
    }
    if (const auto ejecta = assets_.projectileFreezingEjecta.find(missileId);
        ejecta != assets_.projectileFreezingEjecta.end()) {
        // CltHit14's original directional ejecta already carries pixel motion
        // in its DCC offsets; the table velocity is zero. Legacy multiplicity
        // and offset randomization remain unverified, so only one is displayed.
        constexpr Vec directions[]{{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1},{1,0},{1,1}};
        const auto direction = directions[limitedRandom(projectileVisualRandom_, 8)];
        if (const auto *visual = assets_.ensureProjectile(ejecta->second))
            clientMissiles_.push_back({ejecta->second, position, {}, 0, visual->lifetime, direction});
        return;
    }
    const auto found = assets_.projectileImpactVariants.find(missileId);
    if (found == assets_.projectileImpactVariants.end()) return;
    // Presentation has its own random stream; debris cannot change damage,
    // accuracy, loot or the simulation's entity allocation order.
    rollRandom(projectileVisualRandom_);
    const int id = found->second[uint32_t(projectileVisualRandom_) % found->second.size()];
    if (id < 0) return;
    if (const auto *visual = assets_.ensureProjectile(id))
        clientMissiles_.push_back({id, position, {}, 0, visual->lifetime, {}});
}
} // namespace d2x
