#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <map>
#include <string>
#include <string_view>

namespace d2x {
class Archives;
struct ClassicData;
class ItemCatalog;
struct ItemInstance;
std::string_view pd2ItemEditionText(int fileIndex);
std::string pd2ItemInventoryArt(const ClassicData &, const ItemInstance &);
// D2Common inventory palette group and Colors.txt row; {0,0} means no shift.
std::array<int, 2> inventoryColorTransform(const ClassicData &, const ItemCatalog &, const ItemInstance &);
// Ground flippy uses Transform/chrtransform, separately from InvTrans/invtransform.
std::array<int, 2> groundColorTransform(const ClassicData &, const ItemCatalog &, const ItemInstance &);
// The worn body layer uses the original world transform, without icon/flippy overrides.
std::array<int, 2> equipmentColorTransform(const ClassicData &, const ItemCatalog &, const ItemInstance &);
class ItemPaletteCatalog {
    Archives &archives_;
    std::map<int, Bytes> groups_;
  public:
    explicit ItemPaletteCatalog(Archives &archives) : archives_(archives) {}
    const uint8_t *row(std::array<int, 2> transform);
};
void loadItemAppearances(std::vector<ItemDefinition> &items,
                         const std::map<std::string, DataTable, std::less<>> &tables,
                         const std::vector<std::string> &armorTypes);
} // namespace d2x
