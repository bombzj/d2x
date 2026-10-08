#pragma once
#include "hosting/game_host.hpp"
#include "hosting/loot_content.hpp"
#include "hosting/game_content.hpp"
#include "content/classic_data.hpp"
#include "content/character/realm_portrait.hpp"
#include <chrono>
#include <filesystem>
#include <set>
namespace d2x::hosting {
struct NativeRealmService;
struct HostedGame {
    GameHandle handle;
    server::GameSettings settings;
    RegionId town;
    uint32_t index{}, flags{};
    uint8_t capacity{}, levelDifference{}, creatorLevel{};
    bool hardcore{};
    std::string name, password, description;
    std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
};
// Shared authority and room directory. Peer protocol/storage leases live in
// NativeRealmService; only this scheduler advances instances or prepares areas.
struct NativeRealmHost {
    Archives &archives;
    std::filesystem::path root;
    std::shared_ptr<const ClassicData> content;
    std::shared_ptr<const ItemCatalog> items;
    std::unique_ptr<RealmPortraitCatalog> portraits;
    GameHost host;
    LootContent lootContent;
    uint64_t rules{};
    uint32_t nextGameIndex = 1;
    std::map<std::string, HostedGame> games;
    std::map<GameHandle, std::map<RegionId, GeneratedArea>> terrain;
    std::set<NativeRealmService *> peers;
    explicit NativeRealmHost(Archives &a, std::filesystem::path directory) : archives(a), root(std::move(directory)) {}
    void initialize();
    void advance(double seconds);
    void publishEvents(GameHandle);
    void retire(PlayerBinding);
    const HostedGame *find(GameHandle) const;
    NativeRealmService *ticket(uint32_t hash, uint16_t token) const;
};
}
