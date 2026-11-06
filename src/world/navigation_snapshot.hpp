#pragma once
#include "core/math.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace d2x {
struct Grid;
struct NavigationWorldKey {
  uint64_t game{},area{};
  uint32_t seed{};
  uint8_t act{},difficulty{};
  std::string profile;
  bool operator==(const NavigationWorldKey &) const = default;
};
class NavigationSnapshot : public std::enable_shared_from_this<NavigationSnapshot> {
  public:
    static constexpr int blockSize=32;
    using Block=std::array<uint16_t,blockSize*blockSize>;
    using Key=std::pair<int,int>;
    using Overlay=std::map<Key,uint16_t>;
    int width{},height{},originX{},originY{};
    NavigationWorldKey world;
    uint64_t publication{},scannedCells{},copiedBytes{},reusedBlocks{};
    std::shared_ptr<const NavigationSnapshot> base;
    Overlay overlay;
    uint16_t mask(int x,int y) const;
    bool sameRegion(const NavigationSnapshot &,int left,int top,int right,int bottom) const;
    bool sameGlobalRegion(const NavigationSnapshot &,int left,int top,int right,int bottom) const;
    static std::shared_ptr<const NavigationSnapshot> publish(const Grid &,Vec origin,
      const std::shared_ptr<const NavigationSnapshot> &previous,bool native=false,const std::set<Key> *dirty=nullptr,
      NavigationWorldKey world={});
    static int blockCoordinate(int value);
    std::shared_ptr<const Grid> grid() const;
    private:
    struct Directory {
      std::array<std::shared_ptr<const Directory>,2> children;
      std::shared_ptr<const Block> value;
    };
    std::shared_ptr<const Directory> root_;
    static uint64_t address(Key);
    std::shared_ptr<const Block> block(Key) const;
    static std::shared_ptr<const Directory> replace(const std::shared_ptr<const Directory> &,uint64_t,
      const std::shared_ptr<const Block> &,unsigned depth=0);
};
}