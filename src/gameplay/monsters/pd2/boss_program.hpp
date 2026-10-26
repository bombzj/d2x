#pragma once
#include <span>
namespace d2x::pd2 {
bool scenePauseClass(int definition);
bool sceneBossAbsorption(int definition);
std::span<const int> uberMinionPool(int nativeClass);
}
