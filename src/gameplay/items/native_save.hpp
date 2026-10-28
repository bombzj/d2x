#pragma once
#include "core/bytes.hpp"
#include "definitions.hpp"
#include "native_identity.hpp"
#include <array>
#include <map>
#include <span>

namespace d2x {
struct D2sStat {
    uint16_t id = 0;
    int64_t value = 0;
    int32_t parameter = 0;
};
struct D2sItem {
    uint32_t flags = 0x00800010, seed = 0;
    unsigned format = 101, mode = 0, body = 0, x = 0, y = 0, page = 1;
    std::string code;
    unsigned level = 1, quality = 2, graphic = 0, autoAffix = 0;
    bool hasGraphic = false;
    unsigned fileIndex = 0, rarePrefix = 0, rareSuffix = 0;
    std::array<unsigned, 3> prefixes{}, suffixes{};
    unsigned defense = 0, maxDurability = 0, durability = 0, quantity = 1;
    unsigned questDifficulty = 0, book = 0;
    unsigned sockets = 0;
    unsigned runewordId = 0;
    std::vector<D2sItem> socketedItems;
    std::vector<D2sStat> runewordStats;
    std::string personalizedName;
    std::optional<ItemEarIdentity> ear;
    std::optional<ItemRealmIdentity> realmIdentity;
    std::vector<D2sStat> stats;
    std::array<std::vector<D2sStat>, 5> setStats;
};
struct D2sItemRead { D2sItem item; size_t bytesRead = 0; };
struct NativeItemSaveRules {
    struct StatFormat { unsigned bits{},params{};int add{}; };
    struct Base { ItemDefinition definition; bool compact{},questDifficulty{},stackable{};int autoPrefix{}; };
    bool pd2Host{};
    std::map<std::string,Base,std::less<>> bases;
    std::array<std::map<unsigned,StatFormat>,3> stats;
    std::vector<int> autoAffixGroups;
    size_t rareSuffixCount{};
    std::map<int,unsigned> runewordIds;
};
D2sItemRead readNativeSavedItem(std::span<const uint8_t>,const NativeItemSaveRules &,bool ground=false);
Bytes writeNativeSavedItem(const D2sItem &,const NativeItemSaveRules &,bool ground=false);
}