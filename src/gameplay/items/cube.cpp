#include "cube_records.hpp"

namespace d2x {
bool matchesCubeInput(const CubeInput &input, const ItemInstance &item,
    const ItemDefinition &base, const CubeBaseRecord &tiers) {
    if (!input.any) {
        if (input.type) { if (!base.equipment.isType(input.code)) return false; }
        else if (input.upgraded) {
            if (input.code != item.definition && input.code != tiers.normal &&
                !(item.definition == tiers.elite && input.code == tiers.exceptional)) return false;
        } else if (input.code != item.definition) return false;
    }
    if ((input.quality && item.quality != *input.quality) ||
        (input.specialRow >= 0 && item.specialRow != input.specialRow) ||
        (input.noSockets && item.sockets) || (input.sockets && !item.sockets) ||
        (input.noEthereal && (item.nativeFlags & 0x400000u)) ||
        (input.ethereal && !(item.nativeFlags & 0x400000u)) ||
        (input.noRuneword && item.runewordRow >= 0)) return false;
    return !input.tier || item.definition == (input.tier == 1 ? tiers.normal :
        input.tier == 2 ? tiers.exceptional : tiers.elite);
}
} // namespace d2x
