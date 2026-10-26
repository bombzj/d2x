#pragma once
#include <functional>
#include <optional>
#include <utility>
namespace d2x::pd2 {
// Current 387C60's bounded four-subtile BFS; room/collision ownership stays
// with the caller. The rectangular grid is deliberately asymmetric at +r.
std::optional<std::pair<int,int>> scenePlacement(int x,int y,int radius,const std::function<bool(int,int)> &clear);
}
