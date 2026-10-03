#include "client/local_actor_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include <algorithm>

namespace d2x {
LocalActorClient::LocalActorClient(GameSession &session) : session_(session) {}

ActorView LocalActorClient::controlledActor() const {
    const auto &world = session_.state();
    const auto &player = world.player;
    const auto &stats = session_.characterStats();
    ActorView actor;
    actor.id = player.id;
    actor.region = world.area.region;
    actor.position = player.pos;
    actor.look = player.look;
    actor.moving = player.moving;
    actor.dead = player.dead;
    actor.chilled = player.chill > 0;
    actor.poisoned = player.poisonRemaining > 0;
    actor.animationMode = player.dead ? "dt" : player.blockAnimation ? "bl"
        : player.hitTime > 0 ? "gh" : player.castTime > 0 ? "sc"
        : player.weaponAttack ? player.weaponAttack->timing.mode
        : player.moving ? (player.runningNow ? "rn" : "wl") : "nu";
    actor.animationRate = actor.animationMode == "rn" ? stats.runAnimationRate
        : actor.animationMode == "wl" ? stats.walkAnimationRate : 25.f;
    const float chillSpeed = actor.chilled ? .5f : 1.f;
    const float webSpeed = player.webSlowRemaining > 0
        ? std::max(0.f, 1.f + player.webSlowPercent / 100.f) : 1.f;
    actor.animationSpeed = chillSpeed * (player.moving ? webSpeed : 1.f);
    actor.movementSpeed = (player.runningNow ? stats.runSpeed : stats.walkSpeed) * chillSpeed * webSpeed;
    actor.lightRadius = stats.lightRadius;

    // Preserve the existing client frame selection. Asset frame-count clamping
    // stays in presentation, where the actual COF/DCC animation is known.
    const auto &mode = actor.animationMode;
    if (mode == "dt") actor.actionFrame = int(player.deathTime * 20);
    if (mode == "sc") actor.actionFrame = int((player.lastCastDuration - player.castTime) * player.lastCastRate);
    if (mode == "sc" && player.channel) actor.actionFrame = std::min(9, int(player.channelAge() * 25));
    if (mode == "sc" && player.lightningSequence) {
        constexpr int sequence[]{0,1,3,4,5,7,8,9,9,9,9,10,9,9,9,10,11,12,13};
        const int step = std::clamp(int((player.lastCastDuration - player.castTime) * player.lastCastRate), 0, 18);
        actor.actionFrame = sequence[step];
    }
    if (player.weaponAttack && mode == player.weaponAttack->timing.mode)
        actor.actionFrame = player.weaponAttack->animationFrame();
    if (player.blockAnimation && mode == "bl") actor.actionFrame = player.blockAnimation->animationFrame();
    if (player.charge && mode == "rn") actor.actionFrame = int(player.charge->ticks % 8);
    return actor;
}

void LocalActorClient::move(MoveIntent intent) {
    session_.submit(MoveTo{intent.destination});
}
void LocalActorClient::stopMoving() { session_.submit(StopMoving{}); }
void LocalActorClient::toggleRun() { session_.submit(ToggleRun{}); }
} // namespace d2x
