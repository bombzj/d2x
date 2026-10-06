#include "content/items/equipment_modifiers.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include <map>
#include <utility>

namespace d2x {
void prepareEquipmentSetData(ClassicData &content) {
    std::map<std::string, int, std::less<>> pieceCounts;
    for (const auto &record : content.setItems) ++pieceCounts[record.set];
    auto &sets = content.equipmentSets;
    sets.clear();
    sets.reserve(content.setItems.size());
    for (size_t index = 0; index < content.setItems.size(); ++index) {
        const auto &record = content.setItems[index];
        EquipmentSetPiece piece;
        piece.row = int32_t(record.row); piece.set = record.set;
        piece.addFunction = record.setAddFunction; piece.instruction = index;
        piece.fullPieces = pieceCounts.at(record.set);
        for (size_t bonusIndex = 0; bonusIndex < record.setBonuses.size(); ++bonusIndex) {
            const auto &bonus = record.setBonuses[bonusIndex];
            const auto &property = bonus.property;
            piece.bonuses.push_back({bonus.pieces, bonus.perItem,
                !property.directRoll || property.minimum == property.maximum, bonusIndex});
        }
        sets.push_back(std::move(piece));
    }
}
} // namespace d2x
