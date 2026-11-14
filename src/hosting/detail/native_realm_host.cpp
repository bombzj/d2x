#include "gameplay/character/persistent_character.hpp"
#include "server/runtime/facts/combat.hpp"
#include "server/runtime/facts/inventory.hpp"
#include "server/runtime/facts/social.hpp"
#include "server/runtime/facts/travel.hpp"
#include "server/systems/world/system.hpp"
#include "server/game_messages.hpp"
#include "server/area_store.hpp"
#include "server/game_instance.hpp"
#include "native_realm_host.hpp"
#include "native_realm_service.hpp"
#include "hosting/loot_content.hpp"
#include "hosting/merchant_content.hpp"
#include "hosting/crafting_content.hpp"
#include "hosting/quest_content.hpp"
#include "hosting/companion_content.hpp"
#include "hosting/native_combat_wire.hpp"
#include "content/world/world_catalog.hpp"
#include "server/runtime/event_batch.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>
namespace d2x::hosting {
struct NativeRealmHost::Retirement {
    struct Entry {
        std::unique_ptr<server::GameInstance> game;
        std::map<RegionId,GeneratedArea> terrain;
    };
    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Entry> pending;
    std::jthread worker{[this](std::stop_token stop) {
        for(;;) {
            std::unique_lock lock(mutex);
            wake.wait(lock,stop,[&]{return !pending.empty();});
            if(pending.empty()) return;
            auto entry=std::move(pending.front());pending.pop_front();
            lock.unlock();
            entry.game.reset();entry.terrain.clear();
        }
    }};
    ~Retirement() {worker.request_stop();wake.notify_all();}
    bool retire(GameHost &host,GameHandle handle,std::map<GameHandle,std::map<RegionId,GeneratedArea>> &terrain) {
        std::lock_guard lock(mutex);
        if(pending.size()>=8) return false;
        pending.emplace_back();
        auto &entry=pending.back();
        entry.game=host.release(handle);
        if(auto found=terrain.find(handle);found!=terrain.end()) {
            entry.terrain=std::move(found->second);terrain.erase(found);
        }
        wake.notify_one();
        return true;
    }
};
void NativeRealmHost::initialize() {
    if (content) return;
    auto data = sharedClassicData(archives, preparation);
    auto previews = std::make_unique<RealmPortraitCatalog>(archives);
    items = std::shared_ptr<const ItemCatalog>(data, &data->items);
    content = std::move(data); portraits = std::move(previews);
    areaPreparation = std::make_unique<AreaPreparation>(archives, content);
    retirement=std::make_shared<Retirement>();
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
    const auto started=std::chrono::steady_clock::now();
    host.remove(binding);
    if (!host.participants(binding.game).empty()) return;
    const auto removed=std::chrono::steady_clock::now();
    const bool deferred=retirement && retirement->retire(host,binding.game,terrain);
    if(!deferred) host.destroy(binding.game);
    const auto destroyed=std::chrono::steady_clock::now();
    terrain.erase(binding.game);
    const auto terrainReleased=std::chrono::steady_clock::now();
    std::erase_if(areaPreparations, [&](const auto &entry) {
        if (entry.first.first != binding.game) return false;
        areaPreparation->cancel(entry.second); return true;
    });
    std::erase_if(nearbyPreparations,[&](const auto &entry) {
        if(entry.first.first!=binding.game) return false;
        areaPreparation->cancel(entry.second);return true;
    });
    std::erase_if(games, [&](const auto &entry) { return entry.second.handle == binding.game; });
    const auto finished=std::chrono::steady_clock::now();
    const auto ms=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
    if(ms(finished-started)>=40) std::osyncstream(std::clog) << "[game-retire] game=" << binding.game.slot << ':' << binding.game.generation
        << " total_ms=" << ms(finished-started) << " remove_ms=" << ms(removed-started)
        << " destroy_ms=" << ms(destroyed-removed) << " terrain_ms=" << ms(terrainReleased-destroyed)
        << " preparation_ms=" << ms(finished-terrainReleased) << " deferred=" << deferred << '\n';
}
void NativeRealmHost::installPreparedArea(GameHandle game, uint64_t request, PreparedWorldArea prepared) {
    auto &areas = terrain.at(game);
    const auto [projection, inserted] = areas.try_emplace(prepared.authority.id);
    // Preserve the existing projection until authority accepts the ticket, including residency reloads.
    try {
        if (!host.installArea(game, {request, std::move(prepared.authority)}))
            throw std::runtime_error("Prepared area handoff expired");
    } catch (...) {
        if (inserted) areas.erase(projection);
        throw;
    }
    projection->second = std::move(prepared.terrain);
}
void NativeRealmHost::installPreparedRoomInputs(GameHandle game,const server::world::PrepareArea &request,
    std::shared_ptr<const PreparedRoomTerrain> inputs) {
    server::world::PreparedArea prepared;
    prepared.request=request.request;prepared.definition.id=request.destination;
    prepared.definition.pd2Map=request.pd2Map;prepared.roomInputs=std::move(inputs);
    if(!host.installArea(game,std::move(prepared))) throw std::runtime_error("Prepared room input handoff expired");
}
void NativeRealmHost::prepareAreaNow(GameHandle game,const server::world::PrepareArea &request) {
    const auto catalog=sharedWorldCatalog(archives,request.settings.difficulty);
    const auto &level=catalog->level(int(request.destination));
    const auto act=request.roomInputs.empty()?level.act:request.roomInputAct;
    const AreaGenerationRequest source{request.settings.mapSeed,act,level.id,request.settings.difficulty,
        request.settings.ladderMonsterStats(),request.pd2Map};
    if(request.roomInputs.empty()) installPreparedArea(game,request.request,prepareWorldArea(archives,*content,source));
    else installPreparedRoomInputs(game,request,generateRoomInputs(archives,source,request.roomInputs,request.extendRoomGraph));
    if(const auto inFlight=areaPreparations.find({game,request.request});inFlight!=areaPreparations.end()) {
        areaPreparation->cancel(inFlight->second);
        areaPreparations.erase(inFlight);
    }
}
void NativeRealmHost::advance(double seconds) {
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    const auto measure=[&](const char *stage,auto &&operation) {
        const auto begin=Clock::now();
        operation();
        const auto end=Clock::now();
        const auto milliseconds=std::chrono::duration<double,std::milli>(end-begin).count();
        if(milliseconds>=40) std::clog << "[host-work] steady_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()).count()
            << " stage=" << stage << " work_ms=" << milliseconds << '\n';
    };
    // Exactly one advance per scheduler call, regardless of connected clients.
    host.advance(seconds);
    const auto simulated=Clock::now();
    // Resource preparation never occupies the simulation scheduler. Consume
    // only the oldest request in each lane per game. Room-only installs do
    // not allocate gameplay identities and can bypass full area preparation.
    for (auto &[game, areas] : terrain) {
        // Failed unpublished map openings retire authority before the host's
        // decoded terrain. Never leave an old map occupying a future handoff.
        std::erase_if(areas,[&](const auto &entry){return !host.areaMetadata(game,entry.first);});
        measure("loot",[&]{preparePendingLoot(host, game, archives, *content, lootContent);});
        measure("merchant",[&]{prepareMerchant(host, game, *content);});
        measure("crafting",[&]{prepareCrafting(host, game, *content);});
        measure("quests",[&]{prepareQuests(host,game,archives,*content);});
        measure("hirelings",[&]{preparePendingHirelings(host,game,archives,*content,lootContent);});
        measure("summons",[&]{for(const auto &issue:preparePendingSummons(host,game,archives,*content))
            for(auto *peer:peers) if(peer->binding && peer->binding->game==game) {
                peer->counters.lastFailure=issue;++peer->counters.failures;
            }});
        const auto pending = host.pendingAreas(game);
        if (!pending) continue;
        std::erase_if(areaPreparations,[&](const auto &entry) {
            if(entry.first.first!=game || std::any_of(pending->begin(),pending->end(),
                [&](const auto &request){return request.request==entry.first.second;})) return false;
            areaPreparation->cancel(entry.second);return true;
        });
        for(const bool roomLane:{true,false}) for (const auto &request : *pending) {
            if(roomLane==request.roomInputs.empty()) continue;
            const auto key = std::pair{game, request.request};
            try {
                auto found = areaPreparations.find(key);
                if (found == areaPreparations.end()) {
                    const auto catalog = sharedWorldCatalog(archives, request.settings.difficulty);
                    const auto &level = catalog->level(int(request.destination));
                    const auto act=request.roomInputs.empty()?level.act:request.roomInputAct;
                    uint64_t ticket{};
                    if(!roomLane && !request.pd2Map) {
                        if(const auto warm=nearbyPreparations.find({game,request.destination});warm!=nearbyPreparations.end()) {
                            ticket=warm->second;
                            areaPreparation->promote(ticket);
                            nearbyPreparations.erase(warm);
                        }
                    }
                    if(!ticket) ticket=areaPreparation->submit({request.settings.mapSeed,act,level.id,request.settings.difficulty,
                        request.settings.ladderMonsterStats(),request.pd2Map},request.roomInputs,request.extendRoomGraph);
                    found=areaPreparations.emplace(key,ticket).first;
                    std::clog << "[host-content] bind game=" << game.slot << ':' << game.generation
                        << " request=" << request.request << " ticket=" << ticket << " level=" << int(request.destination) << '\n';
                }
                auto result = areaPreparation->take(found->second);
                if (!result) break;
                areaPreparations.erase(found);
                if (!result->error.empty()) throw std::runtime_error(result->error);
                if(result->roomInputs) {
                    measure("install-room-inputs",[&] {
                        installPreparedRoomInputs(game,request,std::move(result->roomInputs));
                    });
                } else measure("install-area",[&]{installPreparedArea(game, request.request, std::move(result->area.value()));});
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
    for(auto *peer:peers) if(peer->admissionPending) {
        try {peer->finishAdmission();}
        catch(const std::exception &error) {peer->rejectAdmission(6,error.what());}
    }
    // Room07 and movement can reference a level before its population inputs
    // arrive. Protocol projection uses the already installed DRLG geometry.
    for(auto &[game,areas]:terrain) {
        const auto settings=host.settings(game);
        if(!settings) continue;
        std::set<RegionId> nearby;
        for(const auto player:host.participants(game)) {
            const auto visible=host.visibleAreas({game,player});
            if(!visible.empty()) {
                if(const auto current=areas.find(visible.front());current!=areas.end())
                    for(const int level:current->second.nearbyLevels)
                        if(level<=136 && !areas.contains(RegionId(level))) nearby.insert(RegionId(level));
            }
            for(const auto region:visible) {
                if(areas.contains(region)) continue;
                const auto *definition=host.areaMetadata(game,region);
                const auto *layout=host.areaRoomLayout(game,region);
                if(!definition || !layout) throw std::logic_error("Visible room lacks its level geometry");
                auto projection=std::make_shared<Map>();
                projection->activation=*layout;
                GeneratedArea generated;
                generated.request={settings->mapSeed,definition->act,int(region),settings->difficulty};
                generated.origin=definition->origin;
                generated.map=std::move(projection);
                areas.emplace(region,std::move(generated));
            }
        }
        std::set<RegionId> required;
        if(const auto pending=host.pendingAreas(game)) for(const auto &request:*pending)
            if(request.roomInputs.empty()) required.insert(request.destination);
        std::erase_if(nearbyPreparations,[&](const auto &entry) {
            if(entry.first.first!=game || nearby.contains(entry.first.second) || required.contains(entry.first.second)) return false;
            areaPreparation->cancel(entry.second);return true;
        });
        for(const auto region:nearby) {
            const auto key=std::pair{game,region};
            if(required.contains(region) || nearbyPreparations.contains(key)) continue;
            const auto catalog=sharedWorldCatalog(archives,settings->difficulty);
            const auto &level=catalog->level(int(region));
            nearbyPreparations.emplace(key,areaPreparation->submit({settings->mapSeed,level.act,level.id,settings->difficulty,
                settings->ladderMonsterStats()}, {}, false, true));
        }
    }
    const auto prepared=Clock::now();
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
        for (const auto &[game, areas] : terrain) { (void)areas; measure("events",[&]{publishEvents(game);}); }
        eventsPublished=Clock::now();
        for (auto *peer : peers) if (peer->binding && peer->peer.phase == GamePhase::Entered) {
            try {
                const auto view = host.read(*peer->binding);
                measure("publish-areas",[&]{peer->publishAreas(view->actor.region);});
                measure("publish-ground-items",[&]{peer->publishGroundItems();});
                measure("publish-players",[&]{peer->publishPlayers();});
                measure("publish-monsters",[&]{peer->publishMonsters();});
                measure("publish-missiles",[&]{peer->publishMissiles();});
                measure("publish-corpses",[&]{peer->publishCorpses();});
                measure("publish-objects",[&]{peer->publishObjects();});
                measure("publish-npcs",[&]{peer->publishNpcs();});
                measure("publish-shop",[&]{peer->publishShop();});
                measure("publish-portals",[&]{peer->publishPortals();});
                measure("publish-item-skills",[&]{peer->publishItemSkills();});
                measure("publish-motion",[&]{peer->publishMotion();});
                measure("publish-inventory",[&]{peer->publishInventoryRefresh();});
                measure("publish-room-removals",[&]{peer->publishRoomRemovals();});
                measure("publish-load-complete",[&]{peer->publishLoadComplete();});
            } catch (const std::exception &error) {
                peer->failure = error.what(); peer->counters.lastFailure = error.what(); ++peer->counters.failures; peer->failGame();
            }
        }
    }
    const auto published=Clock::now();
    if(performanceObserver) {
        const auto now=Clock::now();
        const auto ns=[](auto duration){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());};
        performanceObserver({ns(simulated-started),ns(prepared-simulated),ns(eventsPublished-prepared),ns(now-eventsPublished)});
    }
    for(const auto &[game,areas]:terrain) {
        (void)areas;std::set<EntityId> retained;
        const auto keys=[&](const auto &values) {for(const auto &[id,value]:values) {(void)value;retained.insert(id);}};
        for(const auto *connection:peers) if(connection->binding && connection->binding->game==game) {
            const auto &peer=connection->peer;
            retained.insert(peer.warps.begin(),peer.warps.end());retained.insert(peer.missiles.begin(),peer.missiles.end());
            retained.insert(peer.shopItems.begin(),peer.shopItems.end());retained.insert(peer.shopOwner);
            retained.insert(peer.hireling.actor);
            keys(peer.roster);keys(peer.social);keys(peer.monsters);keys(peer.pets);keys(peer.petVitals);
            keys(peer.groundItems);keys(peer.corpses);keys(peer.portals);keys(peer.objects);keys(peer.states);keys(peer.stateStats);keys(peer.hover);
            for(const auto &[id,player]:peer.visible) {(void)id;retained.insert(player.actor);keys(player.equipment);}
            for(const auto &[id,monster]:peer.monsters) {(void)id;keys(monster.equipment);}
            for(const auto &[corpse,items]:peer.corpseEquipment) {retained.insert(corpse);retained.insert(items.begin(),items.end());}
            for(const auto &[corpse,owner]:peer.corpseOwners) {retained.insert(corpse);retained.insert(owner);}
            for(const auto &[relation,flags]:peer.relations) {(void)flags;retained.insert(relation.first);retained.insert(relation.second);}
        }
        host.collectNativeIdentities(game,std::move(retained));
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
    const auto finished=Clock::now();
    const auto ms=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
    if(ms(finished-started)>=40) std::clog << "[host-pump] steady_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(finished.time_since_epoch()).count()
        << " total_ms=" << ms(finished-started) << " simulation_ms=" << ms(simulated-started)
        << " preparation_ms=" << ms(prepared-simulated) << " events_ms=" << ms(eventsPublished-prepared)
        << " publication_ms=" << ms(published-eventsPublished) << " cleanup_ms=" << ms(finished-published) << '\n';
}
void NativeRealmHost::publishEvents(GameHandle game) {
    const auto pending = host.pendingEvents(game); if (!pending) return;
    std::vector<EntityId> inventoryResets;
    std::vector<EntityId> groundDrops;
    std::vector<EntityId> monsterDeaths;
    std::vector<EntityId> monsterActions;
    std::vector<std::pair<EntityId,EntityId>> relationUpdates;
    for(const auto &batch:*pending) for(const auto &fact:batch.facts) {
        if(const auto *reset=server::factIf<server::InventoryResetFact>(&fact)) inventoryResets.push_back(reset->projection.player.id);
        else if(const auto *relation=server::factIf<server::ChatRelationFact>(&fact)) relationUpdates.emplace_back(relation->from,relation->to);
        else if(const auto *drop=server::factIf<server::GroundDropFact>(&fact)) groundDrops.push_back(drop->item.id);
        else if(const auto *hit=server::factIf<server::HitFact>(&fact);hit && hit->type==1 && hit->killed) monsterDeaths.push_back(hit->target);
        else if(const auto *attack=server::factIf<server::AttackFact>(&fact);attack && attack->actorType==1) monsterActions.push_back(attack->actor);
    }
    // Prepare recipient baselines once before draining this immutable event set.
    if (!pending->empty()) for (auto *peer : peers) {
        if (!peer->binding || peer->binding->game != game || peer->peer.phase != GamePhase::Entered) continue;
        try {
            const auto view = host.read(*peer->binding);
            std::vector<Bytes> roomPackets;
            for(const auto &batch:*pending) for(const auto &fact:batch.facts) {
                const auto *position=server::factIf<server::RepositionFact>(&fact);
                if(!position || !position->revealRoom || position->actor!=view->actor.id) continue;
                const auto &destination=terrain.at(game).at(position->area);
                const auto *room=destination.map->activation.room(position->position+Vec{.5f,.5f});
                if(!room) throw std::logic_error("Reposition destination has no native room");
                roomPackets.push_back(encodeServerPacket(ServerMessage::RevealRoom,[&](auto &out) {
                    out.u16(uint16_t((int(destination.origin.x)+room->x)/5));
                    out.u16(uint16_t((int(destination.origin.y)+room->y)/5));
                    out.u8(uint8_t(position->area));
                }));
                if(position->operatedRoom) roomPackets.push_back(encodeServerPacket(ServerMessage::RevealRoom,[&](auto &out) {
                    const auto &room=*position->operatedRoom;
                    out.u16(room.x);out.u16(room.y);out.u8(uint8_t(room.area));
                }));
                if(position->notifyOwner) roomPackets.push_back(nativeReposition(*position,destination.origin,peer->identities()));
                peer->peer.lastMotion.clear();
            }
            peer->publishAreas(view->actor.region,std::move(roomPackets)); peer->publishGroundItems(groundDrops); peer->publishPlayers(false,inventoryResets,relationUpdates); peer->publishMonsters(monsterDeaths,monsterActions);
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
