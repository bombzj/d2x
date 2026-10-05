#include "room_graph.hpp"
#include <algorithm>

namespace d2x::maze {
FamilyRules barracksRules() {
    return {167, 10, {}, 14, true};
}
void RoomMaze::placeBarracks(int direction) {
    if (direction < 0 || direction > 2)
        throw std::runtime_error("Invalid outer cloister direction");
    constexpr int extensions[]{2, 3, 0};
    int parent = -1;
    for (int index = int(rooms_.size()) - 1; index >= 0; --index) {
        const auto &candidate = rooms_[index];
        if (parent >= 0) {
            const auto &current = rooms_[parent];
            if (!((direction == 0 && candidate.x > current.x) ||
                (direction == 1 && candidate.y > current.y) ||
                (direction == 2 && candidate.x < current.x))) continue;
        }
        const int extension = extensions[direction];
        if (candidate.fixed || (candidate.mask & bits[extension])) continue;
        // GetFreeLocation probes a temporary room and frees it. Even a failed
        // overlap probe consumes AllocRoomEx's level draw; no links survive.
        Chamber probe(seed_.next());
        probe.x = candidate.x + dx[extension];
        probe.y = candidate.y + dy[extension];
        if (std::none_of(rooms_.begin(), rooms_.end(), [&](const auto &room) {
            return room.x == probe.x && room.y == probe.y;
        })) parent = index;
    }
    if (parent < 0) throw std::runtime_error("No native barracks court connection location");
    int entrance = add(parent, extensions[direction], false);
    if (entrance < 0)
        throw std::runtime_error("Cannot place original barracks court connection");
    rooms_[entrance].preset = 167;
    rooms_[entrance].variant = direction;
    rooms_[entrance].fixed = true;
    if (seed_.next() & 1) {
        special(direction, 198);
        special((direction + 1) % 4, 202);
    } else {
        special(direction, 202);
        special((direction + 1) % 4, 198);
    }
}
} // namespace d2x::maze

namespace d2x {
void connectBarracks(MapRecipe &court, MapRecipe &barracks, int courtWidth, int courtHeight) {
    auto entrance = std::find_if(barracks.pieces.begin(), barracks.pieces.end(),
                                 [](const auto &piece) { return piece.preset == 167; });
    if (entrance == barracks.pieces.end() || entrance->variant != court.variant)
        throw std::runtime_error("Barracks entrance does not match the outer cloister");
    court.width = courtWidth;
    court.height = courtHeight;
    for (const auto &piece : barracks.pieces) {
        barracks.width = std::max(barracks.width, piece.x + piece.width);
        barracks.height = std::max(barracks.height, piece.y + piece.height);
    }
    int side = 0, start = 0, end = 0, plane = 0;
    switch (court.variant) {
    case 0:
        barracks.worldX = court.worldX - entrance->width - entrance->x;
        barracks.worldY = court.worldY + courtHeight / 2 - entrance->y;
        side = 3;
        start = entrance->y;
        end = start + entrance->height;
        plane = entrance->x + entrance->width;
        break;
    case 1:
        barracks.worldX = court.worldX + courtWidth / 2 - entrance->x - 6;
        barracks.worldY = court.worldY - entrance->height - entrance->y;
        side = 0;
        start = entrance->x;
        end = start + entrance->width;
        plane = entrance->y + entrance->height;
        break;
    case 2:
        barracks.worldX = court.worldX + courtWidth - entrance->x;
        barracks.worldY = court.worldY + courtHeight / 2 - entrance->y + 1;
        side = 1;
        start = entrance->y;
        end = start + entrance->height;
        plane = entrance->x;
        break;
    default:
        throw std::runtime_error("Unsupported outer cloister variant");
    }
    int offset = side % 2 ? barracks.worldY - court.worldY : barracks.worldX - court.worldX;
    barracks.boundaries.push_back({27, side, start, end, start, end, plane});
    court.boundaries.push_back(
        {28, (side + 2) % 4, start + offset, end + offset, start + offset, end + offset});
}
} // namespace d2x
