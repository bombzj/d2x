#include "client/local_actor_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "content/classic_data.hpp"
#include <algorithm>

namespace d2x {
LocalActorClient::LocalActorClient(GameSession &session) : session_(session), actor_(session.state().player.id) {}

ActorView LocalActorClient::controlledActor() const {
    const auto &world = session_.state();
    const auto &player = world.player;
    const auto &stats = session_.characterStats();
    ActorView actor;
    actor.id = player.id;
    actor.region = world.area.region;
    actor.position = player.movement.pos;
    actor.look = player.movement.look;
    actor.moving = player.movement.moving;
    actor.dead = player.actions.dead;
    actor.chilled = player.resources.chill > 0;
    actor.poisoned = player.resources.poisonRemaining > 0;
    actor.animationMode = player.actions.dead ? "dt" : player.actions.blockAnimation ? "bl"
        : player.actions.hitTime > 0 ? "gh" : player.actions.castTime > 0 ? "sc"
        : player.actions.weaponAttack ? player.actions.weaponAttack->timing.mode
        : player.movement.moving ? (player.movement.runningNow ? "rn" : "wl") : "nu";
    actor.animationRate = actor.animationMode == "rn" ? stats.runAnimationRate
        : actor.animationMode == "wl" ? stats.walkAnimationRate : 25.f;
    const float chillSpeed = actor.chilled ? .5f : 1.f;
    const float webSpeed = player.resources.webSlowRemaining > 0
        ? std::max(0.f, 1.f + player.resources.webSlowPercent / 100.f) : 1.f;
    actor.animationSpeed = chillSpeed * (player.movement.moving ? webSpeed : 1.f);
    actor.movementSpeed = (player.movement.runningNow ? stats.runSpeed : stats.walkSpeed) * chillSpeed * webSpeed;
    actor.lightRadius = stats.lightRadius;

    // Preserve the existing client frame selection. Asset frame-count clamping
    // stays in presentation, where the actual COF/DCC animation is known.
    const auto &mode = actor.animationMode;
    if (mode == "dt") {
        const auto timing = session_.content().playerDeath.timings.find(session_.characterAppearance() + "dthth");
        if (timing != session_.content().playerDeath.timings.end())
            actor.actionFrame = std::min(timing->second.frames - 1,
                int(player.actions.deathTime * 25 * timing->second.speed / 256));
    }
    if (mode == "sc") actor.actionFrame = int((player.skills.lastCastDuration - player.actions.castTime) * player.skills.lastCastRate);
    if (mode == "sc" && player.skills.channel) actor.actionFrame = std::min(9, int(player.skills.channelAge() * 25));
    if (mode == "sc" && player.skills.lightningSequence) {
        constexpr int sequence[]{0,1,3,4,5,7,8,9,9,9,9,10,9,9,9,10,11,12,13};
        const int step = std::clamp(int((player.skills.lastCastDuration - player.actions.castTime) * player.skills.lastCastRate), 0, 18);
        actor.actionFrame = sequence[step];
    }
    if (player.actions.weaponAttack && mode == player.actions.weaponAttack->timing.mode)
        actor.actionFrame = player.actions.weaponAttack->animationFrame();
    if (player.actions.blockAnimation && mode == "bl") actor.actionFrame = player.actions.blockAnimation->animationFrame();
    if (player.actions.charge && mode == "rn") actor.actionFrame = int(player.actions.charge->ticks % 8);
    return actor;
}

void LocalActorClient::control(ActorControlIntent intent) {
    session_.setPlayerInput({actor_, intent.direction, intent.forceRun});
}
void LocalActorClient::move(MoveIntent intent) {
    session_.submit(MoveTo{intent.destination});
}
void LocalActorClient::stopMoving() { session_.submit(StopMoving{}); }
void LocalActorClient::stopActions() {
    session_.submit(StopMoving{});
    session_.submit(StopChannel{});
}
void LocalActorClient::toggleRun() { session_.submit(ToggleRun{}); }
} // namespace d2x
