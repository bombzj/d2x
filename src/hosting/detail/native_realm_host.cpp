#include "native_realm_host.hpp"
#include "native_realm_service.hpp"
#include "hosting/loot_content.hpp"
#include "hosting/merchant_content.hpp"
#include "hosting/crafting_content.hpp"
#include "hosting/quest_content.hpp"
#include "hosting/companion_content.hpp"
#include "content/world/world_catalog.hpp"
#include <algorithm>
namespace d2x::hosting {
void NativeRealmHost::initialize() {
    if (content) return;
    auto data = sharedClassicData(archives, preparation);
    auto previews = std::make_unique<RealmPortraitCatalog>(archives);
    items = std::shared_ptr<const ItemCatalog>(data, &data->items);
    content = std::move(data); portraits = std::move(previews);
    areaPreparation = std::make_unique<AreaPreparation>(archives, content);
}
const HostedGame *NativeRealmHost::find(GameHandle handle) const {
    for (const auto &[name, game] : games) { (void)name; if (game.handle == handle) return &game; }
    return nullptr;
}
NativeRealmService *NativeRealmHost::ticket(uint32_t hash, uint16_t token) const {
    for (auto *peer : peers) if (peer->ticket && peer->binding && peer->hash == hash && peer->token == token && peer->peer.phase == GamePhase::Closed) return peer;
    return nullptr;
}
void NativeRealmHost::retire(PlayerBinding binding) {
    host.remove(binding);
    if (!host.participants(binding.game).empty()) return;
    host.destroy(binding.game); terrain.erase(binding.game);
    std::erase_if(areaPreparations, [&](const auto &entry) {
        if (entry.first.first != binding.game) return false;
        areaPreparation->cancel(entry.second); return true;
    });
    std::erase_if(games, [&](const auto &entry) { return entry.second.handle == binding.game; });
}
void NativeRealmHost::advance(double seconds) {
    using Clock=std::chrono::steady_clock;
    const auto started=performanceObserver?Clock::now():Clock::time_point{};
    // Exactly one advance per scheduler call, regardless of connected clients.
    host.advance(seconds);
    const auto simulated=performanceObserver?Clock::now():Clock::time_point{};
    // Resource preparation never occupies the simulation scheduler. Consume
    // only the oldest request per game so completion timing cannot reorder IDs.
    for (auto &[game, areas] : terrain) {
        preparePendingLoot(host, game, archives, *content, lootContent);
        prepareMerchant(host, game, *content);
        prepareCrafting(host, game, *content);
        prepareQuests(host,game,archives,*content);
        preparePendingHirelings(host,game,archives,*content,lootContent);
        for(const auto &issue:preparePendingSummons(host,game,archives,*content))
            for(auto *peer:peers) if(peer->binding && peer->binding->game==game) {
                peer->counters.lastFailure=issue;++peer->counters.failures;
            }
        const auto pending = host.pendingAreas(game);
        if (!pending) continue;
        for (const auto &request : *pending) {
            const auto key = std::pair{game, request.request};
            try {
                const auto found = areaPreparations.find(key);
                if (found == areaPreparations.end()) {
                    const auto catalog = sharedWorldCatalog(archives, request.settings.difficulty);
                    const auto &level = catalog->level(int(request.destination));
                    const auto ticket = areaPreparation->submit({request.settings.mapSeed, level.act, level.id, request.settings.difficulty,request.settings.ladderMonsterStats()});
                    areaPreparations.emplace(key, ticket);
                    break;
                }
                auto result = areaPreparation->take(found->second);
                if (!result) break;
                areaPreparations.erase(found);
                if (!result->error.empty()) throw std::runtime_error(result->error);
                auto prepared = std::move(result->area.value());
                auto [entry, inserted] = areas.emplace(request.destination, std::move(prepared.terrain));
                if (!inserted) throw std::logic_error("Prepared terrain already installed");
                try {
                    if (!host.installArea(game, {request.request, std::move(prepared.authority)})) throw std::runtime_error("Prepared area handoff expired");
                } catch (...) { areas.erase(entry); throw; }
                break;
            } catch (const std::exception &error) {
                if (const auto found = areaPreparations.find(key); found != areaPreparations.end()) {
                    areaPreparation->cancel(found->second); areaPreparations.erase(found);
                }
                host.failArea(game, request.request);
                for (auto *peer : peers) if (peer->binding && peer->binding->game == game) {
                    peer->counters.lastFailure = error.what(); ++peer->counters.failures;
                }
            }
        }
    }
    const auto prepared=performanceObserver?Clock::now():Clock::time_point{};
    Clock::time_point eventsPublished;
    {
        struct MonsterPublicationScope {
            const std::set<NativeRealmService *> &peers;
            explicit MonsterPublicationScope(const std::set<NativeRealmService *> &values):peers(values) {
                for(auto *peer:peers) {peer->monsterProjection.reset();peer->retainMonsterProjection=true;}
            }
            ~MonsterPublicationScope() {
                for(auto *peer:peers) {peer->retainMonsterProjection=false;peer->monsterProjection.reset();}
            }
        } projections{peers};
        for (const auto &[game, areas] : terrain) { (void)areas; publishEvents(game); }
        eventsPublished=performanceObserver?Clock::now():Clock::time_point{};
        for (auto *peer : peers) if (peer->binding && peer->peer.phase == GamePhase::Entered) {
            try {
                const auto view = host.read(*peer->binding);
                peer->publishAreas(view->actor.region); peer->publishGroundItems(); peer->publishPlayers(); peer->publishMonsters(); peer->publishMissiles(); peer->publishCorpses(); peer->publishObjects(); peer->publishNpcs(); peer->publishShop(); peer->publishPortals(); peer->publishItemSkills(); peer->publishMotion();
                peer->publishRoomRemovals(); peer->publishLoadComplete();
            } catch (const std::exception &error) {
                peer->failure = error.what(); peer->counters.lastFailure = error.what(); ++peer->counters.failures; peer->failGame();
            }
        }
    }
    if(performanceObserver) {
        const auto now=Clock::now();
        const auto ns=[](auto duration){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());};
        performanceObserver({ns(simulated-started),ns(prepared-simulated),ns(eventsPublished-prepared),ns(now-eventsPublished)});
    }
    // Drain published facts before retrying save/leave. Frozen party rewards
    // may require another authoritative step after their content installation.
    for (auto *peer : peers) if (peer->binding && peer->peer.phase == GamePhase::Entered) {
        const auto view = host.read(*peer->binding);
        if (view && view->life <= 0 && view->deadSettled &&
            peer->hardcoreDeathSavedRevision != view->characterRevision) {
            const auto saved = host.exportCharacter(*peer->binding);
            if (saved && saved->player.hardcoreDead) {
                try { peer->checkpoint(); }
                catch (const std::exception &error) {
                    if (peer->counters.lastFailure != error.what()) {peer->counters.lastFailure=error.what();++peer->counters.failures;}
                }
            }
        }
    }
    for(auto *peer:peers) if(peer->leavePending && peer->peer.phase==GamePhase::Entered) {
        try {peer->finishPendingLeave();}
        catch(const std::exception &error) {
            if(peer->counters.lastFailure!=error.what()) {peer->counters.lastFailure=error.what();++peer->counters.failures;}
        }
    }
}
void NativeRealmHost::publishEvents(GameHandle game) {
    const auto pending = host.pendingEvents(game); if (!pending) return;
    // Prepare recipient baselines once before draining this immutable event set.
    if (!pending->empty()) for (auto *peer : peers) {
        if (!peer->binding || peer->binding->game != game || peer->peer.phase != GamePhase::Entered) continue;
        try {
            const auto view = host.read(*peer->binding);
            peer->publishAreas(view->actor.region); peer->publishGroundItems(); peer->publishPlayers(); peer->publishMonsters();
        } catch (const std::exception &error) {
            peer->failure = error.what(); peer->counters.lastFailure = error.what(); ++peer->counters.failures; peer->failGame();
        }
    }
    for (const auto &batch : *pending) {
        for (auto *peer : peers) {
            if (!peer->binding || peer->binding->game != game || peer->peer.phase != GamePhase::Entered) continue;
            if (batch.audience.kind == server::AudienceKind::Player && peer->binding->player != batch.audience.player) continue;
            if (batch.audience.kind == server::AudienceKind::Area) {
                const auto areas = host.visibleAreas(*peer->binding);
                if (std::find(areas.begin(), areas.end(), batch.audience.area) == areas.end()) continue;
            }
            try { peer->receiveEvent(batch); }
            catch (const std::exception &error) {
                peer->failure = error.what(); peer->counters.lastFailure = error.what(); ++peer->counters.failures; peer->failGame();
            }
        }
        // A failed recipient is explicitly retired from delivery. Its live save
        // remains leased for checkpoint recovery; a closed stream cannot replay.
        if (!host.acknowledgeEvents(game, batch.sequence)) throw std::logic_error("Event acknowledgement failed");
    }
}
}
