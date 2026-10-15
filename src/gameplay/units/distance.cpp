#include "distance.hpp"
#include <algorithm>
#include <cstdlib>

namespace d2x {
int unitCoordinateDistance(int fromX, int fromY, int toX, int toY) {
    const int horizontal = std::abs(fromX - toX);
    const int vertical = std::abs(fromY - toY);
    return std::max(horizontal, vertical) + std::min(horizontal, vertical) / 2;
}
int unitDistanceWithSizes(int fromX,int fromY,int fromSize,int toX,int toY,int toSize) {
    const int size=fromSize/2+toSize/2;
    const int x=std::max(std::abs(fromX-toX)-size,0),y=std::max(std::abs(fromY-toY)-size,0);
    return std::max(x,y)+std::min(x,y)/2;
}
int unitCoordinateDistanceWithSize(int fromX, int fromY, int toX, int toY, int size) {
    const int horizontal = std::abs(std::abs(fromX - toX) - size);
    const int vertical = std::abs(std::abs(fromY - toY) - size);
    return std::max(horizontal, vertical) + std::min(horizontal, vertical) / 2;
}
}