#include "damage_data.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
namespace {
void add(MissileHitDamage &hit,MissileDamageElement element,int32_t value) {
    switch(element) {
    case MissileDamageElement::None:hit.channels[0]=nativeAdd(hit.channels[0],value);break;
    case MissileDamageElement::Fire:hit.channels[2]=nativeAdd(hit.channels[2],value);break;
    case MissileDamageElement::Lightning:hit.channels[3]=nativeAdd(hit.channels[3],value);break;
    case MissileDamageElement::Magic:hit.channels[1]=nativeAdd(hit.channels[1],value);break;
    case MissileDamageElement::Cold:case MissileDamageElement::Freeze:hit.channels[4]=nativeAdd(hit.channels[4],value);break;
    case MissileDamageElement::Poison:hit.channels[5]=nativeAdd(hit.channels[5],value);break;
    case MissileDamageElement::LifeDrain:hit.lifeLeech=nativeAdd(hit.lifeLeech,value);break;
    case MissileDamageElement::ManaDrain:hit.manaLeech=nativeAdd(hit.manaLeech,value);break;
    case MissileDamageElement::StaminaDrain:hit.staminaLeech=nativeAdd(hit.staminaLeech,value);break;
    case MissileDamageElement::Stun:hit.stunLength=nativeAdd(hit.stunLength,value);break;
    case MissileDamageElement::Burn:hit.burn=nativeAdd(hit.burn,value);break;
    case MissileDamageElement::Random:break;
    }
}
void assignElement(MissileHitDamage &hit,const MissileHitDamage &value,MissileDamageElement element) {
    switch(element) {
    case MissileDamageElement::None:hit.channels[0]=value.channels[0];hit.critical|=value.critical;break;
    case MissileDamageElement::Fire:hit.channels[2]=value.channels[2];break;
    case MissileDamageElement::Lightning:hit.channels[3]=value.channels[3];break;
    case MissileDamageElement::Magic:hit.channels[1]=value.channels[1];break;
    case MissileDamageElement::Cold:hit.channels[4]=value.channels[4];hit.coldLength=value.coldLength;break;
    case MissileDamageElement::Freeze:hit.channels[4]=value.channels[4];hit.freezeLength=value.freezeLength;break;
    case MissileDamageElement::Poison:hit.channels[5]=value.channels[5];hit.poisonLength=value.poisonLength;break;
    case MissileDamageElement::LifeDrain:hit.lifeLeech=value.lifeLeech;break;
    case MissileDamageElement::ManaDrain:hit.manaLeech=value.manaLeech;break;
    case MissileDamageElement::StaminaDrain:hit.staminaLeech=value.staminaLeech;break;
    case MissileDamageElement::Stun:hit.stunLength=value.stunLength;break;
    case MissileDamageElement::Burn:hit.burn=value.burn;hit.burnLength=value.burnLength;break;
    case MissileDamageElement::Random:break;
    }
}
void reset(MissileHitDamage &hit,MissileDamageElement element) {
    hit.channels={};hit.lifeLeech=hit.manaLeech=hit.staminaLeech=hit.stunLength=hit.burn=0;
    hit.bypassUndead=hit.bypassDemon=hit.bypassBeasts=false;
    if(element==MissileDamageElement::Poison) hit.coldLength=hit.freezeLength=hit.burnLength=0;
    else if(element==MissileDamageElement::Burn) hit.coldLength=hit.freezeLength=hit.poisonLength=0;
    else if(element==MissileDamageElement::Cold || element==MissileDamageElement::Freeze) hit.poisonLength=hit.burnLength=0;
    else hit.coldLength=hit.freezeLength=hit.poisonLength=hit.burnLength=0;
}
}
bool applyMissileDamageProgram(MissileHitDamage &hit,const MissileDamageProgramInput &program,
    const MissileDamageData &data,const MissileDamageMastery &mastery,MissileDamageTarget target,uint64_t &random) {
    const auto selected=[&] {
        const auto value=rollMissileElementDamage(data,program.element,mastery,target,random);
        assignElement(hit,value.hit,program.element);return value.value;
    };
    switch(program.function) {
    case 0:break;
    case 1: {
        if(program.percentage<=0) break;
        const auto converted=std::min(nativePercentage(hit.channels[0],std::min(program.percentage,100),100),hit.channels[0]);
        hit.channels[0]=nativeSubtract(hit.channels[0],converted);add(hit,program.element,converted);break;
    }
    case 2:hit.freezeLength=nativePercentage(data.coldLength,std::max(program.parameters[0],0),100);hit.coldLength=0;break;
    case 3:if(int32_t(rollRandom(random)&127)<program.parameters[0]) hit.softHit=true;break;
    case 4:hit.freezeLength=hit.coldLength;hit.coldLength=0;break;
    case 5:case 13: {
        if(program.function==13) {
            if(!program.targetMonster) break;
            hit.channels[0]=nativeAdd(hit.channels[0],nativeMultiplyDivide(hit.channels[0],program.ownerDamagePercent,100));
        }
        const auto base=selected();
        if(target.undead && program.parameters[0]>0) add(hit,program.element,nativePercentage(base,program.parameters[0],100));
        if(target.demon && program.parameters[1]>0) add(hit,program.element,nativePercentage(base,program.parameters[1],100));
        break;
    }
    case 6:hit.disableTreasure=target.present && !program.ownerHireling;break;
    case 7:
        hit.stunLength=program.parameters[0]>0?program.parameters[0]:program.skillExists && program.level>0?
            nativeAdd(program.skillParameter1,nativeMultiply(nativeSubtract(program.level,1),program.skillParameter2)):0;
        hit.hitClass=0x60;break;
    case 8:break; // ProgOverlay is an ordered domain publication.
    case 9:hit.stunLength=program.parameters[0]>0?program.parameters[0]:program.skillExists?program.skillParameter2:0;hit.hitClass=0x60;break;
    case 10:hit.freezeLength=hit.coldLength;break;
    case 11:
        if(!program.remainingPoison) return false;
        if(*program.remainingPoison>=10 && *program.remainingPoison<=program.maximumPoisonLength) hit.poisonLength=*program.remainingPoison;
        else hit.channels[5]=hit.poisonLength=0;
        break;
    case 12: {
        if(program.percentage<=0) break;
        const auto bonus=std::min(nativePercentage(hit.channels[0],std::min(program.percentage,100),100),hit.channels[0]);
        const auto value=nativeAdd(selected(),bonus);reset(hit,program.element);add(hit,program.element,value);break;
    }
    case 14: {
        uint32_t chance{};
        const auto mode=program.parameters[0],base=program.parameters[1];
        if(program.targetPlayer) {if(mode<1) break;chance=uint32_t(base);}
        else if(program.targetMonster) {
            if(!program.targetDimensionsKnown) break;
            if(mode<1) {if(!program.targetSmall) break;chance=uint32_t(base);}
            else if(mode==1) {if(program.targetSmall) chance=uint32_t(nativeMultiply(base,2));else if(!program.targetLarge) chance=uint32_t(base);else break;}
            else chance=uint32_t(nativeMultiply(base,program.targetSmall?3:!program.targetLarge?2:1));
        } else break;
        if(chance && rollRandom(random)%100<chance) hit.knockback=true;
        break;
    }
    default:break; // Native table slots after 14 are null.
    }
    return true;
}
}
