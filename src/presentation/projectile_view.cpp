#include "core/random.hpp"
#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::createMissileImpactVisuals(int missileId, Vec position) {
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
    for (auto &effect : clientMissiles_) {
        effect.age += dt;
        effect.pos = effect.pos + effect.velocity * dt;
    }
    std::erase_if(clientMissiles_, [](const auto &effect) { return effect.age + .00001f >= effect.duration; });
}
} // namespace d2x
