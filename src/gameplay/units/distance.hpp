#pragma once

namespace d2x {
int unitCoordinateDistance(int fromX, int fromY, int toX, int toY);
int unitDistanceWithSizes(int fromX,int fromY,int fromSize,int toX,int toY,int toSize);
int unitCoordinateDistanceWithSize(int fromX, int fromY, int toX, int toY, int size);
}