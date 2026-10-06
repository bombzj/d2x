#include "presentation/scene_assets.hpp"
#include "content/classic_data.hpp"
#include <algorithm>

namespace d2x {
void SceneAssets::loadProjectileDefinitions(const ClassicData &content) {
    for (const auto &[code, item] : content.items.entries())
        if (item.base.projectile) weaponMissiles.emplace(code, item.base.projectile->id);
    missileDefinitions_ = std::make_unique<DataTable>(content.tables.at("missiles"));
    const auto &table = *missileDefinitions_;
    std::map<std::string_view, int> names;
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (const auto id = table.number(row, "Id")) {
            missileRows_.emplace(*id, row); names.emplace(table.value(row, "Missile"), *id);
        }
    const auto linked = [&](size_t row, std::string_view field) {
        const auto found = names.find(table.value(row, field));
        return found == names.end() ? -1 : found->second;
    };
    for (const auto &[id, row] : missileRows_) {
        auto number = [&](std::string_view field) { return table.number(row, field).value_or(0); };
        ProjectileVisual visual;
        visual.fps = float(table.number(row, "animrate").value_or(1024)) * 25.f / 1024.f;
        visual.loop = number("LoopAnim") != 0; visual.frames = number("AnimLen");
        if (number("SubLoop")) { visual.loopStart = number("SubStart"); visual.loopEnd = number("SubStop"); }
        visual.lifetime = float(number("Range")) / 25.f;
        visual.initSteps = number("InitSteps"); visual.trans = number("Trans");
        visual.lightRadius = number("Light"); visual.lightFlicker = number("Flicker") != 0;
        visual.lightColor = {uint8_t(number("Red")), uint8_t(number("Green")), uint8_t(number("Blue")), 255};
        if (visual.lifetime <= 0 && number("Explosion") && !visual.loop && visual.fps > 0)
            visual.lifetime = float(visual.frames) / visual.fps;
        if (visual.trans) translucentProjectiles.insert(id);
        projectileVisuals.emplace(id, visual);
        ClientMissileProgram program;
        program.function = number("pCltDoFunc");
        // The retail table contains annotated values such as "*16". Keep
        // these hit programs unresolved instead of treating the annotation as
        // a verified function index or aborting all scene resource loading.
        const auto hitFunction = table.value(row, "pCltHitFunc");
        program.hitFunction = hitFunction.starts_with('*') ? -1 : number("pCltHitFunc");
        program.velocity = number("Vel"); program.velocityPerLevel = number("VelLev");
        program.acceleration = number("Accel"); program.maximumVelocity = number("MaxVel") << 8;
        program.frames = number("Range"); program.framesPerLevel = number("LevRange");
        if (const auto collision = content.missileCollisions.find(id); collision != content.missileCollisions.end())
            program.collision = collision->second;
        program.collide = number("ClientCol") != 0;
        program.killOnContact = number("CollideKill") != 0;
        program.explodeOnExpiry = number("AlwaysExplode") != 0;
        program.explosion = linked(row, "ExplosionMissile");
        for (size_t i = 0; i < program.parameters.size(); ++i)
            program.parameters[i] = number("CltParam" + std::to_string(i + 1));
        for (size_t i = 0; i < program.hitParameters.size(); ++i) {
            program.hitParameters[i] = number("cHitPar" + std::to_string(i + 1));
            program.children[i] = linked(row, "CltSubMissile" + std::to_string(i + 1));
        }
        for (size_t i = 0; i < program.hitChildren.size(); ++i)
            program.hitChildren[i] = linked(row, "CltHitSubMissile" + std::to_string(i + 1));
        if (const auto child=missileRows_.find(program.children[0]); child!=missileRows_.end())
            program.childServerSent=table.number(child->second,"ClientSend").value_or(0)!=0;
        if (program.function==9 && program.hitFunction==18) {
            const auto light=missileRows_.find(program.hitChildren[1]);
            meteorVisuals.emplace(id,MeteorVisual{light==missileRows_.end()?0:table.number(light->second,"Range").value_or(0),
                program.hitChildren[0],program.hitParameters[0],program.hitChildren[1],program.hitChildren[2],program.hitChildren[3],
                program.hitParameters[1],program.hitParameters[2]});
        }
        clientMissilePrograms.emplace(id, program);
        if (program.hitFunction == 3 && (program.hitChildren[1] >= 0 || program.hitChildren[2] >= 0))
            projectileImpactVariants.emplace(id, std::array{program.hitChildren[1], program.hitChildren[2]});
        if (program.hitFunction == 14 && program.hitChildren[1] >= 0)
            projectileFreezingEjecta.emplace(id, program.hitChildren[1]);
    }
}
} // namespace d2x
