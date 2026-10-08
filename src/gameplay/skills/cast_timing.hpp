#pragma once
namespace d2x {
struct CastAnimationTiming { int frames{}, speed{}, actionFrame{}; };
struct CastTiming { int duration{}, impact{}, speed{}; };
// Normal SC timing extracted from the old single-player applySkillCastTiming.
CastTiming normalCastTiming(CastAnimationTiming, int fasterCast, int animationRate);
}
