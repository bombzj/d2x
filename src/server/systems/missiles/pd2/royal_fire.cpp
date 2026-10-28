#include "../system.hpp"
#include "../runtime.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::missiles {
DomainResult<std::shared_ptr<NativeBirthPublication>> System::preparePd2RoyalFire(const NativeSpawn &request,int step) {
    // Current core 3AE960 -> 3A2580 -> retail Game93D60. The path seed is
    // updated once per ring; missile constructors consume a separate local
    // SEED, while only the owner's random low DWORD is replaced.
    if(step<=0 || !ports_.nativeRules || !ports_.nativeRules->pd2 || !ports_.nativeRules->formulas)
        return {DomainStatus::Unavailable,{}};
    const auto *monster=ports_.monsters.find(request.owner);
    const auto *player=ports_.players.findActor(request.owner);
    const auto *area=ports_.areas.find(request.area);
    if((!monster && (!player || !player->entered)) || !area || area->generation!=request.generation)
        return {DomainStatus::InvalidActor,{}};
    const auto previous=runtime_->pd2PathSeeds.find(request.owner);
    const uint32_t expected=previous==runtime_->pd2PathSeeds.end()?0:previous->second;
    const uint32_t seed=pd2MissilePathSeed(expected,monster?1:0,ports_.ids.nativeIdentities().guid(request.owner));
    const auto ownerRandom=monster?monster->combatRandom:player->combatRandom;
    auto random=initialRandom(seed);
    auto center=Vec{std::floor(request.position.x)+.5f,std::floor(request.position.y)+.5f};
    if(monster && monster->identity.nativeClass==1113) {
        // DLL's explicit LucionControl arena coordinates are global subtiles.
        center=Vec{float((seed+13u)%40u+0x620fu)+.5f,float((seed+19u)%40u+0x17cdu)+.5f}-area->definition.origin;
    }
    std::vector<NativeSpawn> births;
    for(int direction=0;direction<64;) {
        auto spawn=request;spawn.position=center;spawn.target=center+missileRingDirection(direction);
        spawn.initialization=NativeSpawn::Initialization::ProgressiveSpiral;
        spawn.initialSeed=rollRandom(random);
        // Retail server only applies this edit to royalstrikechainlightning
        // (568), unlike Client40F00 which writes it to every ring missile.
        if(request.definition==568) {
            const auto &skills=ports_.nativeRules->formulas->skills;
            const auto royal=skills.find(280);
            spawn.finalParameters=std::array{nativeAdd(royal==skills.end()?0:royal->second.number("param2"),1),0};
        }
        births.push_back(std::move(spawn));
        if(step>=64-direction) break;
        direction+=step;
    }
    births.front().ownerSeedEdit=std::pair{ownerRandom,(ownerRandom&0xffffffff00000000ULL)|uint64_t(seed)};
    return prepareNativeBirth(births,0,{}, {},std::pair{expected,seed});
}
}
