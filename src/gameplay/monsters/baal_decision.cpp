#include "baal_decision.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstddef>

namespace d2x {
int baalTargetScore(const BaalTargetScore &in) {
    if(in.threat<=1) return 0;
    const int resists=nativeAdd(nativeAdd(in.fireResist,in.coldResist),
        nativeAdd(in.lightResist,nativeMultiply(4,nativeAdd(in.damageResist,nativeMultiply(2,in.magicResist)))))/15;
    const int damage=nativeAdd(nativeAdd(nativeAdd(in.maximumDamage,in.fireDamage),nativeAdd(in.lightDamage,in.magicDamage)),
        nativeAdd(in.coldDamage,in.poisonDamage>>8))/2;
    const int skill=nativeAdd(nativeMultiply(in.leftRank,in.leftAttackRank),nativeMultiply(in.rightRank,in.rightAttackRank))/4;
    const int range=in.melee?100:in.clearRay?75:0;
    // The 1.13c B6EDA gate rewards life below 20%. D2MOO's expression
    // has the opposite sign; retain the verified version-specific gate.
    const int score=nativeAdd(nativeAdd(resists,nativeMultiply(5,nativeAdd(range,in.returnHome?100:0))),
        nativeAdd(nativeMultiply(2,nativeAdd(nativeAdd(damage,skill),in.cold?200:0)),in.lifePercentage<20?300:0))/22;
    return score?score:1;
}
std::array<int,16> baalWeights(const BaalWeights &in) {
    std::array<int,16> weights;
    if(in.melee) {
        weights={0,50,0,0,0,0,0,0,20,30,150,10,10,70,40,0};
        if(!in.difficulty) {weights[1]=75;weights[13]=45;}
        if(in.targetLifePercentage<33) weights[10]+=50;
        weights[1]=nativeAdd(weights[1],nativeSubtract(100,in.lifePercentage));
        if(in.cold) weights[8]=weights[13]=weights[11]=0;
        if(in.blocked) weights[11]=weights[13]=weights[12]=0;
        return weights;
    }
    if(in.blocked) {
        weights={0,100,20,20,20,0,20,0,80,70,0,0,0,0,0,0};
        if(!in.difficulty) weights[1]=125;
        if(in.targets<2) {weights[2]=45;weights[10]=25;}
        weights[1]=nativeAdd(weights[1],nativeSubtract(100,in.lifePercentage));
        if(!in.far) {
            if(in.specialSkill) {weights[14]+=50;weights[13]+=50;}
            if(in.further) weights[5]+=60;
            return weights;
        }
        weights[2]=weights[6]=0;weights[3]+=15;weights[11]+=25;weights[13]+=25;
        if(in.further) weights[5]+=60;
        return weights;
    }
    weights={0,0,5,5,5,0,5,0,40,40,0,70,80,60,20,0};
    int bonus=100;
    if(!in.difficulty) {bonus=125;weights[13]=40;}
    weights[15]=std::max(nativeMultiply(10,nativeSubtract(2,in.clones)),0);
    if(in.distance>35) {weights[6]=15;weights[13]=0;}
    weights[1]=nativeAdd(nativeSubtract(100,in.lifePercentage),bonus);
    if(in.distance>25) {weights[14]=30;weights[11]=0;}
    if(in.targets<2) weights[11]-=10;
    if(in.targets>3) weights[13]+=25;
    if(in.score>60) weights[8]=70;
    if(in.targets<2 && in.difficulty<2) weights[8]=0;
    if(in.specialSkill && !in.far) {weights[6]+=30;weights[11]+=10;weights[14]+=15;}
    if(in.far) {weights[3]=25;weights[2]=0;weights[6]+=25;weights[14]+=35;}
    if(in.further) weights[5]=60;
    if(in.portal) weights[7]=0;
    return weights;
}
int baalWeightedAction(const std::array<int,16> &weights,uint64_t &random) {
    int total{};for(const auto value:weights) total=nativeAdd(total,value);
    const auto draw=int(limitedRandom(random,uint32_t(total)));
    int cumulative{};
    for(size_t index=0;index<weights.size();++index) {
        cumulative=nativeAdd(cumulative,weights[index]);if(draw<cumulative) return int(index);
    }
    return 1;
}
}
