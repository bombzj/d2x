#pragma once
#include "gameplay/units/resources.hpp"

namespace d2x {
struct NativeManaPayment {Fixed8 life,mana;bool accepted{},removeBloodMana{};};
NativeManaPayment nativeManaPayment(Fixed8 life,Fixed8 mana,Fixed8 cost,bool bloodMana,int32_t bloodThreshold);
}
