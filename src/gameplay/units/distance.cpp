#include "distance.hpp"
#include <algorithm>
#include <cstdlib>

namespace d2x {
int unitCoordinateDistance(int fromX, int fromY, int toX, int toY) {
    const int horizontal = std::abs(fromX - toX);
    const int vertical = std::abs(fromY - toY);
    return std::max(horizontal, vertical) + std::min(horizontal, vertical) / 2;
}
int unitCoordinateDistanceWithSize(int fromX, int fromY, int toX, int toY, int size) {
    const int horizontal = std::abs(std::abs(fromX - toX) - size);
    const int vertical = std::abs(std::abs(fromY - toY) - size);
    return std::max(horizontal, vertical) + std::min(horizontal, vertical) / 2;
}
}