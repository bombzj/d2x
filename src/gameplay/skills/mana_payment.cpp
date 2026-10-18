#include "mana_payment.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <stdexcept>

namespace d2x {
NativeManaPayment nativeManaPayment(Fixed8 life,Fixed8 mana,Fixed8 cost,bool bloodMana,int32_t bloodThreshold) {
    if(cost<0) throw std::invalid_argument("Negative native skill mana payment");
    if(bloodMana) {
        if(life<cost) return {Fixed8::fromRaw(256),mana,false,true};
        life=Fixed8::fromRaw(nativeSubtract(life.raw(),cost.raw()));
        return {life,mana,true,life.raw()<bloodThreshold};
    }
    if(mana<cost) return {life,mana,false,false};
    return {life,Fixed8::fromRaw(nativeSubtract(mana.raw(),cost.raw())),true,false};
}
}
