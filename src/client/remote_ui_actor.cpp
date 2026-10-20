#include "remote_ui_clients_impl.hpp"

namespace d2x {
ActorView RemoteUiClients::Impl::Actor::controlledActor() const {
            ActorView v; v.id = o.characterView.actor; v.region = o.mapView.region;
            v.dead = o.characterView.dead; v.position = o.mapView.observer;
            if (v.dead) {
                const auto &online = o.session.read();
                v.difficulty = online.load.difficulty;
                for (const auto &character : online.characters)
                    if (character.name == online.selectedCharacter) { v.hardcore = character.hardcore; break; }
                if (const auto lost = o.stat("goldlost"); lost && *lost >= 0 && *lost <= UINT32_MAX)
                    v.deathGoldLost = uint32_t(*lost);
            }
            if (lightActor != v.id || lightGeneration != o.generation) {
                lightActor = v.id; lightGeneration = o.generation; lightRadius = 13;
            }
            // Client 18BC0 -> A8B60: a nonpositive request leaves the
            // existing light unchanged; positive requests update immediately.
            if (const auto id=o.session.read().load.playerUnitId) if (const auto light=o.light({0,*id})) {
                const int64_t requested = 13 + int64_t(light->radiusModifier);
                if (requested > 0) lightRadius = int(std::min<int64_t>(18, requested));
            }
            v.lightRadius = lightRadius;
            return v;
        }
bool RemoteUiClients::Impl::Actor::control(ActorControlIntent intent) {
            if (intent.action == ActorControlIntent::Action::Interact) {
                if (!intent.target) return false;
                const auto raw = intent.target.value - 1;
                const OnlineUnitKey key{uint8_t(raw >> 32), uint32_t(raw)};
                const bool accepted = o.control.interact(key, o.run || intent.forceRun, o.context, intent.displayOrigin);
                if (!accepted) o.notice = o.control.reason();
                return accepted;
            }
            o.control.cancelMovement();
            if (intent.action == ActorControlIntent::Action::Pickup) {
                if (!intent.item) return false;
                OnlineItemCommand request; request.action = OnlineItemAction::Pickup;
                request.item = guid(intent.item->id); request.itemRevision = intent.item->revision;
                request.toCursor = intent.toCursor; request.context = o.context;
                const bool accepted = o.items.submit(o.session, request);
                if (!accepted) o.notice = o.items.reason();
                return accepted;
            }
            OnlineCombatCommand request; request.action = OnlineCombatCommand::Action::Cast;
            request.hand = intent.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
            request.stationary = intent.stationary; request.repeat = intent.repeat;
            request.context = o.context;
            if (intent.target) {
                const auto raw = intent.target.value - 1;
                request.target = OnlineUnitKey{uint8_t(raw >> 32), uint32_t(raw)};
            } else if (std::isfinite(intent.point.x) && std::isfinite(intent.point.y) &&
                intent.point.x >= 0 && intent.point.y >= 0 && intent.point.x <= UINT16_MAX && intent.point.y <= UINT16_MAX)
                request.point = OnlinePoint{uint16_t(intent.point.x), uint16_t(intent.point.y)};
            else return false;
            const bool accepted = o.combat.submit(request);
            if (!accepted) o.notice = o.combat.reason();
            return accepted;
        }
bool RemoteUiClients::Impl::Actor::move(MoveIntent intent) {
            if (!std::isfinite(intent.destination.x) || !std::isfinite(intent.destination.y) ||
                intent.destination.x < 0 || intent.destination.y < 0 ||
                intent.destination.x > UINT16_MAX || intent.destination.y > UINT16_MAX) return false;
            const bool accepted = o.control.move({uint16_t(intent.destination.x), uint16_t(intent.destination.y)},
                o.run || intent.forceRun, o.context, intent.displayOrigin);
            if (!accepted) o.notice = o.control.reason();
            return accepted;
        }
void RemoteUiClients::Impl::Actor::stopMoving() { o.control.cancelMovement(); }
void RemoteUiClients::Impl::Actor::stopActions() {
            o.control.cancelMovement();
            OnlineCombatCommand c; c.action = OnlineCombatCommand::Action::Stop;
            c.context = o.context;
            o.combat.submit(c);
        }
void RemoteUiClients::Impl::Actor::toggleRun() { o.run = !o.run; }
void RemoteUiClients::Impl::Actor::respawn() {
            o.control.cancelMovement();
            if (!o.session.resurrect(o.context)) o.notice = o.session.read().error
                ? o.session.read().error->message : "Waiting for the server resurrection response.";
        }
}
