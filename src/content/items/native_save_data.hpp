#pragma once
#include "gameplay/items/native_save.hpp"

namespace d2x {
struct ClassicData;
NativeItemSaveRules prepareNativeItemSaveRules(const ClassicData &);
}