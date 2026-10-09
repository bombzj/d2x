#include "native_realm_host.hpp"
#include "native_realm_service.hpp"
#include "hosting/character_rules.hpp"
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
    auto data = std::make_shared<const ClassicData>(loadClassicData(archives));
    auto previews = std::make_unique<RealmPortraitCatalog>(archives);
    items = std::shared_ptr<const ItemCatalog>(data, &data->items);
    rules = characterRulesFingerprint(*data); content = std::move(data); portraits = std::move(previews);
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
    std::erase_if(games, [&](const auto &entry) { return entry.second.handle == binding.game; });
}
void NativeRealmHost::advance(double seconds) {
    // Exactly one advance per scheduler call, regardless of connected clients.
    host.advance(seconds);
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
            try {
                WorldCatalog world(archives, request.settings.difficulty);
                const auto &level = world.level(int(request.destination));
                auto prepared = prepareWorldArea(archives, *content, {request.settings.mapSeed, level.act, level.id, request.settings.difficulty});
                auto [entry, inserted] = areas.emplace(request.destination, std::move(prepared.terrain));
                if (!inserted) throw std::logic_error("Prepared terrain already installed");
                try {
                    if (!host.installArea(game, {request.request, std::move(prepared.authority)})) throw std::runtime_error("Prepared area handoff expired");
                } catch (...) { areas.erase(entry); throw; }
            } catch (const std::exception &error) {
                host.failArea(game, request.request);
                for (auto *peer : peers) if (peer->binding && peer->binding->game == game) {
                    peer->counters.lastFailure = error.what(); ++peer->counters.failures;
                }
            }
        }
    }
    for (const auto &[game, areas] : terrain) { (void)areas; publishEvents(game); }
    for (auto *peer : peers) if (peer->binding && peer->peer.phase == GamePhase::Entered) {
        try {
            const auto view = host.read(*peer->binding);
            peer->publishAreas(view->actor.region); peer->publishPlayers(); peer->publishMonsters(); peer->publishGroundItems(); peer->publishCorpses(); peer->publishObjects(); peer->publishNpcs(); peer->publishShop(); peer->publishPortals(); peer->publishItemSkills(); peer->publishMotion();
        } catch (const std::exception &error) {
            peer->failure = error.what(); peer->counters.lastFailure = error.what(); ++peer->counters.failures; peer->failGame();
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
            peer->publishAreas(view->actor.region); peer->publishPlayers(); peer->publishMonsters();
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
