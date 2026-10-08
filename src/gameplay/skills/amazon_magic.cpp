#include "amazon_magic_spec.hpp"
#include <algorithm>
namespace d2x {
std::optional<CombatEffectSpec> amazonMagicEffect(const AmazonMagicSpec &program,EntityId source,
    int skill,int rank,int curseResistance) {
    if(curseResistance>=100 || program.frames<=0 || program.state.id<0 || rank<=0) return {};
    CombatEffectSpec effect;effect.state=program.state;
    effect.source={CombatEffectSource::Skill,source,skill,rank};effect.stacking=EffectStacking::CurseLevel;
    effect.duration=EffectFrame(std::max<int64_t>(1,int64_t(program.frames)*(100-curseResistance)/100));
    effect.modifiers.defense=-program.defenseReduction;effect.modifiers.combat.slowMissiles=program.slowPercent;
    effect.visual.overlayId=program.overlay;return effect;
}
}
