#include "outdoor_cliffs.hpp"
#include <algorithm>

namespace d2x {
namespace {
struct Vertex {
    int x, y, side;
    bool linked, cliff = false;
};
}
std::vector<uint8_t> outdoorCliffEdges(const OutdoorPosition &position) {
    int width = position.width / 8, height = position.height / 8;
    std::vector<uint8_t> result(size_t(width) * height);
    if (position.level == 2 || position.level == 3 || position.level == 17)
        return result;
    std::vector<Vertex> vertices;
    for (int side : {1, 2, 3, 0}) {
        bool vertical = side % 2;
        int extent = (vertical ? position.height : position.width) - 1;
        std::vector<int> stops{0, extent};
        for (const auto &boundary : position.boundaries)
            if (boundary.side == side) {
                stops.push_back(std::clamp(boundary.contactStart, 0, extent));
                stops.push_back(std::clamp(boundary.contactEnd - 1, 0, extent));
            }
        std::sort(stops.begin(), stops.end());
        stops.erase(std::unique(stops.begin(), stops.end()), stops.end());
        if (side == 1 || side == 0)
            std::reverse(stops.begin(), stops.end());
        for (size_t index = 0; index + 1 < stops.size(); ++index) {
            int begin = stops[index], end = stops[index + 1];
            if (begin / 8 == end / 8)
                continue;
            bool linked = std::any_of(position.boundaries.begin(), position.boundaries.end(),
                                       [&](const auto &boundary) {
                                           return boundary.side == side &&
                                                  std::min(begin, end) >= boundary.contactStart &&
                                                  std::max(begin, end) < boundary.contactEnd;
                                       });
            vertices.push_back({vertical ? (side == 1 ? 0 : width - 1) : begin / 8,
                                vertical ? begin / 8 : (side == 2 ? 0 : height - 1), side, linked});
        }
    }
    int count = int(vertices.size());
    for (int start = 0; start < count; ++start) {
        const auto &previous = vertices[(start + count - 1) % count];
        const auto &current = vertices[start];
        const auto &next = vertices[(start + 1) % count];
        bool corner = ((current.x < next.x && previous.y > current.y) ||
                       (current.y > next.y && previous.x > current.x)) &&
                      !current.linked && !previous.linked;
        if (!corner)
            continue;
        int last = -1;
        for (int step = 0; step < count; ++step) {
            int index = (start + step) % count;
            const auto &vertex = vertices[index];
            const auto &following = vertices[(index + 1) % count];
            const auto &after = vertices[(index + 2) % count];
            bool stop = vertex.y < following.y || vertex.x > following.x || vertex.linked ||
                        following.linked;
            if (stop)
                break;
            if (((vertex.x < following.x && following.y < after.y) ||
                 (vertex.y > following.y && following.x < after.x)) && !following.linked)
                last = index;
        }
        if (last >= 0)
            for (int index = start;; index = (index + 1) % count) {
                vertices[index].cliff = true;
                if (index == last)
                    break;
            }
    }
    for (int index = 0; index < count; ++index) {
        const auto &vertex = vertices[index];
        if (!vertex.cliff)
            continue;
        const auto &next = vertices[(index + 1) % count];
        int stepX = (next.x > vertex.x) - (next.x < vertex.x);
        int stepY = (next.y > vertex.y) - (next.y < vertex.y);
        for (int x = vertex.x, y = vertex.y;; x += stepX, y += stepY) {
            result[size_t(y) * width + x] |= uint8_t(1u << vertex.side);
            if (x == next.x && y == next.y)
                break;
        }
    }
    return result;
}
} // namespace d2x