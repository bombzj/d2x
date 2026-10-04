#pragma once
#include "content/classic_data.hpp"
#include "d2s_stats.hpp"
#include <span>

namespace d2x {
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
    std::vector<D2sStat> stats;
    std::array<std::vector<D2sStat>, 5> setStats;
};
struct D2sItemRead {
    D2sItem item;
    size_t bytesRead = 0;
};
D2sItemRead readD2sItem(std::span<const uint8_t> bytes, const ClassicData &content);
Bytes writeD2sItem(const D2sItem &item, const ClassicData &content);
} // namespace d2x
