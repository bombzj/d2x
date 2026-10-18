#include "diablo_decision.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cstddef>

namespace d2x {
int diabloTargetScore(const DiabloTargetScore &in) {
    if(in.threat<=1) return 0;
    const int resists=nativeAdd(nativeAdd(in.fireResist,in.lightResist),
        nativeAdd(in.melee?in.coldResist:0,nativeMultiply(4,nativeAdd(in.damageResist,nativeMultiply(2,in.magicResist)))))/15;
    const int damage=nativeAdd(nativeAdd(nativeAdd(in.maximumDamage,in.fireDamage),nativeAdd(in.lightDamage,in.magicDamage)),
        nativeAdd(in.coldDamage,in.poisonDamage>>8))/2;
    const int skill=nativeAdd(nativeMultiply(in.leftRank,in.leftAttackRank),nativeMultiply(in.rightRank,in.rightAttackRank))/4;
    const int range=in.melee?100:in.clearRay?75:0;
    const int score=nativeAdd(nativeAdd(resists,nativeMultiply(5,nativeAdd(range,in.returnHome?100:0))),
        nativeAdd(nativeMultiply(2,nativeAdd(nativeAdd(damage,skill),in.cold?200:0)),in.lifePercentage<20?300:0))/22;
    return score?score:1;
}
std::array<int,17> diabloWeights(const DiabloWeights &in) {
    std::array<int,17> weights{};
    if(in.melee) {
        weights={0,0,40,70,0,40,24,40,15,0,0,0,0,0,0,0,0};
        if(in.lifePercentage<20) weights[2]+=10;
        if(in.cold) weights[7]=weights[8]=0;
        if(in.fireResist>in.lightResist) weights[6]-=10;
        if(in.fireResist<in.lightResist) weights[6]+=10;
        if(in.blocked) weights[5]=weights[6]=0;
        if(in.portal) weights[15]=10;
    } else if(in.blocked) {
        weights={0,0,0,0,5,0,25,0,25,40,0,0,25,0,0,0,0};
        if(in.targets<2) {weights[6]=0;weights[1]=25;weights[8]-=5;weights[9]=0;}
        if(in.far) {
            weights[13]=25;weights[1]=0;weights[12]=15;
            if(!weights[9]) weights[9]=20;
            if(in.targets<2) weights[9]-=5;
        } else if(in.specialSkill) weights[13]=15;
        if(in.further) weights[14]=60;
        if(in.portal) weights[15]=20;
    } else {
        weights={0,0,0,0,0,25,25,0,15,20,10,0,20,0,0,0,0};
        if(in.distance>25) {weights[10]=20;weights[5]=0;weights[8]-=5;}
        if(in.fireResist>in.lightResist) {weights[6]-=10;weights[8]-=10;}
        if(in.fireResist<in.lightResist) {weights[6]+=10;weights[8]+=10;}
        if(in.targets<2) weights[6]-=10;
        if(in.targets>3) weights[6]+=5;
        if(uint32_t(in.score)>60) weights[9]+=10;
        if(in.targets<2 && in.playerDifficulty<2) weights[9]=0;
        if(in.far) {
            weights[1]=0;weights[12]=10;weights[10]=0;weights[13]=15;
            if(!weights[9]) weights[9]=10;
        } else if(in.specialSkill) {weights[10]=30;weights[13]=15;weights[6]+=10;}
        if(in.further) {weights[9]=20;weights[14]=60;}
        if(in.portal) weights[15]=15;
    }
    return weights;
}
int diabloWeightedAction(const std::array<int,17> &weights,uint64_t &random) {
    int total{};for(const auto value:weights) total=nativeAdd(total,value);
    const auto draw=total>0?int(limitedRandom(random,uint32_t(total))):0;
    int cumulative{};
    for(size_t index=0;index<weights.size();++index) {
        cumulative=nativeAdd(cumulative,weights[index]);
        if(draw<cumulative) return int(index);
    }
    return 11;
}
}
