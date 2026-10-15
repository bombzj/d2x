#include "drain.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
namespace d2x {
int32_t nativeLeechFromScaledPercent(int32_t physical,int32_t scaled,int32_t drain) {
    auto value=nativePercentage(physical,scaled,100);
    if(drain!=100) value=nativePercentage(value,drain,100);
    return nativeDivide(value,64);
}
NativeDrainResult calculateNativeDrain(const NativeDrainInput &input) {
    NativeDrainResult result{input.life,input.mana,input.stamina};
    if((!input.life && !input.mana) || input.drainPercent<=0) return result;
    result.lifeField=nativeShiftLeft(input.life,6);result.manaField=nativeShiftLeft(input.mana,6);
    if(input.playerDifficulty) {
        if(input.lifeDivisor) result.lifeField=nativeDivide(result.lifeField,input.lifeDivisor);
        if(input.manaDivisor) result.manaField=nativeDivide(result.manaField,input.manaDivisor);
    }
    const auto physical=std::min(input.physical,input.targetLife);
    if(input.playerStyle) {
        if(physical<=0) return result;
        const auto gain=[&](int32_t amount) {return nativeLeechFromScaledPercent(physical,amount,input.drainPercent);};
        if(result.lifeField) result.lifeGain=gain(result.lifeField);
        if(result.manaField) result.manaGain=gain(result.manaField);
        result.lifeOverlay=result.lifeField!=0;result.manaOverlay=result.manaField!=0;
        result.chooseOverlay=result.lifeOverlay && result.manaOverlay;
    } else {
        result.lifeField>>=6;result.manaField>>=6;
        auto total=nativeAdd(nativeAdd(std::min(result.lifeField,physical),std::min(result.manaField,input.targetMana)),
            std::min(result.staminaField,input.targetStamina));
        if(!total) return result;
        if(input.drainPercent!=100) total=nativePercentage(total,input.drainPercent,100);
        result.lifeGain=std::min(total,input.missingLife);result.lifeOverlay=result.lifeGain>0;
    }
    return result;
}
}
