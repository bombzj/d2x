#include "amazon_magic_spec.hpp"
#include "curse_resolve.hpp"
#include <algorithm>
namespace d2x {
std::optional<CombatEffectSpec> amazonMagicEffect(const AmazonMagicSpec &program,EntityId source,
    int skill,int rank,int curseResistance) {
    if(program.state.id<0 || rank<=0) return {};
    const auto duration=curseStateDuration(program.frames,curseResistance,program.state.curse);
    if(!duration) return {};
    CombatEffectSpec effect;effect.state=program.state;
    effect.source={CombatEffectSource::Skill,source,skill,rank};effect.stacking=EffectStacking::CurseLevel;
    effect.nativeState=true;effect.refreshPassivesOnRemoval=true;
    if(*duration) effect.duration=uint32_t(*duration);
    effect.modifiers.defense=-program.defenseReduction;effect.modifiers.combat.slowMissiles=program.slowPercent;
    effect.visual.overlayId=program.overlay;return effect;
}
}
